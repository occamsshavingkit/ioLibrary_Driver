# Quickstart: Resolve Production Audit Findings

This guide is the release-validation path for the implementation described by
[`plan.md`](./plan.md). Run it from the repository root on branch
`005-fix-audit-findings` after the implementation tasks are complete. Every
command must return zero unless the step explicitly documents another expected
status. A skipped, synthetic, or hardware-unavailable result does not satisfy a
release gate.

This document names the validation interfaces the implementation must create.
Before implementation, a missing planned test file or Make/CMake target is an
expected red state; after implementation, every command below is mandatory.

## 1. Prerequisites

The validation host needs:

- GCC and Clang with C11, pthread, ASan, UBSan, and TSan support.
- CMake 3.20 or newer, Make, CTest, and Python 3.11 or newer.
- `cppcheck`, `clang-tidy`, Clang static analyzer, and CBMC.
- `arm-none-eabi-gcc` and Raspberry Pi Pico SDK 2.2.0.
- `picotool`, USB access to a W55RP20/RP2040 target with W5500, and a network
  peer capable of UDP echo and DHCP service.
- The coordinated WIZnet-PICO-C checkout at `./WIZnet-PICO-C`.

Keep generated files outside both Git worktrees:

```bash
export W5500_BUILD_ROOT="${W5500_BUILD_ROOT:-/tmp/opencode/w5500-005}"
export IOLIBRARY_ROOT="$PWD"
export WIZNET_PICO_C_PATH="$PWD/WIZnet-PICO-C"
export PICO_SDK_PATH="${PICO_SDK_PATH:?set PICO_SDK_PATH to Pico SDK 2.2.0}"
mkdir -p "$W5500_BUILD_ROOT" "$W5500_BUILD_ROOT/evidence"
```

Confirm the two source revisions before testing:

```bash
git status --short
git rev-parse HEAD
git -C WIZnet-PICO-C status --short
git -C WIZnet-PICO-C rev-parse HEAD
```

Record both commit IDs and both dirty states in
[`evidence.md`](./evidence.md), which T141 creates before the final T146
quickstart execution. Earlier gate runs retain the same provenance under the
external evidence directory until T141 assembles the matrix. Release evidence
requires clean candidate worktrees; generated build directories may remain
outside the source trees.

## 2. Root Host Verification

The root harness must compile the production files under `Ethernet/`; it must
not copy their logic into a test-only implementation. Clean between compiler
and sanitizer lanes so no object built with incompatible instrumentation is
reused.

```bash
make -C tests BUILD_DIR="$W5500_BUILD_ROOT/root-gcc" clean
make -C tests BUILD_DIR="$W5500_BUILD_ROOT/root-gcc" CC=gcc test

make -C tests BUILD_DIR="$W5500_BUILD_ROOT/root-clang" clean
make -C tests BUILD_DIR="$W5500_BUILD_ROOT/root-clang" CC=clang test

make -C tests BUILD_DIR="$W5500_BUILD_ROOT/root-sanitize" clean
make -C tests BUILD_DIR="$W5500_BUILD_ROOT/root-sanitize" CC=clang sanitize

make -C tests BUILD_DIR="$W5500_BUILD_ROOT/root-tsan" clean
make -C tests BUILD_DIR="$W5500_BUILD_ROOT/root-tsan" CC=clang tsan
```

Expected outcome: every correctness, fault-injection, raw-flag, PHY, socket
concurrency, and public-API test passes. The `sanitize` lane reports no ASan or
UBSan finding. The separately built `tsan` lane reports no data race or lock
order failure. The socket concurrency test completes 10,000 mixed operations
without deadlock, leaked lock ownership, or cross-socket state loss and has a
120-second outer watchdog. Intentional deadline-expiry cases use short injected
test deadlines and run separately from the successful-operation stress loop.

## 3. RP2040 Transport Host Verification

Configure the transport tests independently from firmware. These tests use the
Pico API fakes under `WIZnet-PICO-C/tests/fakes/` while linking the production
transport and GPIO sources.

```bash
cmake -S WIZnet-PICO-C/tests -B "$W5500_BUILD_ROOT/wiznet-pico-tests" \
  -DIOLIBRARY_ROOT="$PWD"
cmake --build "$W5500_BUILD_ROOT/wiznet-pico-tests" --parallel
ctest --test-dir "$W5500_BUILD_ROOT/wiznet-pico-tests" \
  --output-on-failure --no-tests=error
```

Expected outcome: transactional open/close, resource rollback, repeated
open/close, bounded DMA timeout, channel quarantine, transfer serialization,
sleep/wake/reset exclusion, raw GPIO acknowledgement, deferred dispatch,
registration-mask merge, and unregister cases all pass. No test may emulate
success by replacing the production lifecycle state machine.

## 4. Diagnostic Host Verification

Build and run the platform-neutral tests for the authoritative diagnostic and
its controller:

```bash
cmake -S tests/hardware/rp2040_w5500_diag_full \
  -B "$W5500_BUILD_ROOT/rp2040-w5500-diag-host"
cmake --build "$W5500_BUILD_ROOT/rp2040-w5500-diag-host" --parallel
ctest --test-dir "$W5500_BUILD_ROOT/rp2040-w5500-diag-host" \
  --output-on-failure --no-tests=error
python3 -m unittest discover \
  -s tests/hardware/rp2040_w5500_diag_full/host \
  -p 'test_*.py'
```

Expected outcome: all protocol, journal, network, watchdog, stage-runner,
status-contract, build-provenance, USB identity, and host-controller tests pass.

## 5. Static, Formal, Cross-Build, and Binary Gates

Run each analysis lane against production source and the production-linked
harnesses:

```bash
make -C tests BUILD_DIR="$W5500_BUILD_ROOT/analysis" static-analysis
make -C tests BUILD_DIR="$W5500_BUILD_ROOT/cbmc" cbmc
make -C tests BUILD_DIR="$W5500_BUILD_ROOT/cross" cross-compile
make -C tests BUILD_DIR="$W5500_BUILD_ROOT/binaries" verify-binaries
```

Expected outcome:

- `static-analysis` emits no actionable `cppcheck`, `clang-tidy`, or Clang
  analyzer finding in changed production or test code.
- `cbmc` proves the production-linked pointer-write, bounds, lock-cleanup, and
  deadline harness properties and reports zero failed properties.
- `cross-compile` builds the changed W5500 and W6300 shared-source paths with
  strict warnings and builds the RP2040 firmware targets with
  `arm-none-eabi-gcc`.
- `verify-binaries` confirms non-executable stacks and required hardening for
  host executables and reports no unexpected executable data section.

## 6. Build the Hardware Smoke Probe

Build the lean probe from the same candidate sources already exercised on the
host:

```bash
cmake -S tests/hardware/rp2040_w5500_probe \
  -B "$W5500_BUILD_ROOT/rp2040-w5500-probe" \
  -DPICO_SDK_PATH="$PICO_SDK_PATH" \
  -DIOLIBRARY_ROOT="$IOLIBRARY_ROOT" \
  -DWIZNET_PICO_C_PATH="$WIZNET_PICO_C_PATH"
cmake --build "$W5500_BUILD_ROOT/rp2040-w5500-probe" --parallel
```

Flash the `rp2040_w5500_probe.uf2` file under
`$W5500_BUILD_ROOT/rp2040-w5500-probe/` and capture its USB serial output.
Follow the setup and wiring instructions in
[`tests/hardware/rp2040_w5500_probe/README.md`](../../tests/hardware/rp2040_w5500_probe/README.md).

Expected outcome: the probe initializes the production transport, reads W5500
version `0x04`, obtains link within the configured deadline, completes three
independent address-assignment cycles, completes 100 UDP echo exchanges with
correct receive-pointer advancement, reports no sticky SPI error, and exits or
idles without a watchdog reset. Save the transcript path in `evidence.md`.

## 7. Build and Run the Authoritative Diagnostic

The full diagnostic owns hardware acceptance. Use the provenance-generation,
clean-worktree checks, flash procedure, USB-device selection, and network setup
in
[`tests/hardware/rp2040_w5500_diag_full/README.md`](../../tests/hardware/rp2040_w5500_diag_full/README.md).
That README is updated by this feature to use only the `_full` directory path.
It embeds both candidate revisions and the binary diff digest in the firmware;
do not substitute hand-entered provenance.

After flashing the clean candidate, run the complete catalog and required
repetitions from the network peer. Set `DIAG_DEVICE` only to the USB device
identified by the diagnostic's documented VID/PID and serial-number procedure.

```bash
export DIAG_DEVICE="${DIAG_DEVICE:?set DIAG_DEVICE from diagnostic USB discovery}"

python3 tests/hardware/rp2040_w5500_diag_full/host/diag_host.py \
  --device "$DIAG_DEVICE" \
  --device-ip 192.168.2.247 --subnet 255.255.255.0 \
  --gateway 192.168.2.1 --listen-ip 192.168.2.34 --listen-port 49000 \
  run-all

python3 tests/hardware/rp2040_w5500_diag_full/host/diag_host.py \
  --device "$DIAG_DEVICE" repeat pointer-api 100

python3 tests/hardware/rp2040_w5500_diag_full/host/diag_host.py \
  --device "$DIAG_DEVICE" \
  --device-ip 192.168.2.247 --subnet 255.255.255.0 \
  --gateway 192.168.2.1 --listen-ip 192.168.2.34 --listen-port 49000 \
  repeat udp 100

python3 tests/hardware/rp2040_w5500_diag_full/host/diag_host.py \
  --device "$DIAG_DEVICE" repeat dhcp 3
```

Expected outcome: every command returns zero. All catalog stages pass, all 100
pointer cycles preserve exact values, all 100 UDP exchanges preserve payload
and socket-pointer integrity, and all three DHCP runs complete independently.
The transcripts must also demonstrate bounded timeout/recovery behavior,
resource lifecycle checks, deferred GPIO dispatch, eight-socket independence,
maximum 16 KiB transfer behavior, and interrupt-latency acceptance defined in
[`contracts/verification-evidence.md`](./contracts/verification-evidence.md).
PHY evidence separately identifies the configured 200 microsecond engineering
hold and the normative hardware reset-pin minimum; it does not present the
engineering default as a datasheet requirement.

## 8. Generate and Validate Release Evidence

Populate `specs/005-fix-audit-findings/evidence.md` from observed command logs.
Use one row per audit ID and the exact states and fields defined in
[`contracts/verification-evidence.md`](./contracts/verification-evidence.md).
Then run the deterministic validator:

```bash
python3 tests/check_audit_evidence.py \
  specs/005-fix-audit-findings/evidence.md
```

Expected outcome: the validator reports `CUR-001` through `CUR-019` and
`AUD-001` through `AUD-073` exactly once, no unresolved release-blocking row,
valid root and transport provenance, existing non-empty evidence artifacts,
and a final `PASS` release verdict.

Only after this command passes may `AUDIT-RESOLVED.md`, the security review,
and related status documentation be updated from the recorded evidence.
