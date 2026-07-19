# RP2040 W5500 Pointer Probe

This is the active hardware debug tool. It prints raw W5500 pointer-register
results over USB CDC and does not run a command shell, network traffic, DHCP,
watchdog recovery, or host controller.

The probe compares direct sequential writes, direct burst writes, and the
public setter macros while socket 0 is closed and again after opening it as
UDP. It restores the pointers after each comparison and closes the socket.

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
prints `PROBE` records once.
