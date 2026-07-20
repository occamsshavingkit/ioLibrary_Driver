# RP2040 W5500 Pointer Probe

This is the active hardware debug tool. It acquires a DHCP lease, opens UDP
port 49002, receives one `0xA5` byte, and verifies that the W5500 RX read
pointer advances by the eight-byte UDP header plus the payload.

Build on the Pi:

```bash
cmake -S tests/hardware/rp2040_w5500_probe \
  -B /tmp/rp2040-w5500-probe-build \
  -DPICO_SDK_PATH=/usr/share/pico-sdk \
  -DWIZNET_PICO_C_PATH=/usr/share/wiznet-pico-c \
  -DIOLIBRARY_ROOT=/path/to/W5500
cmake --build /tmp/rp2040-w5500-probe-build --target rp2040_w5500_probe
```

Flash the resulting UF2 in BOOTSEL mode with:

```bash
picotool load -v -x /tmp/rp2040-w5500-probe-build/rp2040_w5500_probe.uf2
```

Open `/dev/ttyACM0` before reset. The probe begins after USB CDC connects and
prints `PROBE` records once. After it reports the lease and receive readiness,
replace `DEVICE_IP` with the reported address and send the packet from the Pi:

```bash
python3 -c 'import socket; socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(bytes([0xA5]), ("DEVICE_IP", 49002))'
```

The successful DHCP/RX portion of the transcript is:

```text
PROBE dhcp ip=A.B.C.D
PROBE recv_ready port=49002
PROBE rx_rd before=.... after=.... expected_delta=0009
```
