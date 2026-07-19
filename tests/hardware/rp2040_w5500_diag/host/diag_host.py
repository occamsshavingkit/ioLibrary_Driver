#!/usr/bin/env python3

"""Host controller for the standalone RP2040/W5500 diagnostic firmware."""

from __future__ import annotations

import argparse
import ipaddress
import os
import selectors
import socket
import sys
import termios
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import TextIO


_REQUIRED_EVENT_KEYS = frozenset(("protocol", "seq", "stage", "event"))
_UINT32_MAX = (1 << 32) - 1
_UDP_MAGIC = bytes.fromhex("44 55 50 31")
_RECONNECT_TIMEOUT_SECONDS = 10.0
_RUN_ALL_STAGES = (
    "callback-layout",
    "transport-init",
    "chip-reset",
    "version",
    "memory-init",
    "phy-link",
    "single-register",
    "burst-register",
    "pointer-sequential",
    "pointer-burst",
    "pointer-api",
    "socket-open",
    "udp",
    "dhcp",
)


class SerialLineBuffer:
    """Frame bounded ASCII lines from nonblocking CDC reads."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[str]:
        self._buffer.extend(data)
        lines: list[str] = []
        while True:
            newline = self._buffer.find(b"\n")
            if newline < 0:
                break
            raw_line = bytes(self._buffer[:newline])
            del self._buffer[: newline + 1]
            if raw_line.endswith(b"\r"):
                raw_line = raw_line[:-1]
            if len(raw_line) > 192:
                raise ValueError("CDC line exceeds 192 bytes")
            try:
                lines.append(raw_line.decode("ascii"))
            except UnicodeDecodeError as error:
                raise ValueError("CDC line is not ASCII") from error
        if len(self._buffer) > 192:
            raise ValueError("CDC line exceeds 192 bytes")
        return lines


def write_transcript(
    stream: TextIO,
    direction: str,
    message: str,
    timestamp: datetime | None = None,
) -> None:
    """Write one timestamped transcript record in UTC."""
    observed_at = timestamp or datetime.now(timezone.utc)
    utc_timestamp = observed_at.astimezone(timezone.utc).isoformat(
        timespec="milliseconds"
    )
    utc_timestamp = utc_timestamp.replace("+00:00", "Z")
    print(f"[{utc_timestamp}] {direction} {message}", file=stream, flush=True)


def create_udp_socket(listen_ip: str, listen_port: int) -> socket.socket:
    """Create the explicitly bound nonblocking UDP echo socket."""
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        udp_socket.setblocking(False)
        udp_socket.bind((listen_ip, listen_port))
    except BaseException:
        udp_socket.close()
        raise
    return udp_socket


def find_diagnostic_device(
    sys_class_tty: Path = Path("/sys/class/tty"),
    dev_root: Path = Path("/dev"),
) -> str | None:
    """Find the RP2040 diagnostic CDC device by its exact USB identity."""
    for tty_entry in sorted(sys_class_tty.glob("ttyACM*")):
        try:
            resolved = tty_entry.resolve(strict=True)
        except (OSError, RuntimeError):
            continue
        for parent in (resolved, *resolved.parents):
            try:
                vendor = (parent / "idVendor").read_text(encoding="ascii").strip()
                product = (parent / "idProduct").read_text(encoding="ascii").strip()
            except (OSError, UnicodeError):
                continue
            if vendor.lower() == "6666" and product.lower() == "4021":
                return str(dev_root / tty_entry.name)
            break
    return None


def matches_recovered_timeout(
    event: dict[str, str], interrupted_stage: str, interrupted_sequence: int
) -> bool:
    """Check that a watchdog TIMEOUT belongs to the interrupted operation."""
    return (
        event["event"] == "TIMEOUT"
        and event.get("reset") == "watchdog"
        and event["stage"] == interrupted_stage
        and int(event["seq"]) == interrupted_sequence
    )


def update_sequence(previous: int | None, event: dict[str, str]) -> int | None:
    """Advance sequence state while treating reboot BOOT as out-of-band."""
    if event["stage"] == "system" and event["event"] == "BOOT":
        return previous
    return validate_sequence(previous, int(event["seq"]))


def update_active_operation(
    active: tuple[str, int] | None, event: dict[str, str]
) -> tuple[str, int] | None:
    """Track the START that a watchdog disconnect would interrupt."""
    operation = (event["stage"], int(event["seq"]))
    if event["event"] == "START":
        return operation
    if event["event"] in ("PASS", "FAIL", "TIMEOUT") and active == operation:
        return None
    return active


def _ipv4_address(value: str) -> str:
    try:
        return str(ipaddress.IPv4Address(value))
    except ipaddress.AddressValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def _integer_in_range(minimum: int, maximum: int):
    def parse(value: str) -> int:
        try:
            parsed = int(value, 10)
        except ValueError as error:
            raise argparse.ArgumentTypeError(f"invalid integer: {value!r}") from error
        if parsed < minimum or parsed > maximum:
            raise argparse.ArgumentTypeError(
                f"value must be between {minimum} and {maximum}"
            )
        return parsed

    return parse


def parse_args(arguments: list[str] | None = None) -> argparse.Namespace:
    """Parse the supported host-controller command line."""
    parser = argparse.ArgumentParser(
        description="Control the standalone RP2040/W5500 diagnostic firmware"
    )
    parser.add_argument("--device", required=True)
    parser.add_argument("--device-ip", type=_ipv4_address)
    parser.add_argument("--subnet", type=_ipv4_address)
    parser.add_argument("--gateway", type=_ipv4_address)
    parser.add_argument("--listen-ip", type=_ipv4_address)
    parser.add_argument("--listen-port", type=_integer_in_range(1, 65535))

    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status")
    run = commands.add_parser("run")
    run.add_argument("stage")
    repeat = commands.add_parser("repeat")
    repeat.add_argument("stage")
    repeat.add_argument("count", type=_integer_in_range(1, 100000))
    commands.add_parser("run-all")

    parsed = parser.parse_args(arguments)
    network_values = (
        parsed.device_ip,
        parsed.subnet,
        parsed.gateway,
        parsed.listen_ip,
        parsed.listen_port,
    )
    if any(value is not None for value in network_values) and not all(
        value is not None for value in network_values
    ):
        parser.error(
            "--device-ip, --subnet, --gateway, --listen-ip, and --listen-port "
            "must be provided together"
        )
    return parsed


def build_firmware_commands(arguments: argparse.Namespace) -> list[str]:
    """Translate host arguments into firmware shell commands."""
    commands: list[str] = []
    if arguments.device_ip is not None:
        commands.append(
            f"net {arguments.device_ip} {arguments.subnet} {arguments.gateway} "
            f"{arguments.listen_ip} {arguments.listen_port}"
        )

    if arguments.command == "status":
        commands.append("status")
    elif arguments.command == "run":
        commands.append(f"run {arguments.stage}")
    elif arguments.command == "repeat":
        commands.append(f"repeat {arguments.stage} {arguments.count}")
    elif arguments.command == "run-all":
        commands.append("run all")
    else:
        raise ValueError(f"unsupported command: {arguments.command}")
    return commands


class ResultTracker:
    """Classify terminal firmware events for one requested command."""

    def __init__(self, arguments: argparse.Namespace) -> None:
        if arguments.command == "run":
            self._expected = {arguments.stage: 1}
        elif arguments.command == "repeat":
            self._expected = {arguments.stage: arguments.count}
        elif arguments.command == "run-all":
            self._expected = {stage: 1 for stage in _RUN_ALL_STAGES}
        else:
            self._expected = {}
        self._passed = {stage: 0 for stage in self._expected}
        self._network_required = arguments.device_ip is not None
        self._network_seen = False
        self._failed = False
        self._timed_out = False
        self._protocol_error = False

    def observe(self, event: dict[str, str]) -> None:
        event_name = event["event"]
        stage = event["stage"]
        if event_name == "ERROR":
            self._protocol_error = True
        elif event_name == "FAIL":
            self._failed = True
        elif event_name == "TIMEOUT":
            if event.get("reset") == "watchdog":
                self._timed_out = True
            else:
                self._protocol_error = True
        elif event_name == "NET" and stage == "shell":
            self._network_seen = True
        elif event_name == "PASS" and stage in self._passed:
            self._passed[stage] += 1

    def exit_code(self) -> int:
        if self._protocol_error:
            return 4
        if self._timed_out:
            return 3
        if self._failed:
            return 2
        if self._network_required and not self._network_seen:
            return 4
        if self._passed != self._expected:
            return 4
        return 0


def handle_udp_readable(udp_socket: socket.socket) -> bool:
    """Echo one UDP datagram when it carries the diagnostic magic."""
    packet, address = udp_socket.recvfrom(65535)
    if not packet.startswith(_UDP_MAGIC):
        return False
    udp_socket.sendto(packet, address)
    return True


def open_cdc(path: str) -> int:
    """Open a CDC device in nonblocking raw mode."""
    descriptor = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attributes = termios.tcgetattr(descriptor)
        attributes[0] = 0
        attributes[1] = 0
        attributes[2] &= ~(termios.CSIZE | termios.PARENB)
        attributes[2] |= termios.CS8 | termios.CREAD | termios.CLOCAL
        attributes[3] = 0
        attributes[6][termios.VMIN] = 0
        attributes[6][termios.VTIME] = 1
        termios.tcsetattr(descriptor, termios.TCSANOW, attributes)
    except BaseException:
        os.close(descriptor)
        raise
    return descriptor


def parse_event(line: str) -> dict[str, str]:
    """Parse one protocol v1 diagnostic event line."""
    if not line.startswith("DIAG "):
        raise ValueError("event does not start with the DIAG prefix")

    tokens = line.split()
    if not tokens or tokens[0] != "DIAG":
        raise ValueError("event does not start with the DIAG prefix")

    event: dict[str, str] = {}
    for token in tokens[1:]:
        if token.count("=") != 1:
            raise ValueError(f"invalid event token: {token!r}")
        key, value = token.split("=", 1)
        if not key or not value:
            raise ValueError(f"invalid event token: {token!r}")
        if key in event:
            raise ValueError(f"duplicate event key: {key}")
        event[key] = value

    missing = _REQUIRED_EVENT_KEYS.difference(event)
    if missing:
        raise ValueError(f"event missing required keys: {', '.join(sorted(missing))}")
    if event["protocol"] != "1":
        raise ValueError(f"unsupported protocol: {event['protocol']}")
    sequence = event["seq"]
    if not sequence.isascii() or not sequence.isdecimal():
        raise ValueError(f"invalid event sequence: {sequence!r}")
    if int(sequence) > _UINT32_MAX:
        raise ValueError(f"event sequence exceeds uint32: {sequence}")
    return event


def validate_sequence(previous: int | None, current: int) -> int:
    """Accept repeated events for one operation or the next operation."""
    if current < 0 or current > _UINT32_MAX:
        raise ValueError(f"sequence outside uint32 range: {current}")
    if previous is not None and current not in (previous, previous + 1):
        raise ValueError(
            f"sequence must stay at {previous} or advance to {previous + 1}, "
            f"got {current}"
        )
    return current


def run_controller(
    arguments: argparse.Namespace,
    transcript: TextIO | None = None,
    sys_class_tty: Path = Path("/sys/class/tty"),
    dev_root: Path = Path("/dev"),
) -> int:
    """Run one host command while servicing CDC, UDP, and watchdog reconnects."""
    output = transcript or sys.stdout
    selector = selectors.DefaultSelector()
    cdc_descriptor: int | None = None
    udp_socket: socket.socket | None = None
    pending_write = bytearray()
    line_buffer = SerialLineBuffer()
    previous_sequence: int | None = None
    active_operation: tuple[str, int] | None = None
    interrupted_operation: tuple[str, int] | None = None
    reconnect_deadline: float | None = None
    next_rescan = 0.0
    state = "barrier"
    tracker = ResultTracker(arguments)

    def set_cdc_events() -> None:
        if cdc_descriptor is None:
            return
        events = selectors.EVENT_READ
        if pending_write:
            events |= selectors.EVENT_WRITE
        selector.modify(cdc_descriptor, events, "cdc")

    def queue_command(command: str) -> None:
        pending_write.extend(command.encode("ascii") + b"\n")
        write_transcript(output, "HOST>", command)
        set_cdc_events()

    def close_cdc() -> None:
        nonlocal cdc_descriptor
        if cdc_descriptor is None:
            return
        try:
            selector.unregister(cdc_descriptor)
        except (KeyError, ValueError):
            pass
        try:
            os.close(cdc_descriptor)
        except OSError:
            pass
        cdc_descriptor = None

    def begin_reconnect() -> bool:
        nonlocal interrupted_operation, reconnect_deadline, next_rescan
        nonlocal line_buffer, state
        if active_operation is None:
            return False
        interrupted_operation = active_operation
        reconnect_deadline = time.monotonic() + _RECONNECT_TIMEOUT_SECONDS
        next_rescan = 0.0
        pending_write.clear()
        line_buffer = SerialLineBuffer()
        state = "reconnect"
        close_cdc()
        return True

    def register_cdc(path: str) -> None:
        nonlocal cdc_descriptor
        descriptor = open_cdc(path)
        try:
            selector.register(descriptor, selectors.EVENT_READ, "cdc")
        except BaseException:
            try:
                os.close(descriptor)
            except OSError:
                pass
            raise
        cdc_descriptor = descriptor

    try:
        register_cdc(arguments.device)
        if arguments.listen_ip is not None:
            udp_socket = create_udp_socket(
                arguments.listen_ip, arguments.listen_port
            )
            selector.register(udp_socket, selectors.EVENT_READ, "udp")
        queue_command("status")

        while True:
            now = time.monotonic()
            if state in ("reconnect", "recovery"):
                if reconnect_deadline is None or now >= reconnect_deadline:
                    return 4
                select_timeout = min(0.1, reconnect_deadline - now)
            else:
                select_timeout = None

            selected = selector.select(select_timeout)

            if cdc_descriptor is None and state == "reconnect":
                now = time.monotonic()
                if now >= next_rescan:
                    next_rescan = now + 0.1
                    device = find_diagnostic_device(sys_class_tty, dev_root)
                    if device is not None:
                        try:
                            register_cdc(device)
                        except OSError:
                            pass
                        else:
                            state = "recovery"

            for key, mask in selected:
                if key.data == "udp":
                    if udp_socket is None:
                        return 4
                    echoed = handle_udp_readable(udp_socket)
                    write_transcript(output, "UDP", "echo" if echoed else "drop")
                    continue

                descriptor = key.fd
                if descriptor != cdc_descriptor:
                    continue

                if mask & selectors.EVENT_READ:
                    try:
                        chunk = os.read(descriptor, 4096)
                    except BlockingIOError:
                        chunk = None
                    except OSError:
                        if not begin_reconnect():
                            return 4
                        continue
                    if chunk == b"":
                        if not begin_reconnect():
                            return 4
                        continue
                    if chunk:
                        for line in line_buffer.feed(chunk):
                            write_transcript(output, "CDC<", line)
                            event = parse_event(line)
                            previous_sequence = update_sequence(
                                previous_sequence, event
                            )
                            event_name = event["event"]
                            event_stage = event["stage"]

                            active_operation = update_active_operation(
                                active_operation, event
                            )

                            if state == "recovery":
                                if event_stage == "system" and event_name == "BOOT":
                                    continue
                                if (
                                    event_name != "TIMEOUT"
                                    or interrupted_operation is None
                                ):
                                    return 4
                                if not matches_recovered_timeout(
                                    event,
                                    interrupted_operation[0],
                                    interrupted_operation[1],
                                ):
                                    return 4
                                tracker.observe(event)
                                return 3

                            if state == "barrier":
                                if event_name in ("ERROR", "TIMEOUT"):
                                    return 4
                                if event_stage == "shell" and event_name == "STATUS":
                                    if arguments.command == "status":
                                        return 0
                                    state = "active"
                                    for command in build_firmware_commands(arguments):
                                        queue_command(command)
                                    queue_command("status")
                                continue

                            if event_stage == "shell" and event_name == "STATUS":
                                return tracker.exit_code()
                            tracker.observe(event)

                if (
                    cdc_descriptor is not None
                    and descriptor == cdc_descriptor
                    and mask & selectors.EVENT_WRITE
                    and pending_write
                ):
                    try:
                        written = os.write(descriptor, pending_write)
                    except BlockingIOError:
                        written = 0
                    except OSError:
                        if not begin_reconnect():
                            return 4
                        continue
                    if written > 0:
                        del pending_write[:written]
                    set_cdc_events()
    except (OSError, ValueError, UnicodeError) as error:
        write_transcript(output, "HOST!", f"error={error}")
        return 4
    finally:
        close_cdc()
        if udp_socket is not None:
            try:
                selector.unregister(udp_socket)
            except (KeyError, ValueError):
                pass
            udp_socket.close()
        selector.close()


def main(arguments: list[str] | None = None) -> int:
    """Run the command-line controller and return its process status."""
    return run_controller(parse_args(arguments))


if __name__ == "__main__":
    raise SystemExit(main())
