# Data Model: Resolve Production Audit Findings

This feature introduces no persisted application data. Its model consists of embedded runtime state and release-evidence records whose transitions must be explicit and testable.

## Socket Operation State

One record exists for each hardware socket `0..7`.

| Field | Type | Rules |
|-------|------|-------|
| `socket_number` | `uint8_t` | Immutable; must be less than `_WIZCHIP_SOCK_NUM_` before lock access. |
| `protocol_mode` | `uint8_t` | One supported low-nibble Sn_MR mode; cleared on close/reset. |
| `io_mode` | enum | `BLOCKING` or `NONBLOCKING`; stored per socket, never in a shared bitfield. |
| `send_in_progress` | boolean | Per-socket value protected by the socket lock. |
| `remaining_size` | `uint16_t` | Cleared on close/reset and bounded by configured RX capacity. |
| `packet_info` | `uint8_t` | One valid PACK state; cleared on close/reset. |
| `tx_capacity` | `uint16_t` | Hardware/cache coherent, 0 to 16 KiB. |
| `rx_capacity` | `uint16_t` | Hardware/cache coherent, 0 to 16 KiB. |
| `health` | enum | `HEALTHY` or `FAULTED`. |
| `last_error` | `int8_t` | `SOCK_OK`, existing socket error, `SOCKERR_DEADLINE`, `SOCKERR_IO`, or `SOCKERR_NOTREADY`. |

### Socket Transitions

```text
CLOSED --open accepted/state verified--> ACTIVE
ACTIVE --listen/connect/state change--> ACTIVE
ACTIVE --bounded close verified--------> CLOSED
ACTIVE --ambiguous deadline/I/O error--> FAULTED
FAULTED --bounded close verified-------> CLOSED
FAULTED --verified chip reset----------> CLOSED
FAULTED --ordinary data operation------> reject with SOCKERR_NOTREADY
```

### Socket Invariants

- Exactly one socket lock owner may mutate a socket record.
- Public code does not call another lock-taking public socket operation while holding the lock.
- A successful lock acquisition has exactly one release on every exit.
- Zero-length I/O returns zero before lock acquisition and does not mutate this record.
- Different sockets never update one shared read-modify-write bitfield.

## Root Driver Lifecycle

The root callback/configuration singleton has one lifecycle record.

| Field | Type | Rules |
|-------|------|-------|
| `state` | enum | `UNINITIALIZED`, `READY`, `RESETTING`, or `FAULTED`. |
| `last_io_error` | `int8_t` | Sticky first transport error until explicit clear/recovery. |
| `time_ready` | boolean | True only when a monotonic callback is registered. |
| `status_ready` | boolean | True only when independent SPI status callbacks are registered. |
| `locks_ready` | boolean | True only when valid enter/exit pairs are registered. |
| `timeout_config` | Deadline policy | Validated nonzero command, operation, and PHY limits. |

### Root Lifecycle Transitions

```text
UNINITIALIZED --valid callbacks/init--------> READY
READY --------software or hardware reset---> RESETTING
RESETTING ----verified register/cache state-> READY
READY --------transport error--------------> FAULTED
FAULTED ------successful recover/reinit----> READY
FAULTED ------ordinary hardware operation--> reject with error
```

### Root Lifecycle Invariants

- SPI status storage is independent from byte, burst, BUS, and QSPI callback storage.
- A successful later transfer does not erase a sticky failure from the initiating operation.
- Callback registration occurs only while no operation can consume a partially updated callback set.
- Missing status or lifecycle readiness cannot appear as successful hardware access.

## Deadline Policy and Instance

### Policy

| Field | Type | Default behavior |
|-------|------|------------------|
| `command_timeout_us` | `uint32_t` | 10,000 microseconds. |
| `operation_timeout_us` | `uint32_t` | 2,000,000 microseconds or the RTR/RCR-derived retry window plus margin, whichever is longer. |
| `phy_timeout_us` | `uint32_t` | 10,000 microseconds after reset hold. |
| `poll_fallback` | `uint32_t` | `_WIZCHIP_POLL_MAX_` when no clock callback exists. |
| `now_us` | callback | Monotonic time source; RP2040 uses `time_us_64()`. |
| `wait_hook` | callback | Optional watchdog/yield hook called while waiting. |

### Per-Wait Instance

| Field | Type | Rules |
|-------|------|-------|
| `deadline_us` | `uint64_t` | Valid only when `now_us` is registered. |
| `polls_remaining` | `uint32_t` | Always decremented as a failsafe. |
| `kind` | enum | Command acceptance, operation completion, data availability, PHY, transfer, or abort. |

### Deadline Invariants

- A wait exits when the condition succeeds, transport status fails, monotonic deadline expires, or fallback polls exhaust.
- Sn_CR clearing is command acceptance, not operation completion; completion uses Sn_IR or Sn_SR.
- Exhaustion returns an explicit error and never falls through to success.

## RP2040 Transport Instance

Only one instance may be active because the root callback table is a singleton.

| Field | Type | Rules |
|-------|------|-------|
| `state` | enum | `FREE`, `OPENING`, `READY`, `TRANSFERRING`, `SLEEPING`, `FAULTED`, or `CLOSING`. |
| `config` | copied `wiznet_spi_config_t` | Owned by the instance; caller lifetime is irrelevant after open. |
| `handle_published` | boolean | True only in READY/SLEEPING/FAULTED after complete allocation. |
| `pio` | PIO identifier | Valid only while program/SM ownership is true. |
| `pio_sm` | signed identifier | `-1` unless claimed. |
| `pio_offset` | signed identifier | `-1` unless program installed. |
| `dma_out` | signed identifier | `-1` unless claimed. |
| `dma_in` | signed identifier | `-1` unless claimed. |
| `owned_resources` | bit mask | Program, SM, DMA-out, DMA-in, GPIO, raw IRQ. |
| `header` | three-byte array | Count is either zero or exactly three; cleared on every lifecycle/error transition. |
| `last_error` | signed status | Sticky `PICO_OK` or a concrete timeout, I/O, resource, argument, or state error. |
| `transfer_timeout_us` | `uint32_t` | Zero in input selects the documented 100 ms default. |
| `abort_timeout_us` | `uint32_t` | Zero in input selects the documented 1 ms default. |

### Transport Transitions

```text
FREE --------open begins----------------------> OPENING
OPENING -----all resources/config succeed----> READY
OPENING -----any claim/config failure--------> FREE after reverse unwind
READY -------transfer starts-----------------> TRANSFERRING
TRANSFERRING-completion verified-------------> READY
TRANSFERRING-timeout/AHB/PIO error-----------> FAULTED
READY -------sleep----------------------------> SLEEPING
SLEEPING ----wake/reinitialize SM------------> READY
READY/SLEEPING/FAULTED --close---------------> CLOSING
CLOSING -----concurrent close waits----------> observe owner's FREE/FAULTED result
CLOSING -----all active resources retire-----> FREE
CLOSING -----DMA remains BUSY-----------------> FAULTED with channel quarantined
FAULTED -----explicit recover succeeds-------> READY
```

### Resource Invariants

- The state slot and public handle are published only after complete allocation.
- Exactly one lifecycle owner may enter CLOSING. A concurrent close serializes on
  the bus mutex, then applies idempotent close/retry rules to the resulting FREE
  or FAULTED state; it never runs cleanup concurrently with the owner.
- PIO program and state machine are claimed through the SDK combined operation.
- Unwind releases only resources recorded as owned and releases them in reverse order.
- A DMA channel is never unclaimed while BUSY.
- Partial RX/TX data is discarded after timeout or abort.
- `active_state` is cleared on close and never references a FREE slot.
- Close, sleep, wake, reset, recover, and transfer are serialized by the bus mutex.

## Callback Registration State

### Root Callback Categories

| Category | Required members | Readiness rule |
|----------|------------------|----------------|
| CRIS/bus | enter + exit | Pair installed together. |
| Chip select | select + deselect | Pair installed together. |
| Byte SPI | read + write | Pair installed together. |
| Burst SPI | read + write | Pair or documented byte fallback. |
| SPI status | busy + get error + clear error | Independent sibling storage, never union-aliased. |
| Socket locks | enter + exit | Pair installed together. |
| Global lock | enter + exit | Pair installed together. |
| Time | now + optional wait hook | Monotonic clock determines real deadlines. |

### GPIO Registrations

One record exists per socket:

| Field | Type | Rules |
|-------|------|-------|
| `registered` | boolean | False until all validation and masks succeed. |
| `event_mask` | `sockint_kind` | Only supported W5500 socket events. |
| `callback` | function pointer | Non-null while registered. |
| `context` | `void *` | Passed unchanged during task-context dispatch. |

Global GPIO state contains the registered-socket bit mask, pending flag, raw-handler ownership, and enabled edge mask.

### GPIO Invariants

- The raw ISR handles and acknowledges only `PIN_INT`, records pending state, disables the edge, and returns.
- No SPI, blocking lock, or user callback executes in ISR context.
- Dispatch invokes only sockets present in both SIR and the registration mask and only for events present in that socket's mask.
- Registration merges hardware masks; unregister removes only the caller's ownership.

## Device Configuration State

| Field group | Protection | Coherence rule |
|-------------|------------|----------------|
| TX/RX capacity per socket | Global lock during layout, socket lock during consumption | Hardware write, readback, and byte cache change as one successful operation. |
| `sock_mode` and packet state | Socket lock | Cleared on close/reset. |
| Network identity + DNS/DHCP | Global lock | Set/get sees one old or one new logical value, never a mixture. |
| RTR/RCR tuple | Global lock | Both fields change as one logical transaction. |
| Interrupt masks | Global lock | Registration merges and unregister removes owned bits only. |
| PHYCFGR | Global lock + bus mutex | Read-modify-write preserves unrelated bits and RST returns high. |
| 16-bit registers | Bus mutex, one VDM frame | No independently locked high/low byte writes. |

## Audit Evidence Record

One record exists for every historical `AUD-001` through `AUD-073` and every current category `CUR-001` through `CUR-019`.

| Field | Type | Rules |
|-------|------|-------|
| `finding_id` | string | Unique historical ID or stable current-category ID. |
| `requirements` | list | At least one FR identifier. |
| `root_revision` | Git SHA + state | Exact 40-digit root revision and clean/dirty state used. |
| `transport_revision` | Git SHA + state/scope | Exact nested revision and state, or verified root-only scope. |
| `method` | set of enums | One or more accepted host, sanitizer, TSan, formal, static, cross-compile, smoke-hardware, or full-diagnostic methods. |
| `command` | string | Reproducible command, with environment prerequisites. |
| `evidence_location` | path/reference | Log, test, or report location. |
| `result` | enum | `PASS`, `FAIL`, `BLOCKED`, `NOT_APPLICABLE`, or `SUPERSEDED`. |
| `observed_at` | timestamp | UTC evidence time. |
| `superseded_by` | optional ID | Required for `SUPERSEDED`. |
| `scope_reason` | optional text | Required for `NOT_APPLICABLE`. |
| `release_blocking` | boolean | True for every current result not equal to PASS and every historical FAIL/BLOCKED or invalid scope/supersession claim. |

### Evidence Transitions

```text
UNVERIFIED -> PASS
UNVERIFIED -> FAIL
UNVERIFIED -> BLOCKED
UNVERIFIED -> NOT_APPLICABLE with verified scope reason
UNVERIFIED -> SUPERSEDED with replacement finding
FAIL/BLOCKED -> PASS only after a fresh command and recorded evidence
```

### Release Invariant

Release readiness is true only when every `CUR-001` through `CUR-019` row has PASS evidence, all 73 historical IDs are PASS or validly reconciled under FR-027, no required hardware result is blocked, and no document claims a stronger state than the canonical evidence record.
