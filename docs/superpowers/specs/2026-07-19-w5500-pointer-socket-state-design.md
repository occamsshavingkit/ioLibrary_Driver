# W5500 Pointer Socket-State Experiment

## Context

On physical hardware, transport initialization, reset, VERSIONR, memory
initialization, PHY link, and ordinary register reads and writes pass. The
pointer sequential, burst, and public-API paths all write `0x1234` to
`Sn_TX_WR(0)` but read back `0x0000`.

The sequential path uses direct `WIZCHIP_WRITE` calls and the burst path uses
`WIZCHIP_WRITE_BUF`; therefore the identical failure does not isolate the
public pointer setter macro as its cause. All three tests currently operate
while socket 0 is closed.

## Goal

Determine whether pointer writes are retained when socket 0 is open, without
requiring network configuration or changing later network-stage prerequisites.

## Design

`run_pointer_stage` will own an isolated socket lifecycle:

1. Open socket 0 as UDP on a diagnostic-only local port.
2. Require `SOCK_UDP` before reading or writing the pointer registers.
3. Run the existing sequential, burst, or public-API pointer operation without
   changing its write or readback implementation.
4. Restore both pointers.
5. Close socket 0 on both success and failure paths, then require `SOCK_CLOSED`.

The stage will retain watchdog coverage around open, status, pointer, and close
operations. It will report an open, socket-state, pointer-readback, restore, or
close failure precisely. A pointer readback failure remains the stage result
after cleanup succeeds.

## Scope

- Modify the diagnostic pointer-stage lifecycle only.
- Add focused host-side coverage for the new open and cleanup expectations.
- Build with `DIAG_EXPECT_SPI_STATUS=OFF` because this driver deliberately
  does not expose the optional SPI-status API.
- Flash through BOOTSEL and run the three pointer stages on the RP2040/W5500.

## Non-Goals

- Do not configure static IP, transmit packets, or run DHCP as part of this
  experiment.
- Do not change the W5500 pointer setter implementation.
- Do not reorder the public diagnostic stage catalog.

## Acceptance Criteria

- A pointer stage opens and later closes socket 0 without leaving it open.
- The stage reports whether pointer writes read back correctly with `SOCK_UDP`.
- Existing host C and Python tests pass.
- The RP2040 firmware cross-build succeeds.
- The on-device transcript records the exact terminal result for all three
  pointer modes.
