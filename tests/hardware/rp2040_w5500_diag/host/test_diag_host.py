import os
import pty
import queue
import select
import selectors
import signal
import socket
import termios
import threading
import time
import unittest
from contextlib import redirect_stderr
from datetime import datetime, timezone
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import mock

from diag_host import (
    ResultTracker,
    SerialLineBuffer,
    build_firmware_commands,
    create_udp_socket,
    find_diagnostic_device,
    handle_udp_readable,
    main,
    matches_recovered_timeout,
    open_cdc,
    parse_args,
    parse_event,
    run_controller,
    update_active_operation,
    update_sequence,
    validate_sequence,
    write_transcript,
)


RUN_ALL_STAGES = (
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
PROVENANCE_EVENT = (
    "DIAG protocol=1 seq=0 stage=boot event=PASS "
    "git=0123456789abcdef0123456789abcdef01234567 dirty=1 "
    "diff=89abcdef0123456789abcdef0123456789abcdef0123456789abcdef01234567 "
    "build=2026-07-19T12:34:56Z"
)


class CommandReader:
    def __init__(self, descriptor):
        self.descriptor = descriptor
        self.buffer = bytearray()

    def read(self, timeout=3.0):
        while b"\n" not in self.buffer:
            readable, _, _ = select.select([self.descriptor], [], [], timeout)
            if not readable:
                raise TimeoutError("timed out waiting for host command")
            self.buffer.extend(os.read(self.descriptor, 4096))
        newline = self.buffer.index(b"\n")
        line = bytes(self.buffer[:newline])
        del self.buffer[: newline + 1]
        return line.rstrip(b"\r").decode("ascii")


class EventTests(unittest.TestCase):
    def test_parses_timeout(self):
        event = parse_event(
            "DIAG protocol=1 seq=17 stage=pointer-api event=TIMEOUT "
            "reset=watchdog phase=write-tx"
        )
        self.assertEqual(event["protocol"], "1")
        self.assertEqual(event["stage"], "pointer-api")
        self.assertEqual(event["event"], "TIMEOUT")

    def test_rejects_non_diag_line(self):
        with self.assertRaises(ValueError):
            parse_event("Temperature server starting")

    def test_rejects_non_key_value_token(self):
        with self.assertRaises(ValueError):
            parse_event(
                "DIAG protocol=1 seq=17 stage=pointer-api event=PASS trailing"
            )

    def test_rejects_duplicate_key(self):
        with self.assertRaises(ValueError):
            parse_event(
                "DIAG protocol=1 seq=17 stage=pointer-api event=PASS seq=18"
            )

    def test_rejects_missing_required_key(self):
        required_tokens = {
            "protocol": "protocol=1",
            "seq": "seq=17",
            "stage": "stage=pointer-api",
            "event": "event=PASS",
        }
        for missing in required_tokens:
            with self.subTest(missing=missing), self.assertRaises(ValueError):
                parse_event(
                    "DIAG "
                    + " ".join(
                        token
                        for key, token in required_tokens.items()
                        if key != missing
                    )
                )

    def test_rejects_unsupported_protocol(self):
        with self.assertRaises(ValueError):
            parse_event("DIAG protocol=2 seq=17 stage=pointer-api event=PASS")

    def test_rejects_invalid_sequence_value(self):
        for sequence in ("seventeen", "4294967296"):
            with self.subTest(sequence=sequence), self.assertRaises(ValueError):
                parse_event(
                    f"DIAG protocol=1 seq={sequence} "
                    "stage=pointer-api event=PASS"
                )

    def test_requires_literal_diag_prefix(self):
        for line in (
            " DIAG protocol=1 seq=17 stage=pointer-api event=PASS",
            "DIAGNOSTIC protocol=1 seq=17 stage=pointer-api event=PASS",
        ):
            with self.subTest(line=line), self.assertRaises(ValueError):
                parse_event(line)


class SequenceTests(unittest.TestCase):
    def test_accepts_initial_sequence(self):
        self.assertEqual(validate_sequence(None, 17), 17)

    def test_accepts_multiple_events_for_one_operation(self):
        self.assertEqual(validate_sequence(17, 17), 17)

    def test_accepts_next_operation(self):
        self.assertEqual(validate_sequence(17, 18), 18)

    def test_detects_sequence_gap(self):
        with self.assertRaises(ValueError):
            validate_sequence(17, 19)

    def test_detects_sequence_regression(self):
        with self.assertRaises(ValueError):
            validate_sequence(17, 16)


class SerialTests(unittest.TestCase):
    def setUp(self):
        self.attributes = [
            termios.IGNBRK | termios.ICRNL,
            termios.OPOST,
            termios.CS7 | termios.PARENB,
            termios.ECHO | termios.ICANON,
            termios.B9600,
            termios.B9600,
            [0] * termios.NCCS,
        ]

    @mock.patch("diag_host.termios.tcsetattr")
    @mock.patch("diag_host.termios.tcgetattr")
    @mock.patch("diag_host.os.open", return_value=42)
    def test_opens_nonblocking_raw_cdc(self, open_mock, getattr_mock, setattr_mock):
        getattr_mock.return_value = self.attributes

        descriptor = open_cdc("/dev/ttyACM0")

        self.assertEqual(descriptor, 42)
        open_mock.assert_called_once_with(
            "/dev/ttyACM0", os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK
        )
        getattr_mock.assert_called_once_with(42)
        fd, when, configured = setattr_mock.call_args.args
        self.assertEqual((fd, when), (42, termios.TCSANOW))
        self.assertEqual(configured[0], 0)
        self.assertEqual(configured[1], 0)
        self.assertEqual(configured[3], 0)
        self.assertEqual(configured[2] & termios.CSIZE, termios.CS8)
        self.assertTrue(configured[2] & termios.CREAD)
        self.assertTrue(configured[2] & termios.CLOCAL)
        self.assertEqual(configured[6][termios.VMIN], 0)
        self.assertEqual(configured[6][termios.VTIME], 1)

    @mock.patch("diag_host.os.close")
    @mock.patch("diag_host.termios.tcgetattr", side_effect=termios.error("failed"))
    @mock.patch("diag_host.os.open", return_value=42)
    def test_closes_descriptor_when_raw_setup_fails(
        self, _open_mock, _getattr_mock, close_mock
    ):
        with self.assertRaises(termios.error):
            open_cdc("/dev/ttyACM0")

        close_mock.assert_called_once_with(42)


class UdpTests(unittest.TestCase):
    def test_echoes_packet_with_big_endian_magic(self):
        packet = bytes.fromhex("44 55 50 31") + bytes(range(28))
        address = ("192.168.2.247", 49001)
        udp_socket = mock.Mock()
        udp_socket.recvfrom.return_value = (packet, address)

        echoed = handle_udp_readable(udp_socket)

        self.assertTrue(echoed)
        udp_socket.sendto.assert_called_once_with(packet, address)

    def test_rejects_packet_without_magic(self):
        packet = bytes.fromhex("31 50 55 44") + bytes(range(28))
        udp_socket = mock.Mock()
        udp_socket.recvfrom.return_value = (packet, ("192.168.2.247", 49001))

        echoed = handle_udp_readable(udp_socket)

        self.assertFalse(echoed)
        udp_socket.sendto.assert_not_called()

    def test_does_not_require_broader_packet_validation_for_echo(self):
        packet = bytes.fromhex("44 55 50 31") + b"short"
        address = ("192.168.2.247", 49001)
        udp_socket = mock.Mock()
        udp_socket.recvfrom.return_value = (packet, address)

        echoed = handle_udp_readable(udp_socket)

        self.assertTrue(echoed)
        udp_socket.sendto.assert_called_once_with(packet, address)

    def test_rejects_packet_too_short_to_contain_magic(self):
        udp_socket = mock.Mock()
        udp_socket.recvfrom.return_value = (b"DUP", ("192.168.2.247", 49001))

        echoed = handle_udp_readable(udp_socket)

        self.assertFalse(echoed)
        udp_socket.sendto.assert_not_called()


class CommandTests(unittest.TestCase):
    def test_forms_status_command(self):
        arguments = parse_args(["--device", "/dev/ttyACM0", "status"])
        self.assertEqual(build_firmware_commands(arguments), ["status"])

    def test_forms_run_command(self):
        arguments = parse_args(
            ["--device", "/dev/ttyACM0", "run", "pointer-api"]
        )
        self.assertEqual(
            build_firmware_commands(arguments), ["run pointer-api"]
        )

    def test_forms_repeat_command(self):
        arguments = parse_args(
            ["--device", "/dev/ttyACM0", "repeat", "pointer-api", "100"]
        )
        self.assertEqual(
            build_firmware_commands(arguments), ["repeat pointer-api 100"]
        )

    def test_forms_network_and_run_all_commands(self):
        arguments = parse_args(
            [
                "--device",
                "/dev/ttyACM0",
                "--device-ip",
                "192.168.2.247",
                "--subnet",
                "255.255.255.0",
                "--gateway",
                "192.168.2.1",
                "--listen-ip",
                "192.168.2.34",
                "--listen-port",
                "49000",
                "run-all",
            ]
        )
        self.assertEqual(
            build_firmware_commands(arguments),
            [
                "net 192.168.2.247 255.255.255.0 192.168.2.1 "
                "192.168.2.34 49000",
                "run all",
            ],
        )

    def test_rejects_repeat_count_outside_firmware_range(self):
        for count in ("0", "100001"):
            with self.subTest(count=count), redirect_stderr(StringIO()):
                with self.assertRaises(SystemExit):
                    parse_args(
                        [
                            "--device",
                            "/dev/ttyACM0",
                            "repeat",
                            "pointer-api",
                            count,
                        ]
                    )

    def test_rejects_partial_network_configuration(self):
        with redirect_stderr(StringIO()), self.assertRaises(SystemExit):
            parse_args(
                [
                    "--device",
                    "/dev/ttyACM0",
                    "--listen-ip",
                    "192.168.2.34",
                    "run-all",
                ]
            )


class CliTests(unittest.TestCase):
    @mock.patch("diag_host.run_controller", return_value=3)
    def test_main_returns_controller_exit_status(self, run_mock):
        exit_code = main(
            ["--device", "/dev/ttyACM0", "run", "pointer-api"]
        )

        self.assertEqual(exit_code, 3)
        arguments = run_mock.call_args.args[0]
        self.assertEqual(arguments.device, "/dev/ttyACM0")
        self.assertEqual(arguments.command, "run")
        self.assertEqual(arguments.stage, "pointer-api")


class ResultTests(unittest.TestCase):
    @staticmethod
    def network_run_all_arguments():
        return parse_args(
            [
                "--device",
                "/dev/ttyACM0",
                "--device-ip",
                "192.168.2.247",
                "--subnet",
                "255.255.255.0",
                "--gateway",
                "192.168.2.1",
                "--listen-ip",
                "192.168.2.34",
                "--listen-port",
                "49000",
                "run-all",
            ]
        )

    def test_returns_zero_when_requested_stage_passes(self):
        arguments = parse_args(
            ["--device", "/dev/ttyACM0", "run", "pointer-api"]
        )
        tracker = ResultTracker(arguments)
        tracker.observe(
            parse_event("DIAG protocol=1 seq=17 stage=pointer-api event=PASS")
        )
        self.assertEqual(tracker.exit_code(), 0)

    def test_returns_two_on_fail_without_reinterpreting_failure_code(self):
        arguments = parse_args(
            ["--device", "/dev/ttyACM0", "run", "callback-layout"]
        )
        tracker = ResultTracker(arguments)
        tracker.observe(
            parse_event(
                "DIAG protocol=1 seq=17 stage=callback-layout event=FAIL "
                "code=no-status-api"
            )
        )
        self.assertEqual(tracker.exit_code(), 2)

    def test_returns_three_on_recovered_timeout(self):
        arguments = parse_args(
            ["--device", "/dev/ttyACM0", "run", "pointer-api"]
        )
        tracker = ResultTracker(arguments)
        tracker.observe(
            parse_event(
                "DIAG protocol=1 seq=17 stage=pointer-api event=TIMEOUT "
                "reset=watchdog phase=write-tx"
            )
        )
        self.assertEqual(tracker.exit_code(), 3)

    def test_returns_four_on_shell_error(self):
        arguments = parse_args(
            ["--device", "/dev/ttyACM0", "run", "not-a-stage"]
        )
        tracker = ResultTracker(arguments)
        tracker.observe(
            parse_event(
                "DIAG protocol=1 seq=17 stage=shell event=ERROR "
                "reason=invalid-stage"
            )
        )
        self.assertEqual(tracker.exit_code(), 4)

    def test_repeat_requires_every_requested_pass(self):
        arguments = parse_args(
            ["--device", "/dev/ttyACM0", "repeat", "pointer-api", "2"]
        )
        tracker = ResultTracker(arguments)
        tracker.observe(
            parse_event("DIAG protocol=1 seq=17 stage=pointer-api event=PASS")
        )
        self.assertEqual(tracker.exit_code(), 4)

        tracker.observe(
            parse_event("DIAG protocol=1 seq=18 stage=pointer-api event=PASS")
        )
        self.assertEqual(tracker.exit_code(), 0)

    def test_run_all_requires_every_stage_and_network_acknowledgement(self):
        tracker = ResultTracker(self.network_run_all_arguments())
        tracker.observe(
            parse_event("DIAG protocol=1 seq=17 stage=shell event=NET")
        )
        for sequence, stage in enumerate(RUN_ALL_STAGES, start=18):
            tracker.observe(
                parse_event(
                    f"DIAG protocol=1 seq={sequence} stage={stage} event=PASS"
                )
            )

        self.assertEqual(tracker.exit_code(), 0)

    def test_run_all_rejects_missing_stage_result(self):
        tracker = ResultTracker(self.network_run_all_arguments())
        tracker.observe(
            parse_event("DIAG protocol=1 seq=17 stage=shell event=NET")
        )
        for sequence, stage in enumerate(RUN_ALL_STAGES[:-1], start=18):
            tracker.observe(
                parse_event(
                    f"DIAG protocol=1 seq={sequence} stage={stage} event=PASS"
                )
            )

        self.assertEqual(tracker.exit_code(), 4)

    def test_run_all_rejects_missing_network_acknowledgement(self):
        tracker = ResultTracker(self.network_run_all_arguments())
        for sequence, stage in enumerate(RUN_ALL_STAGES, start=17):
            tracker.observe(
                parse_event(
                    f"DIAG protocol=1 seq={sequence} stage={stage} event=PASS"
                )
            )

        self.assertEqual(tracker.exit_code(), 4)


class ReconnectTests(unittest.TestCase):
    def test_terminal_event_clears_completed_active_operation(self):
        active = update_active_operation(
            None,
            parse_event(
                "DIAG protocol=1 seq=17 stage=pointer-api event=START"
            ),
        )
        active = update_active_operation(
            active,
            parse_event(
                "DIAG protocol=1 seq=17 stage=pointer-api event=PASS"
            ),
        )

        self.assertIsNone(active)

    def test_matches_watchdog_timeout_to_interrupted_operation(self):
        event = parse_event(
            "DIAG protocol=1 seq=17 stage=pointer-api event=TIMEOUT "
            "reset=watchdog phase=write-tx"
        )
        self.assertTrue(matches_recovered_timeout(event, "pointer-api", 17))

    def test_rejects_timeout_for_different_interrupted_operation(self):
        events = (
            parse_event(
                "DIAG protocol=1 seq=18 stage=pointer-api event=TIMEOUT "
                "reset=watchdog phase=write-tx"
            ),
            parse_event(
                "DIAG protocol=1 seq=17 stage=pointer-burst event=TIMEOUT "
                "reset=watchdog phase=write-tx"
            ),
            parse_event(
                "DIAG protocol=1 seq=17 stage=pointer-api event=TIMEOUT "
                "reset=requested phase=write-tx"
            ),
        )
        for event in events:
            with self.subTest(event=event):
                self.assertFalse(
                    matches_recovered_timeout(event, "pointer-api", 17)
                )

    def test_recovery_boot_does_not_regress_interrupted_sequence(self):
        start = parse_event(
            "DIAG protocol=1 seq=17 stage=pointer-api event=START"
        )
        boot = parse_event(PROVENANCE_EVENT)
        timeout = parse_event(
            "DIAG protocol=1 seq=17 stage=pointer-api event=TIMEOUT "
            "reset=watchdog phase=write-tx"
        )

        previous = update_sequence(None, start)
        previous = update_sequence(previous, boot)
        previous = update_sequence(previous, timeout)

        self.assertEqual(previous, 17)

    def test_rescan_selects_only_matching_usb_identity(self):
        with TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            class_tty = root / "sys" / "class" / "tty"
            devices = root / "sys" / "devices"
            dev_root = root / "dev"
            class_tty.mkdir(parents=True)
            dev_root.mkdir()
            self._add_usb_tty(devices, class_tty, "ttyACM0", "1234", "4021")
            self._add_usb_tty(devices, class_tty, "ttyACM1", "6666", "4021")

            found = find_diagnostic_device(class_tty, dev_root)

            self.assertEqual(found, str(dev_root / "ttyACM1"))

    def test_rescan_returns_none_without_matching_usb_identity(self):
        with TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            class_tty = root / "sys" / "class" / "tty"
            devices = root / "sys" / "devices"
            dev_root = root / "dev"
            class_tty.mkdir(parents=True)
            dev_root.mkdir()
            self._add_usb_tty(devices, class_tty, "ttyACM0", "6666", "9999")

            self.assertIsNone(find_diagnostic_device(class_tty, dev_root))

    @staticmethod
    def _add_usb_tty(devices, class_tty, name, vendor, product):
        usb_device = devices / name
        tty_device = usb_device / "interface" / "tty" / name
        tty_device.mkdir(parents=True)
        (usb_device / "idVendor").write_text(vendor, encoding="ascii")
        (usb_device / "idProduct").write_text(product, encoding="ascii")
        (class_tty / name).symlink_to(tty_device, target_is_directory=True)


class ControllerHelperTests(unittest.TestCase):
    def test_serial_buffer_frames_partial_crlf_lines(self):
        line_buffer = SerialLineBuffer()

        first = line_buffer.feed(
            b"DIAG protocol=1 seq=1 stage=shell event=STATUS\r\nDIAG prot"
        )
        second = line_buffer.feed(
            b"ocol=1 seq=2 stage=pointer-api event=START\n"
        )

        self.assertEqual(
            first, ["DIAG protocol=1 seq=1 stage=shell event=STATUS"]
        )
        self.assertEqual(
            second,
            ["DIAG protocol=1 seq=2 stage=pointer-api event=START"],
        )

    def test_serial_buffer_accepts_exact_provenance_event_without_truncation(self):
        line_buffer = SerialLineBuffer()

        framed = line_buffer.feed(PROVENANCE_EVENT.encode("ascii") + b"\n")

        self.assertEqual(len(PROVENANCE_EVENT.encode("ascii")) + 1, 194)
        self.assertEqual(framed, [PROVENANCE_EVENT])

    def test_serial_buffer_rejects_oversized_line(self):
        line_buffer = SerialLineBuffer()
        with self.assertRaises(ValueError):
            line_buffer.feed(b"x" * 256)

    def test_serial_buffer_rejects_non_ascii_line(self):
        line_buffer = SerialLineBuffer()
        with self.assertRaises(ValueError):
            line_buffer.feed(b"DIAG \xff\n")

    def test_writes_utc_timestamped_transcript(self):
        transcript = StringIO()
        timestamp = datetime(2026, 7, 19, 12, 34, 56, 789000, timezone.utc)

        write_transcript(transcript, "CDC<", "DIAG protocol=1", timestamp)

        self.assertEqual(
            transcript.getvalue(),
            "[2026-07-19T12:34:56.789Z] CDC< DIAG protocol=1\n",
        )

    @mock.patch("diag_host.socket.socket")
    def test_binds_nonblocking_udp_to_explicit_listener(self, socket_constructor):
        udp_socket = socket_constructor.return_value

        created = create_udp_socket("192.168.2.34", 49000)

        self.assertIs(created, udp_socket)
        socket_constructor.assert_called_once_with(socket.AF_INET, socket.SOCK_DGRAM)
        udp_socket.setblocking.assert_called_once_with(False)
        udp_socket.bind.assert_called_once_with(("192.168.2.34", 49000))

    @mock.patch("diag_host.socket.socket")
    def test_closes_udp_socket_when_bind_fails(self, socket_constructor):
        udp_socket = socket_constructor.return_value
        udp_socket.bind.side_effect = OSError("address unavailable")

        with self.assertRaises(OSError):
            create_udp_socket("192.168.2.34", 49000)

        udp_socket.close.assert_called_once_with()


class ControllerRegistrationTests(unittest.TestCase):
    def test_reconnect_registration_failure_closes_descriptor_and_retries(self):
        operations = []

        class RegistrationFailingSelector:
            def __init__(self):
                self.registered = {}
                self.initial_masks = iter(
                    (
                        selectors.EVENT_WRITE,
                        selectors.EVENT_READ,
                        selectors.EVENT_WRITE,
                        selectors.EVENT_READ,
                        selectors.EVENT_READ,
                    )
                )

            def register(self, fileobj, events, data):
                operations.append(("register", fileobj))
                if fileobj == 20:
                    raise OSError("registration failed")
                self.registered[fileobj] = (events, data)

            def modify(self, fileobj, events, data):
                self.registered[fileobj] = (events, data)

            def unregister(self, fileobj):
                if fileobj not in self.registered:
                    raise KeyError(fileobj)
                del self.registered[fileobj]

            def select(self, _timeout):
                if 10 in self.registered:
                    mask = next(self.initial_masks)
                    return [(mock.Mock(fd=10, data="cdc"), mask)]
                if 30 in self.registered:
                    return [
                        (
                            mock.Mock(fd=30, data="cdc"),
                            selectors.EVENT_READ,
                        )
                    ]
                return []

            def close(self):
                pass

        selector = RegistrationFailingSelector()
        descriptors = iter((10, 20, 30))

        def open_cdc_side_effect(_path):
            descriptor = next(descriptors)
            operations.append(("open", descriptor))
            return descriptor

        reads = {
            10: iter(
                (
                    b"DIAG protocol=1 seq=1 stage=shell event=STATUS\n",
                    b"DIAG protocol=1 seq=2 stage=pointer-api event=START\n",
                    OSError("device disconnected"),
                )
            ),
            30: iter(
                (
                    (
                        PROVENANCE_EVENT
                        + "\nDIAG protocol=1 seq=2 stage=pointer-api "
                        "event=TIMEOUT reset=watchdog phase=write-tx\n"
                    ).encode("ascii"),
                )
            ),
        }

        def read_side_effect(descriptor, _size):
            result = next(reads[descriptor])
            if isinstance(result, BaseException):
                raise result
            return result

        def close_side_effect(descriptor):
            operations.append(("close", descriptor))

        monotonic_time = [-0.11]

        def monotonic_side_effect():
            monotonic_time[0] += 0.11
            return monotonic_time[0]

        arguments = parse_args(
            ["--device", "/dev/ttyACM0", "run", "pointer-api"]
        )
        with (
            mock.patch(
                "diag_host.selectors.DefaultSelector", return_value=selector
            ),
            mock.patch("diag_host.open_cdc", side_effect=open_cdc_side_effect),
            mock.patch(
                "diag_host.find_diagnostic_device",
                return_value="/dev/ttyACM0",
            ),
            mock.patch("diag_host.os.read", side_effect=read_side_effect),
            mock.patch("diag_host.os.write", side_effect=lambda _fd, data: len(data)),
            mock.patch("diag_host.os.close", side_effect=close_side_effect),
            mock.patch(
                "diag_host.time.monotonic", side_effect=monotonic_side_effect
            ),
        ):
            exit_code = run_controller(arguments, transcript=StringIO())

        self.assertEqual(exit_code, 3)
        self.assertIn(("open", 30), operations)
        self.assertIn(("close", 20), operations)
        self.assertLess(
            operations.index(("close", 20)), operations.index(("open", 30))
        )


class ControllerIntegrationTests(unittest.TestCase):
    def test_autonomous_barrier_fail_is_transcripted_without_poisoning_command(self):
        master, slave = pty.openpty()
        transcript = StringIO()
        arguments = parse_args(
            ["--device", os.ttyname(slave), "run", "pointer-api"]
        )

        def simulate_firmware():
            reader = CommandReader(master)
            self.assertEqual(reader.read(), "status")
            self._send_event(
                master,
                "DIAG protocol=1 seq=1 stage=callback-layout event=START",
            )
            self._send_event(
                master,
                "DIAG protocol=1 seq=1 stage=callback-layout event=FAIL "
                "code=no-status-api",
            )
            self._send_event(
                master, "DIAG protocol=1 seq=2 stage=shell event=STATUS"
            )
            self.assertEqual(reader.read(), "run pointer-api")
            self._send_event(
                master,
                "DIAG protocol=1 seq=3 stage=pointer-api event=START",
            )
            self._send_event(
                master,
                "DIAG protocol=1 seq=3 stage=pointer-api event=PASS",
            )
            self.assertEqual(reader.read(), "status")
            self._send_event(
                master, "DIAG protocol=1 seq=4 stage=shell event=STATUS"
            )

        try:
            exit_code = self._run_with_simulator(
                arguments, transcript, simulate_firmware
            )
        finally:
            os.close(master)
            os.close(slave)

        self.assertEqual(exit_code, 0)
        fail_records = [
            line
            for line in transcript.getvalue().splitlines()
            if "stage=callback-layout event=FAIL code=no-status-api" in line
        ]
        self.assertEqual(len(fail_records), 1)
        self.assertRegex(
            fail_records[0],
            r"^\[\d{4}-\d{2}-\d{2}T.*Z\] CDC< DIAG protocol=1 ",
        )

    def test_rejects_recovered_timeout_without_observed_interruption(self):
        master, slave = pty.openpty()
        transcript = StringIO()
        arguments = parse_args(["--device", os.ttyname(slave), "status"])

        def simulate_unmatched_recovery():
            reader = CommandReader(master)
            self.assertEqual(reader.read(), "status")
            self._send_event(master, PROVENANCE_EVENT)
            self._send_event(
                master,
                "DIAG protocol=1 seq=17 stage=pointer-api event=TIMEOUT "
                "reset=watchdog phase=write-tx",
            )
            self._send_event(
                master, "DIAG protocol=1 seq=18 stage=shell event=STATUS"
            )

        try:
            exit_code = self._run_with_simulator(
                arguments, transcript, simulate_unmatched_recovery
            )
        finally:
            os.close(master)
            os.close(slave)

        self.assertEqual(exit_code, 4)

    def test_runs_command_to_completion_over_cdc_selector(self):
        master, slave = pty.openpty()
        transcript = StringIO()
        arguments = parse_args(
            ["--device", os.ttyname(slave), "run", "pointer-api"]
        )

        def simulate_firmware():
            reader = CommandReader(master)
            self.assertEqual(reader.read(), "status")
            self._send_event(master, PROVENANCE_EVENT)
            self._send_event(
                master, "DIAG protocol=1 seq=1 stage=shell event=STATUS"
            )
            self.assertEqual(reader.read(), "run pointer-api")
            self._send_event(
                master,
                "DIAG protocol=1 seq=2 stage=pointer-api event=START",
            )
            self._send_event(
                master,
                "DIAG protocol=1 seq=2 stage=pointer-api event=PASS",
            )
            self.assertEqual(reader.read(), "status")
            self._send_event(
                master, "DIAG protocol=1 seq=3 stage=shell event=STATUS"
            )

        try:
            exit_code = self._run_with_simulator(
                arguments, transcript, simulate_firmware
            )
        finally:
            os.close(master)
            os.close(slave)

        self.assertEqual(exit_code, 0)
        self.assertIn(
            "CDC< DIAG protocol=1 seq=2 stage=pointer-api event=PASS",
            transcript.getvalue(),
        )

    def test_multiplexes_udp_echo_while_run_all_is_active(self):
        master, slave = pty.openpty()
        transcript = StringIO()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as port_probe:
            port_probe.bind(("127.0.0.1", 0))
            listen_port = port_probe.getsockname()[1]
        arguments = parse_args(
            [
                "--device",
                os.ttyname(slave),
                "--device-ip",
                "127.0.0.2",
                "--subnet",
                "255.0.0.0",
                "--gateway",
                "127.0.0.1",
                "--listen-ip",
                "127.0.0.1",
                "--listen-port",
                str(listen_port),
                "run-all",
            ]
        )

        def simulate_firmware():
            reader = CommandReader(master)
            self.assertEqual(reader.read(), "status")
            self._send_event(
                master, "DIAG protocol=1 seq=1 stage=shell event=STATUS"
            )
            self.assertEqual(
                reader.read(),
                f"net 127.0.0.2 255.0.0.0 127.0.0.1 127.0.0.1 {listen_port}",
            )
            self._send_event(
                master, "DIAG protocol=1 seq=2 stage=shell event=NET"
            )
            self.assertEqual(reader.read(), "run all")
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                sender.settimeout(0.2)
                for sequence, stage in enumerate(RUN_ALL_STAGES, start=3):
                    self._send_event(
                        master,
                        f"DIAG protocol=1 seq={sequence} stage={stage} event=START",
                    )
                    if stage == "udp":
                        sender.sendto(b"not-diagnostic", ("127.0.0.1", listen_port))
                        with self.assertRaises(socket.timeout):
                            sender.recvfrom(65535)
                        packet = bytes.fromhex("44 55 50 31") + bytes(range(28))
                        sender.sendto(packet, ("127.0.0.1", listen_port))
                        echoed, _ = sender.recvfrom(65535)
                        self.assertEqual(echoed, packet)
                    self._send_event(
                        master,
                        f"DIAG protocol=1 seq={sequence} stage={stage} event=PASS",
                    )
            self.assertEqual(reader.read(), "status")
            self._send_event(
                master, "DIAG protocol=1 seq=17 stage=shell event=STATUS"
            )

        try:
            exit_code = self._run_with_simulator(
                arguments, transcript, simulate_firmware
            )
        finally:
            os.close(master)
            os.close(slave)

        self.assertEqual(exit_code, 0)

    def test_reconnects_to_matching_device_and_requires_recovered_timeout(self):
        first_master, first_slave = pty.openpty()
        second_master, second_slave = pty.openpty()
        transcript = StringIO()
        with TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            class_tty = root / "sys" / "class" / "tty"
            devices = root / "sys" / "devices"
            dev_root = root / "dev"
            class_tty.mkdir(parents=True)
            dev_root.mkdir()
            ReconnectTests._add_usb_tty(
                devices, class_tty, "ttyACM0", "6666", "4021"
            )
            (dev_root / "ttyACM0").symlink_to(os.ttyname(second_slave))
            arguments = parse_args(
                ["--device", os.ttyname(first_slave), "run", "pointer-api"]
            )

            def simulate_watchdog_reset():
                reader = CommandReader(first_master)
                self.assertEqual(reader.read(), "status")
                self._send_event(
                    first_master,
                    "DIAG protocol=1 seq=1 stage=shell event=STATUS",
                )
                self.assertEqual(reader.read(), "run pointer-api")
                self._send_event(
                    first_master,
                    "DIAG protocol=1 seq=2 stage=pointer-api event=START",
                )
                deadline = time.monotonic() + 1.0
                while "event=START" not in transcript.getvalue():
                    if time.monotonic() >= deadline:
                        raise TimeoutError("host did not observe interrupted START")
                    time.sleep(0.001)
                os.close(first_master)
                self._send_event(second_master, PROVENANCE_EVENT)
                self._send_event(
                    second_master,
                    "DIAG protocol=1 seq=2 stage=pointer-api event=TIMEOUT "
                    "reset=watchdog phase=write-tx",
                )

            try:
                exit_code = self._run_with_simulator(
                    arguments,
                    transcript,
                    simulate_watchdog_reset,
                    sys_class_tty=class_tty,
                    dev_root=dev_root,
                )
            finally:
                for descriptor in (
                    first_master,
                    first_slave,
                    second_master,
                    second_slave,
                ):
                    try:
                        os.close(descriptor)
                    except OSError:
                        pass

        self.assertEqual(exit_code, 3, transcript.getvalue())

    def test_rejects_recovered_timeout_before_provenance_boot_event(self):
        first_master, first_slave = pty.openpty()
        second_master, second_slave = pty.openpty()
        transcript = StringIO()
        with TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            class_tty = root / "sys" / "class" / "tty"
            devices = root / "sys" / "devices"
            dev_root = root / "dev"
            class_tty.mkdir(parents=True)
            dev_root.mkdir()
            ReconnectTests._add_usb_tty(
                devices, class_tty, "ttyACM0", "6666", "4021"
            )
            (dev_root / "ttyACM0").symlink_to(os.ttyname(second_slave))
            arguments = parse_args(
                ["--device", os.ttyname(first_slave), "run", "pointer-api"]
            )

            def simulate_watchdog_reset_without_boot():
                reader = CommandReader(first_master)
                self.assertEqual(reader.read(), "status")
                self._send_event(
                    first_master,
                    "DIAG protocol=1 seq=1 stage=shell event=STATUS",
                )
                self.assertEqual(reader.read(), "run pointer-api")
                self._send_event(
                    first_master,
                    "DIAG protocol=1 seq=2 stage=pointer-api event=START",
                )
                deadline = time.monotonic() + 1.0
                while "event=START" not in transcript.getvalue():
                    if time.monotonic() >= deadline:
                        raise TimeoutError("host did not observe interrupted START")
                    time.sleep(0.001)
                os.close(first_master)
                self._send_event(
                    second_master,
                    "DIAG protocol=1 seq=2 stage=pointer-api event=TIMEOUT "
                    "reset=watchdog phase=write-tx",
                )

            try:
                exit_code = self._run_with_simulator(
                    arguments,
                    transcript,
                    simulate_watchdog_reset_without_boot,
                    sys_class_tty=class_tty,
                    dev_root=dev_root,
                )
            finally:
                for descriptor in (
                    first_master,
                    first_slave,
                    second_master,
                    second_slave,
                ):
                    try:
                        os.close(descriptor)
                    except OSError:
                        pass

        self.assertEqual(exit_code, 4, transcript.getvalue())

    def _run_with_simulator(
        self, arguments, transcript, simulator, **controller_options
    ):
        errors = queue.Queue()

        def run_simulator():
            try:
                simulator()
            except BaseException as error:
                errors.put(error)

        simulator_thread = threading.Thread(target=run_simulator, daemon=True)
        simulator_thread.start()
        previous_handler = signal.getsignal(signal.SIGALRM)

        def timeout_handler(_signal_number, _frame):
            raise TimeoutError("controller integration test timed out")

        signal.signal(signal.SIGALRM, timeout_handler)
        signal.setitimer(signal.ITIMER_REAL, 5.0)
        try:
            result = run_controller(
                arguments, transcript=transcript, **controller_options
            )
        finally:
            signal.setitimer(signal.ITIMER_REAL, 0.0)
            signal.signal(signal.SIGALRM, previous_handler)
        simulator_thread.join(timeout=1.0)
        if simulator_thread.is_alive():
            self.fail("firmware simulator did not finish")
        if not errors.empty():
            raise errors.get()
        return result

    @staticmethod
    def _send_event(descriptor, line):
        os.write(descriptor, f"{line}\n".encode("ascii"))


if __name__ == "__main__":
    unittest.main()
