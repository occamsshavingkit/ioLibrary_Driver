# W5500 DHCP RX Pointer Probe

## Goal

Verify on the RP2040/W5500 hardware that an inbound UDP datagram reaches the
W5500 through the router-assigned DHCP network path and that completing the
receive flow advances `Sn_RX_RD`.

## Scope

- Extend only the active lean probe at
  `tests/hardware/rp2040_w5500_probe/src/main.c`.
- Obtain the W5500 network configuration through DHCP.
- Print the lease address over USB CDC.
- Open UDP socket 0 on port 49002 and wait for one datagram from the Pi.
- Validate a fixed one-byte payload, complete the receive operation, and print
  the before/after RX pointer values.
- Use a one-shot Pi UDP send command after the lease address is printed.

## Non-Goals

- No DHCP or UDP host controller in the repository.
- No persistent server, command shell, watchdog, retry framework, or packet
  echo behavior.
- No W5500 driver changes.

## Flow

1. The probe initializes transport and W5500 memory, then runs DHCP using a
   bounded lease timeout.
2. On success it prints `PROBE dhcp ip=<address>` and opens UDP port 49002.
3. The Pi sends a fixed one-byte datagram to the reported address and port.
4. The probe records `Sn_RX_RD`, receives and validates the datagram, then
   records `Sn_RX_RD` again after the receive command completes.
5. The probe reports success only when the payload is `0xA5` and `Sn_RX_RD`
   advances by nine bytes: the eight-byte W5500 UDP receive header plus the
   one-byte payload consumed by `recvfrom`.
6. The probe closes the socket and remains idle for CDC inspection.

## Failure Reporting

The probe prints a single `PROBE` failure record for DHCP timeout, UDP socket
open failure, receive timeout, payload mismatch, or unexpected RX pointer
delta. It closes the socket before entering its idle loop.

## Verification

- Build the RP2040 firmware on the Pi.
- Flash it in BOOTSEL mode and capture the USB CDC transcript.
- Send the fixed datagram to the printed DHCP lease address from the Pi.
- Confirm the transcript records the lease, received payload, and expected RX
  pointer delta.
