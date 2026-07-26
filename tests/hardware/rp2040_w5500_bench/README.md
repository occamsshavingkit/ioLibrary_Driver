# RP2040 W5500 Hardware Benchmark

This is the active hardware benchmark. It runs the W5500 benchmark over USB
CDC and reports the measured results for collection on the host.

Build on the Pi:

```bash
cmake -S tests/hardware/rp2040_w5500_bench \
  -B /tmp/rp2040-w5500-bench-build \
  -DPICO_SDK_PATH=/usr/share/pico-sdk \
  -DWIZNET_PICO_C_PATH=/usr/share/wiznet-pico-c \
  -DIOLIBRARY_ROOT=/path/to/W5500
cmake --build /tmp/rp2040-w5500-bench-build --target rp2040_w5500_bench
```

Flash the resulting UF2 in BOOTSEL mode with:

```bash
picotool load -v -x /tmp/rp2040-w5500-bench-build/rp2040_w5500_bench.uf2
```

Open `/dev/ttyACM0` before reset and capture the benchmark's USB CDC output.
