# Contract: RP2040 W5500 Transport and GPIO

## Scope

This contract governs the W55RP20/RP2040 path under `WIZnet-PICO-C/port/ioLibrary_Driver/`. The accepted runtime uses Pico SDK 2.2.0 and one active transport instance.

## Transport Configuration

Append timeout fields to the W5500 `wiznet_spi_config_t` without reordering current fields:

```c
typedef struct wiznet_spi_config {
    uint8_t data_in_pin;
    uint8_t data_out_pin;
    uint8_t cs_pin;
    uint8_t clock_pin;
    uint8_t irq_pin;
    uint8_t reset_pin;
    uint16_t clock_div_major;
    uint8_t clock_div_minor;
    uint8_t spi_hw_instance;
    uint32_t transfer_timeout_us;
    uint32_t abort_timeout_us;
} wiznet_spi_config_t;
```

- A zero transfer timeout selects 100,000 microseconds.
- A zero abort timeout selects 1,000 microseconds.
- Pin identifiers and clock divider must be valid before resource claims or GPIO changes.
- Configuration is copied into transport state during open.

## Lifecycle and Status Types

```c
typedef enum {
    WIZNET_SPI_FREE = 0,
    WIZNET_SPI_OPENING,
    WIZNET_SPI_READY,
    WIZNET_SPI_TRANSFERRING,
    WIZNET_SPI_SLEEPING,
    WIZNET_SPI_FAULTED,
    WIZNET_SPI_CLOSING
} wiznet_spi_state_t;

int wiznet_spi_pio_open_ex(
    const wiznet_spi_config_t *config,
    wiznet_spi_handle_t *handle_out);
int wiznet_spi_pio_close_ex(wiznet_spi_handle_t handle);
int wiznet_spi_pio_sleep_ex(wiznet_spi_handle_t handle);
int wiznet_spi_pio_wake_ex(wiznet_spi_handle_t handle);
int wiznet_spi_pio_recover(wiznet_spi_handle_t handle);
int wiznet_spi_pio_get_last_error(wiznet_spi_handle_t handle);
void wiznet_spi_pio_clear_last_error(wiznet_spi_handle_t handle);
wiznet_spi_state_t wiznet_spi_pio_get_state(
    wiznet_spi_handle_t handle);
```

Pico result conventions apply:

- `PICO_OK`
- `PICO_ERROR_INVALID_ARG`
- `PICO_ERROR_INSUFFICIENT_RESOURCES`
- `PICO_ERROR_RESOURCE_IN_USE`
- `PICO_ERROR_INVALID_STATE`
- `PICO_ERROR_TIMEOUT`
- `PICO_ERROR_IO`

The existing `wiznet_spi_pio_open()` and vtable data/lifecycle members remain compatibility wrappers. The open wrapper returns either a fully READY handle or null. Legacy void vtable operations are invoked only through the root checked transaction adapter, which reads the same sticky status before returning an explicit socket error to the initiating public operation. New direct transport callers use the `_ex` APIs; no high-level initiating operation may rely on diagnostic output or an unchecked void callback for its result.

## Transactional Open

- Only one instance may leave FREE; another open returns `PICO_ERROR_RESOURCE_IN_USE`.
- Reserve state, copy configuration, and initialize every resource sentinel before claims.
- Claim the PIO program/state machine through `pio_claim_free_sm_and_add_program()`.
- Claim DMA-out and DMA-in without panic.
- Configure GPIO/SM after all claims succeed.
- Publish `handle_out` only after READY.
- Any failure unwinds owned resources in reverse order and restores FREE.

No failure may leave a published handle, occupied slot marker, changed `active_state`, or untracked resource.

## Transfer Contract

- READY is required before transfer.
- Zero-length transfer returns `PICO_OK` without state, framing, DMA, PIO, CS, or buffer activity.
- Nonzero transfer requires non-null buffers and framing arithmetic that cannot overflow.
- Transfer owns the bus mutex and transitions READY to TRANSFERRING.
- Completion requires DMA BUSY clear, no DMA read/write/AHB error, and required PIO completion before the absolute deadline.
- Success clears temporary framing and returns to READY.
- Failure deasserts CS, disables PIO, discards partial data, records sticky error, and enters FAULTED.
- W5500 transfers do not exceed the configured socket capacity of 16 KiB.

## Bounded DMA Abort and Quarantine

- On failure, write the channel bit to `dma_hw->abort` and poll that channel BUSY bit against `abort_timeout_us`.
- Disable channel completion IRQ before abort and clear possible RP2040-E13 spurious completion state afterward.
- Retired channels may be cleaned, reused by recover, or unclaimed by close.
- A channel still BUSY at the abort deadline remains claimed and is recorded as quarantined.
- Close/recover returns `PICO_ERROR_TIMEOUT` while quarantine remains; it never unclaims the active channel.
- The driver never resets the global DMA block.

## Close, Sleep, Wake, and Recover

| Operation | Allowed states | Resulting behavior |
|-----------|----------------|--------------------|
| Close | Any valid state | Idempotent for FREE; serialize, retire resources, clear active/header/state, then FREE. |
| Sleep | READY | Disable clean SM after current bounded transfer; retain claims; enter SLEEPING. |
| Sleep | SLEEPING | Return `PICO_OK` without side effects. |
| Wake | SLEEPING | Clear/restart SM into known disabled-ready configuration; enter READY. |
| Wake | READY | Return `PICO_OK` without side effects. |
| Recover | FAULTED | Retire DMA, clear FIFO/framing/errors, restore SM; enter READY only after verification. |
| Transfer | SLEEPING/FAULTED/CLOSING/FREE | Return `PICO_ERROR_INVALID_STATE`. |

The bus mutex serializes lifecycle operations with transfers. No lifecycle call unclaims resources asynchronously from an active transfer.

## Root Status Adapter

The port registers these independent callbacks after successful open:

```c
static uint8_t wizchip_transport_busy(void);
static int8_t wizchip_transport_get_error(void);
static void wizchip_transport_clear_error(void);
```

- Busy is true only in TRANSFERRING, OPENING, or CLOSING.
- Get-error returns the sticky transport result mapped into the root signed error range.
- Clear-error clears the value only when the transport is not FAULTED; recovery is required to leave FAULTED.

## High-Level Port API

Upgrade high-level operations to return status and require callers/examples to check it:

```c
int wizchip_spi_initialize(void);
int wizchip_cris_initialize(void);
int wizchip_reset(void);
int wizchip_initialize(void);
int wizchip_check(void);
int wizchip_close(void);
int wizchip_sleep(void);
int wizchip_wake(void);
int wizchip_recover(void);
```

- Initialization is idempotent and does not claim a second mutex, PIO SM, program, or DMA channel.
- `wizchip_initialize()` rejects a null/not-ready transport before dereferencing the handle.
- Initialization registers bus/global/socket locks, time callbacks, data callbacks, status callbacks, and CS callbacks as one ordered setup.
- Link waiting is deadline-bound and returns timeout instead of looping forever.

## Bus and Lock Adapter

- Use a Pico non-recursive `mutex_t` for complete SPI-frame serialization.
- Initialize one bus mutex, one global mutex, and eight socket mutexes exactly once.
- Register them through `reg_wizchip_cris_cbfunc()` and `reg_wizchip_lock_cbfunc()`.
- Use a separate short critical section for transport state shared with IRQ context.
- No SPI callback runs in GPIO ISR context.

## GPIO Registration and Dispatch

```c
typedef void (*wizchip_gpio_irq_cb_t)(
    uint8_t sn,
    sockint_kind events,
    void *context);

int wizchip_gpio_interrupt_register(
    uint8_t sn,
    sockint_kind event_mask,
    wizchip_gpio_irq_cb_t callback,
    void *context);
int wizchip_gpio_interrupt_unregister(uint8_t sn);
bool wizchip_gpio_interrupt_pending(void);
int wizchip_gpio_interrupt_dispatch(void);

int wizchip_gpio_interrupt_initialize(
    uint8_t sn,
    void (*legacy_callback)(void));
```

Registration rules:

- Reject invalid socket, null callback, empty/unsupported mask, or unavailable transport before hardware access.
- Merge socket/common masks with existing owned masks; do not replace unrelated registrations.
- Install one raw GPIO handler for `PIN_INT` when the first registration succeeds.
- The compatibility initializer returns status and adapts one no-argument callback to the same per-socket dispatch table.

Raw ISR rules:

- Check only `PIN_INT` and the configured falling-edge event.
- Acknowledge the raw event, set pending, disable the edge, and return.
- Do not touch W5500 registers, locks, or user callbacks.

Dispatch rules:

- Run in task context.
- Read SIR and only asserted registered sockets.
- Read Sn_IR, intersect with that registration's mask, clear only dispatched write-1-to-clear bits, and invoke the matching callback with socket, event bits, and context.
- Drain the W5500 INT line before acknowledging/rearming the GPIO edge.
- Unregister removes only that socket's owned mask/callback. Removing the final registration disables the edge and removes the raw handler.

## Build Contract

- The production CMake target `IOLIBRARY_FILES` links `pico_sync`, `pico_time`, `hardware_gpio`, `hardware_pio`, `hardware_dma`, and `hardware_clocks` as required by the implementation.
- The planned host-test CMake project accepts `IOLIBRARY_ROOT` as the root checkout path and links the production sources directly; `IOLIBRARY_ROOT` is not the production target name.
- W55RP20/W5500 acceptance compiles with C11, `-Wall -Wextra -Werror -pedantic` for touched sources.
- Shared W6300 branches compile without changing W6300-only runtime semantics.
- Do not suppress maybe-uninitialized or all transport warnings in acceptance targets.
