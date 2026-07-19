# Task 5 Report: W5500 PIO Transport And Bring-Up

## Status

Implemented and verified against base commit
`20b368aff7bc91fd86ce96476c7197580a5321c8` in the isolated worktree
`/home/quackdcs/W5500/.worktrees/rp2040-w5500-diagnostic`.

The implementation commit is the Task 5 commit containing this report; resolve
its immutable SHA with `git rev-parse HEAD`. The exact SHA is also returned to
the controller after commit creation.

The Task 5 implementer did not flash firmware during implementation; HIL
remained controller-owned. The controller later appended the HIL section at the
end of this report after the implementer handoff.

This report and its artifact predate Task 9 generated provenance. The preserved
artifact and transcript are therefore provenance-unverified and cannot satisfy
the final provenance acceptance contract. The commands below remain a
historical record; use `tests/hardware/rp2040_w5500_diag/README.md` for the
current scoped, provenance-bearing build workflow.

## RED Evidence

The first firmware edit added a typed handler table in `main.c` referencing all
six required stage functions. The isolated worktree was then synchronized with
the following historical pre-Task 9 command:

```sh
rsync -a --delete --exclude=.git/ /home/quackdcs/W5500/.worktrees/rp2040-w5500-diagnostic/ root@192.168.2.34:/tmp/ioLibrary-driver-diag/
ssh root@192.168.2.34 "cmake --build /root/w5500-diag-build --target rp2040_w5500_diag --parallel 4"
```

The link failed only on the six intended missing implementations:

```text
/usr/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/bin/ld: CMakeFiles/rp2040_w5500_diag.dir/src/main.c.o:(.rodata.stage_handlers+0x0): undefined reference to `diag_stage_callback_layout'
/usr/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/bin/ld: CMakeFiles/rp2040_w5500_diag.dir/src/main.c.o:(.rodata.stage_handlers+0x4): undefined reference to `diag_stage_transport_init'
/usr/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/bin/ld: CMakeFiles/rp2040_w5500_diag.dir/src/main.c.o:(.rodata.stage_handlers+0x8): undefined reference to `diag_stage_chip_reset'
/usr/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/bin/ld: CMakeFiles/rp2040_w5500_diag.dir/src/main.c.o:(.rodata.stage_handlers+0xc): undefined reference to `diag_stage_version'
/usr/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/bin/ld: CMakeFiles/rp2040_w5500_diag.dir/src/main.c.o:(.rodata.stage_handlers+0x10): undefined reference to `diag_stage_memory_init'
/usr/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/bin/ld: CMakeFiles/rp2040_w5500_diag.dir/src/main.c.o:(.rodata.stage_handlers+0x14): undefined reference to `diag_stage_phy_link'
collect2: error: ld returned 1 exit status
```

The status-contract host test was then added before its implementation. Its
first syntactically valid build failed only at link time:

```text
/usr/bin/ld: test_w5500_diag_status_contract.c:(.text+0x145): undefined reference to `w5500_diag_status_contract_classify'
/usr/bin/ld: test_w5500_diag_status_contract.c:(.text+0x1b2): undefined reference to `w5500_diag_status_contract_classify'
/usr/bin/ld: test_w5500_diag_status_contract.c:(.text+0x25c): undefined reference to `w5500_diag_status_contract_classify'
collect2: error: ld returned 1 exit status
```

## Implementation

- Added the production status-contract classifier and a host test covering
  absent/expected, absent/not-claimed, aliased registration, exact restoration
  of all four typed callbacks, and acceptance of a future independent layout.
- Added the weak status registrar exactly as specified and never calls it when
  its address is null.
- Added the idempotent W55RP20 PIO board boundary with pins 20 through 25, the
  required clock-divisor macros, one non-recursive critical section, CS-high
  idle state, and byte plus burst callback registration.
- Added direct GPIO25 reset timing and six watchdog-bounded bring-up stages.
- Added typed command dispatch for `run`, `run all`, and `repeat` without the
  former shell/stage double sequence increment.
- Added one-shot automatic smoke after BOOT. `callback-layout` is the first
  handler; its failure returns from `run_all`, so `transport-init` cannot open
  PIO and no WIZCHIP transfer can occur.
- Set the system clock to 133 MHz before generic board, TinyUSB, or PIO setup.
- Isolated installed transport diagnostics from project warning policy; all
  project diagnostic sources remain C11 with
  `-Wall -Wextra -Werror -pedantic`.

Files added or changed:

```text
.superpowers/sdd/task-5-report.md
tests/hardware/rp2040_w5500_diag/CMakeLists.txt
tests/hardware/rp2040_w5500_diag/src/main.c
tests/hardware/rp2040_w5500_diag/src/diag_w5500_stages.c
tests/hardware/rp2040_w5500_diag/src/diag_w5500_stages.h
tests/hardware/rp2040_w5500_diag/src/w5500_diag_board.c
tests/hardware/rp2040_w5500_diag/src/w5500_diag_board.h
tests/hardware/rp2040_w5500_diag/src/w5500_diag_status_contract.c
tests/hardware/rp2040_w5500_diag/src/w5500_diag_status_contract.h
tests/hardware/rp2040_w5500_diag/tests/test_w5500_diag_status_contract.c
```

## Host Verification

Exact commands:

```sh
cmake -S tests/hardware/rp2040_w5500_diag -B /tmp/opencode/w5500-diag-host -DDIAG_BUILD_FIRMWARE=OFF
cmake --build /tmp/opencode/w5500-diag-host
ctest --test-dir /tmp/opencode/w5500-diag-host --output-on-failure
git diff --check
```

Final output:

```text
-- Configuring done
-- Generating done
-- Build files have been written to: /tmp/opencode/w5500-diag-host
[ 21%] Built target test_diag_protocol
[ 42%] Built target test_diag_runner
[ 64%] Built target test_diag_journal
[ 78%] Built target test_diag_usb_identity
[100%] Built target test_w5500_diag_status_contract
Test project /tmp/opencode/w5500-diag-host
    Start 1: diag_protocol
1/5 Test #1: diag_protocol ....................   Passed
    Start 2: diag_runner
2/5 Test #2: diag_runner ......................   Passed
    Start 3: diag_journal
3/5 Test #3: diag_journal .....................   Passed
    Start 4: diag_usb_identity
4/5 Test #4: diag_usb_identity ................   Passed
    Start 5: w5500_diag_status_contract
5/5 Test #5: w5500_diag_status_contract .......   Passed
100% tests passed, 0 tests failed out of 5
```

`git diff --check` produced no output and exited zero.

## Firmware Verification

Only this worktree was synchronized by the historical pre-Task 9 workflow:

```sh
rsync -a --delete --exclude=.git/ /home/quackdcs/W5500/.worktrees/rp2040-w5500-diagnostic/ root@192.168.2.34:/tmp/ioLibrary-driver-diag/
```

Historical configure command, before the four Task 9 provenance variables
became mandatory:

```sh
ssh root@192.168.2.34 "cmake -S /tmp/ioLibrary-driver-diag/tests/hardware/rp2040_w5500_diag -B /root/w5500-diag-build -DDIAG_BUILD_FIRMWARE=ON -DDIAG_EXPECT_SPI_STATUS=1 -DPICO_SDK_PATH=/usr/share/pico-sdk -DWIZNET_PICO_C_PATH=/usr/share/wiznet-pico-c -DIOLIBRARY_ROOT=/tmp/ioLibrary-driver-diag"
```

Output:

```text
PICO_SDK_PATH is /usr/share/pico-sdk
Target board (PICO_BOARD) is 'pico'.
Using board configuration from /usr/share/pico-sdk/src/boards/include/boards/pico.h
Pico Platform (PICO_PLATFORM) is 'rp2040'.
Build type is Release
TinyUSB available at /usr/share/pico-sdk/lib/tinyusb/hw/bsp/rp2040; enabling build support for USB.
BTstack available at /usr/share/pico-sdk/lib/btstack
cyw43-driver available at /usr/share/pico-sdk/lib/cyw43-driver
mbedtls available at /usr/share/pico-sdk/lib/mbedtls
lwIP available at /usr/share/pico-sdk/lib/lwip
C library type is newlib
Using picotool from /usr/bin/picotool
-- Configuring done
-- Generating done
-- Build files have been written to: /root/w5500-diag-build
```

Exact required build command:

```sh
ssh root@192.168.2.34 "cmake --build /root/w5500-diag-build --target rp2040_w5500_diag --parallel 4"
```

Final output:

```text
[  1%] Built target diag_wiznet_transport_wizchip_qspi_pio_pio_h
[  1%] Built target bs2_default
[  4%] Built target diag_iolibrary
[  6%] Built target bs2_default_library
[ 44%] Built target diag_wiznet_transport
[100%] Built target rp2040_w5500_diag
```

`arm-none-eabi-nm -u /root/w5500-diag-build/rp2040_w5500_diag.elf`
produced no output. This proves that no unresolved strong symbol blocked the
link; it does not prove that the weak registrar was absent. The runtime
`callback-layout FAIL code=no-status-api` event is the evidence that the weak
registrar resolved to null in this artifact.

Source path origin was explicit in CMake even though generated build provenance
did not exist yet:

- ioLibrary: `/tmp/ioLibrary-driver-diag/Ethernet` and
  `/tmp/ioLibrary-driver-diag/Internet/DHCP` only.
- PIO transport: `/usr/share/wiznet-pico-c/port/ioLibrary_Driver/src/wizchip_qspi_pio.c`
  and its `.pio` program only.
- No `/root/firmware` or installed vendored Ethernet source is referenced.

## Artifact Identity

This is the provenance-unverified, pre-Task 9 artifact described above. Its
identity remains useful as historical Task 5 evidence only.

```text
Path: /root/w5500-diag-build/rp2040_w5500_diag.uf2
Size: 146432 bytes
SHA-256: 1f1ae57654ec8bfd0c4e374bd2653a484f9a28fb734635ceea3eda0940c1e9be
Family: rp2040
Program name: rp2040_w5500_diag
Binary start: 0x10000000
Binary end: 0x10011d14
SDK version: 2.3.0
Pico board: pico
Boot2: boot2_w25q080
Build: Release, Jul 19 2026
```

Offline identity command:

```sh
picotool info -a /root/w5500-diag-build/rp2040_w5500_diag.uf2
```

## Review And Concerns

The implementation was reviewed directly against every binding point in the
Task 5 brief. Two independent reviewer invocations timed out without returning
findings. No known implementation concern remains.

Expected and intentional HIL result on the current audited driver revision:

```text
DIAG protocol=1 seq=1 stage=callback-layout event=START timeout_ms=0
DIAG protocol=1 seq=1 stage=callback-layout event=FAIL code=no-status-api
```

The Task 5 implementer intentionally did not perform HIL during implementation.
The controller-owned verification appended below happened later.

## Controller HIL Verification

This section was appended by the controller after the Task 5 implementation
handoff. It records later HIL of the legacy artifact; it does not claim that the
implementer flashed it or that Task 9 provenance metadata was present.

The controller placed the board in RP2 BOOTSEL mode and flashed the reviewed
artifact with verification enabled. The device re-enumerated as:

```text
Bus 001 Device 018: ID 6666:4021 occamsshavingkit RP2040 W5500 Diagnostic
```

A raw `os.open`/`termios` reader, matching the planned Task 8 controller rather
than adding a pyserial dependency, captured the fresh-boot transcript:

```text
DIAG protocol=1 seq=0 stage=system event=BOOT firmware=rp2040-w5500-diag recovery=false
DIAG protocol=1 seq=1 stage=callback-layout event=START timeout_ms=0
DIAG protocol=1 seq=1 stage=callback-layout event=FAIL code=no-status-api
```

The capture remained open for three seconds and received no `transport-init` or
later stage record. A manual `run transport-init` returned
`reason=prerequisite-blocked`; a manual `run callback-layout` reproduced the
same deterministic `no-status-api` failure.

During diagnosis of an incomplete pyserial display, Linux `usbmon` independently
confirmed BOOT and the complete automatic seq=1 START/FAIL transcript crossed
the CDC bulk-IN endpoint. The missing pyserial lines were a reader artifact,
not firmware loss. This satisfied the historical Task 5 hardware gate without
performing a W5500 transfer on the audited revision, but it is not final
provenance-bearing Task 9 acceptance evidence.
