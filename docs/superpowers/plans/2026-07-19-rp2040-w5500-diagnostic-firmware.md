# RP2040 W5500 Diagnostic Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone RP2040 firmware and Pi controller that isolate W5500 driver failures by stage, recover from blocking calls, and produce revision-grounded transcripts through UDP and DHCP.

**Architecture:** A host-testable C core parses commands, formats events, tracks stage prerequisites, and encodes watchdog journals. A CDC-only Pico firmware wraps each real W5500 operation with stage reporting and watchdog recovery, while a Python standard-library controller on the Pi records events and provides UDP echo. The target compiles ioLibrary from the current checkout and uses only the public WIZnet PIO transport implementation from the installed package.

**Tech Stack:** C11, CMake 3.20+, Raspberry Pi Pico SDK, TinyUSB CDC, RP2040 hardware watchdog, WIZnet PIO SPI transport, ioLibrary W5500/DHCP, Python 3 standard library.

## Global Constraints

- The project root is `tests/hardware/rp2040_w5500_diag/`.
- Do not link, copy, flash, or depend on the temperature-server firmware.
- Compile `wizchip_conf.c`, `socket.c`, W5500, and DHCP from `IOLIBRARY_ROOT`, never from the installed WIZnet package.
- Use the W55RP20-EVB-PICO pins exactly: CS 20, SCK 21, MISO 22, MOSI 23, INT 24, RESET 25.
- Preserve the production port's non-recursive RP2040 critical-section behavior.
- USB is CDC-only with VID `0x6666`, PID `0x4021`, and product `RP2040 W5500 Diagnostic`.
- Host tooling uses only the Python 3 standard library.
- No HTTP, OPC UA, DNS, SNTP, TCP, sensor, ADC, HID, or application-service code.
- No driver compatibility shim may turn a failing stage into a pass.
- Build with `DIAG_EXPECT_SPI_STATUS=1` for the audited branch; use `0` only for a revision that does not claim SPI-status support.
- A blocking driver call is attempted once; watchdog recovery reports it and enters command mode without automatic retry.
- Driver fixes are separate changes after the harness produces evidence.
- Commit steps in this plan require explicit user authorization before execution.

---

## File Map

- `tests/hardware/rp2040_w5500_diag/CMakeLists.txt`: host tests and standalone Pico firmware build.
- `tests/hardware/rp2040_w5500_diag/include/tusb_config.h`: CDC-only TinyUSB configuration.
- `tests/hardware/rp2040_w5500_diag/src/diag_protocol.h`: command/event protocol types.
- `tests/hardware/rp2040_w5500_diag/src/diag_protocol.c`: strict ASCII command parser and event formatter.
- `tests/hardware/rp2040_w5500_diag/src/diag_runner.h`: stage IDs, descriptors, and runner state.
- `tests/hardware/rp2040_w5500_diag/src/diag_runner.c`: prerequisites, sequence numbers, and pass/fail state.
- `tests/hardware/rp2040_w5500_diag/src/diag_journal.h`: portable watchdog journal representation.
- `tests/hardware/rp2040_w5500_diag/src/diag_journal.c`: scratch-word encoding and validation.
- `tests/hardware/rp2040_w5500_diag/src/diag_watchdog.h`: RP2040 watchdog boundary.
- `tests/hardware/rp2040_w5500_diag/src/diag_watchdog.c`: scratch register persistence and timeout recovery.
- `tests/hardware/rp2040_w5500_diag/src/diag_usb_identity.h`: immutable diagnostic VID, PID, and product identity.
- `tests/hardware/rp2040_w5500_diag/src/diag_usb.h`: CDC input/output interface.
- `tests/hardware/rp2040_w5500_diag/src/diag_usb.c`: bounded CDC writes, flushes, and line input.
- `tests/hardware/rp2040_w5500_diag/src/usb_descriptors.c`: unique CDC-only descriptors.
- `tests/hardware/rp2040_w5500_diag/src/w5500_diag_board.h`: board transport interface.
- `tests/hardware/rp2040_w5500_diag/src/w5500_diag_board.c`: PIO transport, reset, critical section, and safe status registration probe.
- `tests/hardware/rp2040_w5500_diag/src/diag_w5500_stages.h`: W5500 stage entry points and shared state.
- `tests/hardware/rp2040_w5500_diag/src/diag_w5500_stages.c`: local register, pointer, socket, UDP, and DHCP stages.
- `tests/hardware/rp2040_w5500_diag/src/main.c`: boot recovery, automatic smoke sequence, and command loop.
- `tests/hardware/rp2040_w5500_diag/tests/test_diag_protocol.c`: parser/formatter tests.
- `tests/hardware/rp2040_w5500_diag/tests/test_diag_runner.c`: stage catalog and prerequisite tests.
- `tests/hardware/rp2040_w5500_diag/tests/test_diag_journal.c`: watchdog journal tests.
- `tests/hardware/rp2040_w5500_diag/tests/test_diag_usb_identity.c`: diagnostic USB identity test.
- `tests/hardware/rp2040_w5500_diag/host/diag_host.py`: Pi serial controller, transcript recorder, and UDP echo server.
- `tests/hardware/rp2040_w5500_diag/host/test_diag_host.py`: host event/parser tests.
- `tests/hardware/rp2040_w5500_diag/README.md`: build, flash, run, and interpretation guide.

---

### Task 1: Command And Event Protocol

**Files:**
- Create: `tests/hardware/rp2040_w5500_diag/CMakeLists.txt`
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_protocol.h`
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_protocol.c`
- Create: `tests/hardware/rp2040_w5500_diag/tests/test_diag_protocol.c`

**Interfaces:**
- Consumes: Newline-terminated ASCII commands from CDC.
- Produces: `diag_parse_command(const char *, diag_command_t *)` and `diag_format_event(char *, size_t, uint32_t, const char *, const char *, const char *)`.

- [ ] **Step 1: Write the failing parser and formatter test**

```c
#include "diag_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    diag_command_t command;
    char line[DIAG_LINE_MAX];

    assert(diag_parse_command("run pointer-api", &command));
    assert(command.kind == DIAG_COMMAND_RUN);
    assert(strcmp(command.stage, "pointer-api") == 0);

    assert(diag_parse_command("repeat udp 20", &command));
    assert(command.kind == DIAG_COMMAND_REPEAT);
    assert(command.count == 20u);

    assert(diag_parse_command("net 192.168.2.247 255.255.255.0 192.168.2.1 192.168.2.34 49000", &command));
    assert(command.kind == DIAG_COMMAND_NET);
    assert(command.device_ip[3] == 247u);
    assert(command.host_ip[3] == 34u);
    assert(command.host_port == 49000u);

    assert(!diag_parse_command("repeat udp 0", &command));
    assert(!diag_parse_command("run pointer-api trailing", &command));

    assert(diag_format_event(line, sizeof(line), 17u, "pointer-api", "FAIL",
                             "code=readback expected=1234 actual=1200") > 0);
    assert(strcmp(line,
                  "DIAG protocol=1 seq=17 stage=pointer-api event=FAIL code=readback expected=1234 actual=1200\n") == 0);
    return 0;
}
```

- [ ] **Step 2: Configure and run the test to verify it fails**

Create the host branch of `CMakeLists.txt` with a `test_diag_protocol` target, then run:

```bash
cmake -S tests/hardware/rp2040_w5500_diag -B /tmp/opencode/w5500-diag-host -DDIAG_BUILD_FIRMWARE=OFF
cmake --build /tmp/opencode/w5500-diag-host --target test_diag_protocol
```

Expected: compilation fails because `diag_protocol.h` and its implementation do not exist yet.

- [ ] **Step 3: Implement the strict protocol API**

Use these public types in `diag_protocol.h`:

```c
#ifndef W5500_DIAG_PROTOCOL_H
#define W5500_DIAG_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIAG_PROTOCOL_VERSION 1u
#define DIAG_LINE_MAX 192u
#define DIAG_STAGE_NAME_MAX 24u

typedef enum {
    DIAG_COMMAND_INVALID = 0,
    DIAG_COMMAND_HELP,
    DIAG_COMMAND_STATUS,
    DIAG_COMMAND_LIST,
    DIAG_COMMAND_RUN,
    DIAG_COMMAND_RUN_ALL,
    DIAG_COMMAND_REPEAT,
    DIAG_COMMAND_NET,
    DIAG_COMMAND_REBOOT
} diag_command_kind_t;

typedef struct {
    diag_command_kind_t kind;
    char stage[DIAG_STAGE_NAME_MAX];
    uint32_t count;
    uint8_t device_ip[4];
    uint8_t subnet[4];
    uint8_t gateway[4];
    uint8_t host_ip[4];
    uint16_t host_port;
} diag_command_t;

bool diag_parse_command(const char *line, diag_command_t *command);
int diag_format_event(char *buffer, size_t capacity, uint32_t sequence,
                      const char *stage, const char *event, const char *details);

#endif
```

Implement exact-token parsing with `%23s`, reject trailing tokens, validate each IPv4 octet as `0..255`, validate ports as `1..65535`, and validate repeat counts as `1..100000`. `diag_format_event` must reject whitespace in `stage` and `event`, append exactly one newline, and return `-1` on truncation.

- [ ] **Step 4: Run the protocol test**

```bash
cmake --build /tmp/opencode/w5500-diag-host --target test_diag_protocol
ctest --test-dir /tmp/opencode/w5500-diag-host --output-on-failure -R diag_protocol
```

Expected: `diag_protocol` passes.

- [ ] **Step 5: Commit the protocol component if explicitly authorized**

```bash
git add tests/hardware/rp2040_w5500_diag/CMakeLists.txt \
        tests/hardware/rp2040_w5500_diag/src/diag_protocol.c \
        tests/hardware/rp2040_w5500_diag/src/diag_protocol.h \
        tests/hardware/rp2040_w5500_diag/tests/test_diag_protocol.c
git commit -m "test: add W5500 diagnostic protocol"
```

---

### Task 2: Stage Catalog And Runner State

**Files:**
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_runner.h`
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_runner.c`
- Create: `tests/hardware/rp2040_w5500_diag/tests/test_diag_runner.c`
- Modify: `tests/hardware/rp2040_w5500_diag/CMakeLists.txt`

**Interfaces:**
- Consumes: Stage names parsed by Task 1 and a `network_configured` flag.
- Produces: `diag_stage_descriptor`, `diag_runner_can_run`, `diag_runner_begin`, `diag_runner_finish`, and stable numeric stage IDs for the watchdog journal.

- [ ] **Step 1: Write the failing stage-catalog test**

```c
#include "diag_runner.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    diag_runner_t runner;
    diag_runner_init(&runner);

    const diag_stage_descriptor_t *pointer = diag_stage_by_name("pointer-api");
    assert(pointer != NULL);
    assert(pointer->id == DIAG_STAGE_POINTER_API);
    assert(pointer->timeout_ms == 2000u);
    assert(diag_stage_by_name("unknown") == NULL);

    assert(diag_runner_can_run(&runner, DIAG_STAGE_CALLBACK_LAYOUT, false));
    assert(!diag_runner_can_run(&runner, DIAG_STAGE_TRANSPORT_INIT, false));

    uint32_t sequence = diag_runner_begin(&runner, DIAG_STAGE_CALLBACK_LAYOUT);
    assert(sequence == 1u);
    diag_runner_finish(&runner, DIAG_STAGE_CALLBACK_LAYOUT, DIAG_STAGE_PASS);
    assert(diag_runner_can_run(&runner, DIAG_STAGE_TRANSPORT_INIT, false));

    diag_runner_finish(&runner, DIAG_STAGE_TRANSPORT_INIT, DIAG_STAGE_PASS);
    assert(!diag_runner_can_run(&runner, DIAG_STAGE_SOCKET_OPEN, false));

    runner.passed_mask = (1u << DIAG_STAGE_COUNT) - 1u;
    diag_runner_prepare_repeat(&runner, DIAG_STAGE_UDP);
    assert((runner.passed_mask & (1u << DIAG_STAGE_SOCKET_OPEN)) == 0u);
    assert((runner.passed_mask & (1u << DIAG_STAGE_POINTER_API)) != 0u);

    runner.passed_mask = (1u << DIAG_STAGE_COUNT) - 1u;
    diag_runner_prepare_repeat(&runner, DIAG_STAGE_DHCP);
    assert((runner.passed_mask & (1u << DIAG_STAGE_CHIP_RESET)) == 0u);
    assert((runner.passed_mask & (1u << DIAG_STAGE_TRANSPORT_INIT)) != 0u);
    return 0;
}
```

- [ ] **Step 2: Run the test to verify the runner API is missing**

```bash
cmake --build /tmp/opencode/w5500-diag-host --target test_diag_runner
```

Expected: compilation fails because `diag_runner.h` is absent.

- [ ] **Step 3: Implement stable stage IDs and prerequisites**

Use these IDs without reordering them later:

```c
typedef enum {
    DIAG_STAGE_CALLBACK_LAYOUT = 0,
    DIAG_STAGE_TRANSPORT_INIT = 1,
    DIAG_STAGE_CHIP_RESET = 2,
    DIAG_STAGE_VERSION = 3,
    DIAG_STAGE_MEMORY_INIT = 4,
    DIAG_STAGE_PHY_LINK = 5,
    DIAG_STAGE_SINGLE_REGISTER = 6,
    DIAG_STAGE_BURST_REGISTER = 7,
    DIAG_STAGE_POINTER_SEQUENTIAL = 8,
    DIAG_STAGE_POINTER_BURST = 9,
    DIAG_STAGE_POINTER_API = 10,
    DIAG_STAGE_SOCKET_OPEN = 11,
    DIAG_STAGE_UDP = 12,
    DIAG_STAGE_DHCP = 13,
    DIAG_STAGE_COUNT = 14
} diag_stage_id_t;

typedef enum {
    DIAG_STAGE_NOT_RUN = 0,
    DIAG_STAGE_PASS,
    DIAG_STAGE_FAIL,
    DIAG_STAGE_TIMEOUT
} diag_stage_result_t;

typedef struct {
    diag_stage_id_t id;
    const char *name;
    uint32_t required_mask;
    uint32_t timeout_ms;
    bool requires_network_config;
} diag_stage_descriptor_t;

typedef struct {
    uint32_t passed_mask;
    uint32_t sequence;
    bool recovery_mode;
} diag_runner_t;
```

Set `callback-layout` timeout to `0`, primitive blocking stages to `2000`, `phy-link` to `5000`, `udp` to `5000`, and `dhcp` to `60000` as a software deadline. `socket-open` requires host network configuration. A failed prerequisite must prevent execution; a failed unrelated stage must not be silently marked complete. Add `diag_runner_prepare_repeat(diag_runner_t *, diag_stage_id_t)` with an explicit restart boundary per stage: `pointer-api` clears only itself and its successors, `udp` clears from `socket-open`, and `dhcp` clears from `chip-reset`. This forces each UDP iteration to reopen its socket and each DHCP iteration to rerun reset, version, memory initialization, and PHY link while retaining the valid callback and transport setup.

- [ ] **Step 4: Run all host C tests**

```bash
cmake --build /tmp/opencode/w5500-diag-host
ctest --test-dir /tmp/opencode/w5500-diag-host --output-on-failure
```

Expected: protocol and runner tests pass.

- [ ] **Step 5: Commit the runner if explicitly authorized**

```bash
git add tests/hardware/rp2040_w5500_diag
git commit -m "test: add W5500 diagnostic stage runner"
```

---

### Task 3: Watchdog Journal And RP2040 Recovery Boundary

**Files:**
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_journal.h`
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_journal.c`
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_watchdog.h`
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_watchdog.c`
- Create: `tests/hardware/rp2040_w5500_diag/tests/test_diag_journal.c`
- Modify: `tests/hardware/rp2040_w5500_diag/CMakeLists.txt`

**Interfaces:**
- Consumes: Stable `diag_stage_id_t`, sequence number, and phase ID.
- Produces: four scratch words in indices `0..3`; scratch index `4` remains reserved for Pico SDK watchdog detection.

- [ ] **Step 1: Write the failing journal codec test**

```c
#include "diag_journal.h"

#include <assert.h>

int main(void)
{
    uint32_t words[DIAG_JOURNAL_WORD_COUNT];
    diag_journal_t input = {
        .stage = DIAG_STAGE_POINTER_API,
        .phase = 2u,
        .sequence = 41u,
    };
    diag_journal_t output;

    diag_journal_encode(&input, words);
    assert(diag_journal_decode(words, &output));
    assert(output.stage == DIAG_STAGE_POINTER_API);
    assert(output.phase == 2u);
    assert(output.sequence == 41u);

    words[3] ^= 1u;
    assert(!diag_journal_decode(words, &output));
    diag_journal_clear(words);
    assert(!diag_journal_decode(words, &output));
    return 0;
}
```

- [ ] **Step 2: Run the test to verify the journal API is missing**

```bash
cmake --build /tmp/opencode/w5500-diag-host --target test_diag_journal
```

Expected: compilation fails because `diag_journal.h` is absent.

- [ ] **Step 3: Implement the portable codec and Pico adapter**

Use magic `0x57444941u` (`WDIA`) and this exact encoding:

```c
#define DIAG_JOURNAL_MAGIC 0x57444941u
#define DIAG_JOURNAL_WORD_COUNT 4u

typedef struct {
    diag_stage_id_t stage;
    uint16_t phase;
    uint32_t sequence;
} diag_journal_t;

void diag_journal_encode(const diag_journal_t *journal, uint32_t words[4])
{
    words[0] = DIAG_JOURNAL_MAGIC;
    words[1] = ((uint32_t)DIAG_PROTOCOL_VERSION << 24) |
               ((uint32_t)journal->stage << 16) | journal->phase;
    words[2] = journal->sequence;
    words[3] = words[0] ^ words[1] ^ words[2] ^ 0xA5A55A5Au;
}
```

`diag_watchdog_begin` writes scratch `0..3`, then calls `watchdog_enable(timeout_ms, false)`. `diag_watchdog_complete` disables the watchdog and clears scratch `0..3`. `diag_watchdog_recover` must call `watchdog_enable_caused_reboot`, validate the journal, copy it to the caller, disable the watchdog, and clear only scratch `0..3`.

Expose these exact Pico-only functions:

```c
bool diag_watchdog_recover(diag_journal_t *journal);
void diag_watchdog_begin(diag_stage_id_t stage, uint16_t phase,
                         uint32_t sequence, uint32_t timeout_ms);
void diag_watchdog_complete(void);
void diag_watchdog_feed(void);
```

- [ ] **Step 4: Run all host tests and a firmware syntax compile**

```bash
cmake --build /tmp/opencode/w5500-diag-host
ctest --test-dir /tmp/opencode/w5500-diag-host --output-on-failure
```

Expected: all three host tests pass. `diag_watchdog.c` is excluded from the host build and is compiled in Task 4's Pico target.

- [ ] **Step 5: Commit the watchdog journal if explicitly authorized**

```bash
git add tests/hardware/rp2040_w5500_diag
git commit -m "feat: add watchdog stage journal"
```

---

### Task 4: Standalone Pico Build And CDC Command Shell

**Files:**
- Create: `tests/hardware/rp2040_w5500_diag/include/tusb_config.h`
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_usb.h`
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_usb.c`
- Create: `tests/hardware/rp2040_w5500_diag/src/usb_descriptors.c`
- Create: `tests/hardware/rp2040_w5500_diag/src/main.c`
- Modify: `tests/hardware/rp2040_w5500_diag/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 commands, Task 2 runner state, Task 3 recovered journal.
- Produces: USB CDC device `6666:4021`, bounded event output, line input, `help/status/list/reboot`, and recovered timeout reporting before any W5500 initialization.

- [ ] **Step 1: Add a host identity assertion that initially fails**

Add `src/diag_usb_identity.h` and assert these constants from a new `tests/test_diag_usb_identity.c`:

```c
#include "diag_usb_identity.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(DIAG_USB_VID == 0x6666u);
    assert(DIAG_USB_PID == 0x4021u);
    assert(strcmp(DIAG_USB_PRODUCT, "RP2040 W5500 Diagnostic") == 0);
    return 0;
}
```

- [ ] **Step 2: Run the identity target to verify the header is missing**

```bash
cmake --build /tmp/opencode/w5500-diag-host --target test_diag_usb_identity
```

Expected: compilation fails because `diag_usb_identity.h` is absent.

- [ ] **Step 3: Implement CDC-only USB and the command loop**

Use this TinyUSB class configuration:

```c
#define CFG_TUSB_MCU OPT_MCU_RP2040
#define CFG_TUSB_OS OPT_OS_PICO
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 512
```

`diag_usb_write_line` must call `tud_task` while draining at most `DIAG_LINE_MAX` bytes, use `tud_cdc_write_available`, call `tud_cdc_write_flush`, and fail after `250 ms` rather than block forever. `diag_usb_poll_line` accumulates until `\n`, strips one optional `\r`, rejects overflow, and returns one complete line per call.

Expose these exact functions:

```c
void diag_usb_init(void);
void diag_usb_task(void);
bool diag_usb_connected(void);
bool diag_usb_write_line(const char *line);
bool diag_usb_poll_line(char *line, size_t capacity);
```

At boot, `main` must initialize TinyUSB before any W5500 code, report build identity, recover a valid watchdog journal as `TIMEOUT`, set `runner.recovery_mode`, and skip automatic stages in recovery mode.

- [ ] **Step 4: Configure and cross-build the standalone UF2 on the Pi**

```bash
rsync -a --delete --exclude=.git/ /home/quackdcs/W5500/ root@192.168.2.34:/tmp/ioLibrary-driver-diag/
ssh root@192.168.2.34 "cmake -S /tmp/ioLibrary-driver-diag/tests/hardware/rp2040_w5500_diag -B /root/w5500-diag-build -DDIAG_BUILD_FIRMWARE=ON -DDIAG_EXPECT_SPI_STATUS=1 -DPICO_SDK_PATH=/usr/share/pico-sdk -DWIZNET_PICO_C_PATH=/usr/share/wiznet-pico-c -DIOLIBRARY_ROOT=/tmp/ioLibrary-driver-diag"
ssh root@192.168.2.34 "cmake --build /root/w5500-diag-build --target rp2040_w5500_diag --parallel 4"
```

Expected: `/root/w5500-diag-build/rp2040_w5500_diag.uf2` is generated without referencing `/root/firmware`.

- [ ] **Step 5: Flash and verify USB identity before adding W5500 code**

After the board is placed in BOOTSEL mode:

```bash
ssh root@192.168.2.34 "picotool load -v -x /root/w5500-diag-build/rp2040_w5500_diag.uf2"
ssh root@192.168.2.34 "lsusb -v -d 6666:4021"
```

Expected: `lsusb` reports one `6666:4021` device with product `RP2040 W5500 Diagnostic`; the CDC transcript accepts `help`, `status`, and `list`.

- [ ] **Step 6: Commit the standalone USB shell if explicitly authorized**

```bash
git add tests/hardware/rp2040_w5500_diag
git commit -m "feat: add standalone RP2040 diagnostic shell"
```

---

### Task 5: W5500 PIO Transport And Bring-Up Stages

**Files:**
- Create: `tests/hardware/rp2040_w5500_diag/src/w5500_diag_board.h`
- Create: `tests/hardware/rp2040_w5500_diag/src/w5500_diag_board.c`
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_w5500_stages.h`
- Create: `tests/hardware/rp2040_w5500_diag/src/diag_w5500_stages.c`
- Modify: `tests/hardware/rp2040_w5500_diag/CMakeLists.txt`
- Modify: `tests/hardware/rp2040_w5500_diag/src/main.c`

**Interfaces:**
- Consumes: WIZnet `wiznet_spi_pio_open`, ioLibrary registration functions, runner stages, and watchdog wrapper.
- Produces: callback-layout, transport-init, chip-reset, version, memory-init, and bounded phy-link stages.

- [ ] **Step 1: Add the new stage dispatch calls so the firmware build fails**

Add a dispatch table in `main.c` that references these exact functions:

```c
diag_stage_result_t diag_stage_callback_layout(diag_stage_context_t *context);
diag_stage_result_t diag_stage_transport_init(diag_stage_context_t *context);
diag_stage_result_t diag_stage_chip_reset(diag_stage_context_t *context);
diag_stage_result_t diag_stage_version(diag_stage_context_t *context);
diag_stage_result_t diag_stage_memory_init(diag_stage_context_t *context);
diag_stage_result_t diag_stage_phy_link(diag_stage_context_t *context);
```

- [ ] **Step 2: Cross-build to verify the stage functions are undefined**

```bash
ssh root@192.168.2.34 "cmake --build /root/w5500-diag-build --target rp2040_w5500_diag --parallel 4"
```

Expected: link fails with undefined `diag_stage_callback_layout` and related symbols.

- [ ] **Step 3: Implement the faithful board transport**

Use a static `critical_section_t`, a static `wiznet_spi_handle_t`, and this exact W55RP20 configuration:

```c
static const wiznet_spi_config_t spi_config = {
    .data_in_pin = 22,
    .data_out_pin = 23,
    .cs_pin = 20,
    .clock_pin = 21,
    .irq_pin = 24,
    .reset_pin = 25,
    .clock_div_major = PIO_CLOCK_DIV_MAJOR,
    .clock_div_minor = PIO_CLOCK_DIV_MINOR,
};
```

Declare the proposed status registration API as an unresolved weak symbol so the current broken revision still links:

```c
extern void reg_wizchip_spi_status_cbfunc(uint8_t (*check_busy)(void),
                                          uint8_t (*get_error)(void))
    __attribute__((weak));
```

`w5500_diag_board_status_contract` must return false when this symbol is absent. When present, save all four `WIZCHIP.IF.SPI` callbacks, register two status callbacks that return zero for the synchronous PIO transport, and verify all four transport callbacks remain unchanged. Restore the saved transport callbacks before returning false if registration overwrites them.

Expose this board boundary:

```c
typedef enum {
    W5500_DIAG_STATUS_OK = 0,
    W5500_DIAG_STATUS_NOT_CLAIMED,
    W5500_DIAG_STATUS_NO_API,
    W5500_DIAG_STATUS_ALIAS,
    W5500_DIAG_STATUS_PIO_OPEN
} w5500_diag_board_status_t;

w5500_diag_board_status_t w5500_diag_board_check_status_contract(bool expected);
w5500_diag_board_status_t w5500_diag_board_init(void);
void w5500_diag_board_reset(void);
```

Define this CMake option and pass it to the firmware as `0` or `1`:

```cmake
option(DIAG_EXPECT_SPI_STATUS "Driver revision claims SPI-status callback support" ON)
target_compile_definitions(rp2040_w5500_diag PRIVATE
    DIAG_EXPECT_SPI_STATUS=$<BOOL:${DIAG_EXPECT_SPI_STATUS}>
)
```

When `DIAG_EXPECT_SPI_STATUS=1`, an absent or aliasing registration API is `FAIL`. When it is `0`, absence is reported as `PASS code=status-not-claimed`; an API that is present but aliases transport callbacks remains `FAIL`.

`w5500_diag_board_init` opens and activates the PIO handle, initializes the non-recursive critical section, and registers CS, byte, and burst callbacks. It must not call the installed port's `wizchip_initialize` or its unbounded PHY loop.

- [ ] **Step 4: Implement bring-up stages with one watchdog-wrapped call per phase**

Use `getVERSIONR() == 0x04`, `wizchip_init` with two arrays containing eight `2` values, and a PHY loop bounded to 5 seconds. Emit distinct failure codes `no-status-api`, `status-alias`, `pio-open`, `version`, `memory-init`, and `phy-link-down`.

Every stage follows this call pattern:

```c
diag_watchdog_begin(stage, phase, sequence, timeout_ms);
result = perform_one_driver_call();
diag_watchdog_complete();
```

Do not wrap a multi-call loop in one journal phase; update the phase before each potentially blocking transfer.

Define the shared stage context in `diag_w5500_stages.h` so every later task uses the same names and sizes:

```c
typedef struct {
    diag_runner_t *runner;
    uint32_t sequence;
    bool network_configured;
    wiz_NetInfo network;
    uint8_t host_ip[4];
    uint16_t host_port;
    uint8_t dhcp_buffer[2048];
    char details[96];
} diag_stage_context_t;

void diag_stage_set_details(diag_stage_context_t *context,
                            const char *format, ...);
```

The dispatcher clears `details` before each stage, emits `START`, calls the stage once, and uses `details` as the optional key-value suffix on `PASS` or `FAIL`.

- [ ] **Step 5: Build, flash, and verify the current revision fails preflight deterministically**

```bash
ssh root@192.168.2.34 "cmake --build /root/w5500-diag-build --target rp2040_w5500_diag --parallel 4"
```

After BOOTSEL and flash, expected transcript on the current audited revision:

```text
DIAG protocol=1 seq=1 stage=callback-layout event=START
DIAG protocol=1 seq=1 stage=callback-layout event=FAIL code=no-status-api
```

No W5500 transfer occurs after this prerequisite failure during automatic smoke testing.

- [ ] **Step 6: Commit the board and bring-up stages if explicitly authorized**

```bash
git add tests/hardware/rp2040_w5500_diag
git commit -m "feat: add W5500 diagnostic bring-up stages"
```

---

### Task 6: Register And Pointer Diagnostic Stages

**Files:**
- Modify: `tests/hardware/rp2040_w5500_diag/src/diag_w5500_stages.h`
- Modify: `tests/hardware/rp2040_w5500_diag/src/diag_w5500_stages.c`
- Modify: `tests/hardware/rp2040_w5500_diag/src/main.c`

**Interfaces:**
- Consumes: Initialized W5500 with socket 0 closed.
- Produces: single-register, burst-register, pointer-sequential, pointer-burst, and pointer-api stage results with exact readbacks.

- [ ] **Step 1: Add dispatch entries and verify the new functions are missing**

Reference these functions from the stage table:

```c
diag_stage_result_t diag_stage_single_register(diag_stage_context_t *context);
diag_stage_result_t diag_stage_burst_register(diag_stage_context_t *context);
diag_stage_result_t diag_stage_pointer_sequential(diag_stage_context_t *context);
diag_stage_result_t diag_stage_pointer_burst(diag_stage_context_t *context);
diag_stage_result_t diag_stage_pointer_api(diag_stage_context_t *context);
```

Run the remote build and expect undefined-symbol failures.

- [ ] **Step 2: Implement save, pattern, verify, and restore transactions**

Use `_RCR_` for the byte test and `GAR` for the four-byte burst test. Use these patterns:

```c
static const uint8_t burst_pattern[4] = {0xA5u, 0x5Au, 0x3Cu, 0xC3u};
#define DIAG_TX_WR_PATTERN 0x1234u
#define DIAG_RX_RD_PATTERN 0x5678u
```

Sequential pointer writes must call `WIZCHIP_WRITE` for high and low bytes explicitly. Burst pointer writes must call `WIZCHIP_WRITE_BUF` once with `{high, low}`. Public pointer writes must call `setSn_TX_WR` and `setSn_RX_RD` unchanged.

Before each write mode, save both pointer values. After readback, restore both values using the same mode under test when possible; if the mode fails after returning, reset socket state rather than claiming restoration.

- [ ] **Step 3: Add phase-specific watchdog reporting**

Use stable phase IDs:

```c
enum {
    DIAG_PHASE_SAVE = 1,
    DIAG_PHASE_WRITE_TX = 2,
    DIAG_PHASE_READ_TX = 3,
    DIAG_PHASE_WRITE_RX = 4,
    DIAG_PHASE_READ_RX = 5,
    DIAG_PHASE_RESTORE = 6
};
```

The known recursive setter must therefore recover as `phase=write-tx`, not merely as an unspecified pointer-stage timeout.

- [ ] **Step 4: Cross-build and run non-destructive stages on a revision with a corrected status contract**

```bash
ssh root@192.168.2.34 "cmake --build /root/w5500-diag-build --target rp2040_w5500_diag --parallel 4"
```

Expected on a candidate with independent status callbacks: byte, burst, sequential pointer, and burst pointer stages pass. A candidate retaining the nested outer CRIS wrappers resets and reports `pointer-api TIMEOUT phase=write-tx` after reconnect.

- [ ] **Step 5: Commit pointer diagnostics if explicitly authorized**

```bash
git add tests/hardware/rp2040_w5500_diag
git commit -m "feat: add W5500 pointer diagnostics"
```

---

### Task 7: Static UDP And DHCP Stages

**Files:**
- Modify: `tests/hardware/rp2040_w5500_diag/src/diag_w5500_stages.h`
- Modify: `tests/hardware/rp2040_w5500_diag/src/diag_w5500_stages.c`
- Modify: `tests/hardware/rp2040_w5500_diag/src/main.c`
- Modify: `tests/hardware/rp2040_w5500_diag/CMakeLists.txt`

**Interfaces:**
- Consumes: Host-supplied network configuration, socket 0, Pi UDP echo endpoint, and ioLibrary DHCP.
- Produces: socket-open, UDP round-trip, and bounded DHCP results plus socket register snapshots.

- [ ] **Step 1: Add network stage dispatch and verify link failure**

Reference these functions before implementing them:

```c
diag_stage_result_t diag_stage_socket_open(diag_stage_context_t *context);
diag_stage_result_t diag_stage_udp(diag_stage_context_t *context);
diag_stage_result_t diag_stage_dhcp(diag_stage_context_t *context);
```

Expected: remote link fails on the three symbols.

- [ ] **Step 2: Implement deterministic MAC and static network setup**

Derive a locally administered MAC from the Pico unique ID:

```c
mac[0] = 0x02u;
mac[1] = board_id.id[3];
mac[2] = board_id.id[4];
mac[3] = board_id.id[5];
mac[4] = board_id.id[6];
mac[5] = board_id.id[7];
```

Populate `wiz_NetInfo` from the validated `net` command and call `ctlnetwork(CN_SET_NETINFO, &net_info)`. Open socket 0 with `socket(0, Sn_MR_UDP, 49001, 0)` and require `getSn_SR(0) == SOCK_UDP`.

- [ ] **Step 3: Implement UDP echo with pointer evidence**

Send exactly this payload shape to the configured Pi endpoint:

```c
typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint8_t payload[24];
} diag_udp_packet_t;
```

Use magic `0x44555031u` (`DUP1`), fill payload byte `i` with `(sequence + i) & 0xFF`, record `Sn_TX_WR` before/after `sendto`, and record `Sn_RX_RD` before/after `recvfrom`. Bound ARP/send and receive to 5 seconds while servicing TinyUSB between calls. Require byte-for-byte equality and the Pi source address/port.

- [ ] **Step 4: Implement bounded DHCP state reporting**

Reset the chip and memory allocation, call `DHCP_init(0, context->dhcp_buffer)`, and call `DHCP_time_handler` once per elapsed second. Call `DHCP_run` from a loop with a 60-second software deadline. Emit a transition record only when the return state changes. Pass only on `DHCP_IP_ASSIGN`, `DHCP_IP_CHANGED`, or `DHCP_IP_LEASED` after `getIPfromDHCP`, `getGWfromDHCP`, `getSNfromDHCP`, and `getDNSfromDHCP` all provide a complete lease.

Snapshot `Sn_SR`, `Sn_IR`, `Sn_TX_WR`, `Sn_RX_RD`, `Sn_TX_FSR`, and `Sn_RX_RSR` on every failure or timeout.

- [ ] **Step 5: Cross-build the complete firmware**

```bash
ssh root@192.168.2.34 "cmake --build /root/w5500-diag-build --target rp2040_w5500_diag --parallel 4"
```

Expected: UF2 links `Internet/DHCP/dhcp.c` from `/tmp/ioLibrary-driver-diag` and does not link DNS, SNTP, HTTP, or OPC UA objects.

- [ ] **Step 6: Commit network diagnostics if explicitly authorized**

```bash
git add tests/hardware/rp2040_w5500_diag
git commit -m "feat: add W5500 UDP and DHCP diagnostics"
```

---

### Task 8: Pi Controller, Transcript Parser, And UDP Echo

**Files:**
- Create: `tests/hardware/rp2040_w5500_diag/host/diag_host.py`
- Create: `tests/hardware/rp2040_w5500_diag/host/test_diag_host.py`

**Interfaces:**
- Consumes: `/dev/ttyACM*` CDC stream and UDP packets from Task 7.
- Produces: timestamped transcript, automatic reconnect after watchdog reset, command execution, UDP echo, and process exit status.

- [ ] **Step 1: Write failing Python parser tests**

```python
import unittest

from diag_host import parse_event, validate_sequence


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

    def test_detects_sequence_gap(self):
        with self.assertRaises(ValueError):
            validate_sequence(17, 19)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify imports fail**

```bash
python3 -m unittest discover -s tests/hardware/rp2040_w5500_diag/host -p 'test_*.py' -v
```

Expected: import fails because `diag_host.py` is absent.

- [ ] **Step 3: Implement strict event parsing and serial I/O**

`parse_event` must require the literal prefix `DIAG`, split only `key=value` tokens, reject duplicate keys, require `protocol`, `seq`, `stage`, and `event`, and require protocol `1`.

Open CDC using `os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)` and configure raw `termios` with `CS8 | CREAD | CLOCAL`, `VMIN=0`, and `VTIME=1`. Do not add a pyserial dependency.

- [ ] **Step 4: Implement UDP echo and watchdog reconnect**

Use `selectors.DefaultSelector` for CDC and a UDP socket bound to the explicit `--listen-ip` and `--listen-port`. Echo only packets with magic `0x44555031`; reject all others. On serial disconnect, rescan only for USB VID/PID `6666:4021`, reconnect, and require a matching recovered `TIMEOUT` event within 10 seconds.

Support these invocations:

```text
python3 diag_host.py --device /dev/ttyACM0 status
python3 diag_host.py --device /dev/ttyACM0 run pointer-api
python3 diag_host.py --device /dev/ttyACM0 repeat pointer-api 100
python3 diag_host.py --device /dev/ttyACM0 --device-ip 192.168.2.247 --subnet 255.255.255.0 --gateway 192.168.2.1 --listen-ip 192.168.2.34 --listen-port 49000 run-all
```

Exit `0` only when all requested stages pass, `2` on `FAIL`, `3` on recovered `TIMEOUT`, and `4` on protocol or transport error.

- [ ] **Step 5: Run host tests**

```bash
python3 -m unittest discover -s tests/hardware/rp2040_w5500_diag/host -p 'test_*.py' -v
```

Expected: all parser, sequence, and packet validation tests pass.

- [ ] **Step 6: Commit the Pi controller if explicitly authorized**

```bash
git add tests/hardware/rp2040_w5500_diag/host
git commit -m "test: add Pi W5500 diagnostic controller"
```

---

### Task 9: Provenance, Documentation, And End-To-End Harness Acceptance

**Files:**
- Create: `tests/hardware/rp2040_w5500_diag/README.md`
- Modify: `tests/hardware/rp2040_w5500_diag/CMakeLists.txt`
- Modify: `tests/hardware/rp2040_w5500_diag/src/main.c`
- Modify: `/home/quackdcs/W5500/.gitignore`

**Interfaces:**
- Consumes: Complete firmware and Pi controller.
- Produces: reproducible build instructions, embedded source provenance, clean build artifacts, and captured HIL evidence.

- [ ] **Step 1: Add a provenance assertion that fails before generated values exist**

Add a boot-event formatter test requiring these tokens:

```text
DIAG protocol=1 seq=0 stage=boot event=PASS git=<40-hex> dirty=<0-or-1> diff=<64-hex> build=<UTC>
```

Configure the host test with fixed values and assert exact formatting. Expected initial failure: generated build macros are absent.

- [ ] **Step 2: Generate and embed provenance**

Add CMake cache variables `DIAG_GIT_SHA`, `DIAG_GIT_DIRTY`, `DIAG_DIFF_SHA256`, and `DIAG_BUILD_UTC`; reject firmware configuration when any is absent. Generate `diag_build_info.h` with `configure_file` and print all four values in the first boot event.

Calculate values before syncing the tree:

```bash
git_sha=$(git rev-parse HEAD)
dirty=$([ -n "$(git status --porcelain)" ] && printf 1 || printf 0)
diff_sha=$(git diff --no-ext-diff --binary | sha256sum | cut -d' ' -f1)
build_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
```

Pass them as explicit `-D` values to the Pi CMake configure command. This makes provenance independent of whether `.git` is synchronized.

- [ ] **Step 3: Document only the standalone workflow**

The README must contain:

- Required Pi paths: `/usr/share/pico-sdk` and `/usr/share/wiznet-pico-c`.
- Host-test commands.
- Rsync and firmware CMake commands with provenance variables.
- BOOTSEL plus `picotool load -v -x` flashing.
- How to locate `6666:4021` and its CDC device.
- Every command and stage with pass/fail meaning.
- Why `callback-layout FAIL code=no-status-api` identifies the current union/API defect.
- Why `pointer-api TIMEOUT phase=write-tx` identifies recursive critical-section acquisition.
- Explicit statement that USB enumeration or a DHCP address alone is not a pass.
- Explicit statement that `/root/firmware` and the temperature-server UF2 are not used.

Add these ignore patterns:

```gitignore
tests/hardware/rp2040_w5500_diag/build/
tests/hardware/rp2040_w5500_diag/*.log
tests/hardware/rp2040_w5500_diag/host/__pycache__/
```

- [ ] **Step 4: Run all host and build verification**

```bash
cmake -S tests/hardware/rp2040_w5500_diag -B /tmp/opencode/w5500-diag-host -DDIAG_BUILD_FIRMWARE=OFF
cmake --build /tmp/opencode/w5500-diag-host --parallel 4
ctest --test-dir /tmp/opencode/w5500-diag-host --output-on-failure
python3 -m unittest discover -s tests/hardware/rp2040_w5500_diag/host -p 'test_*.py' -v
git diff --check
```

Expected: all C and Python tests pass; `git diff --check` emits no output.

- [ ] **Step 5: Verify current-driver structural diagnosis on hardware**

Build and flash the current driver with `DIAG_EXPECT_SPI_STATUS=1`. Run the Pi controller and save the transcript under `/tmp/opencode/w5500-diag-current.log`.

Expected: unique USB enumeration succeeds, the boot event contains correct provenance, and automatic smoke stops at `callback-layout FAIL code=no-status-api` before any W5500 transfer. This is a valid harness result, not a harness failure.

- [ ] **Step 6: Run the acceptance matrix for each separate driver correction**

For a status-storage correction, require callback-layout through burst-register to pass. For an isolated nested-lock revision with corrected status storage, require watchdog recovery at `pointer-api phase=write-tx`. For a candidate pointer correction, require:

```bash
python3 tests/hardware/rp2040_w5500_diag/host/diag_host.py --device /dev/ttyACM0 repeat pointer-api 100
python3 tests/hardware/rp2040_w5500_diag/host/diag_host.py --device /dev/ttyACM0 --device-ip 192.168.2.247 --subnet 255.255.255.0 --gateway 192.168.2.1 --listen-ip 192.168.2.34 --listen-port 49000 repeat udp 20
python3 tests/hardware/rp2040_w5500_diag/host/diag_host.py --device /dev/ttyACM0 repeat dhcp 3
```

Expected: all requested repetitions pass and each transcript carries the matching source provenance. These runs gate the separate driver commits; they do not belong in the diagnostic-firmware commit.

- [ ] **Step 7: Commit the completed harness if explicitly authorized**

```bash
git add .gitignore tests/hardware/rp2040_w5500_diag docs/superpowers/specs/2026-07-19-rp2040-w5500-diagnostic-firmware-design.md docs/superpowers/plans/2026-07-19-rp2040-w5500-diagnostic-firmware.md
git commit -m "test: add RP2040 W5500 diagnostic firmware"
```

---

## Completion Gate

The diagnostic-firmware work is complete only when:

- Host C and Python tests pass.
- The standalone UF2 builds without `/root/firmware` or temperature-server inputs.
- USB enumerates as `6666:4021`.
- Watchdog recovery reports the exact persisted stage and phase without a reset loop.
- The current SPI-status layout defect is reported before the first transfer.
- Separate corrected driver variants can progress through local, UDP, and DHCP stages with provenance-bearing transcripts.
- No unrelated working-tree changes are staged or reverted.
