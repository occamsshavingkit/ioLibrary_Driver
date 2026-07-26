# Contract: W5500 Core Driver

## Scope

This contract governs public and integration-facing behavior in `Ethernet/socket.*`, `Ethernet/wizchip_conf.*`, and `Ethernet/W5500/w5500.*` for the audited W5500 path. Shared interfaces must continue to compile for W6300.

## Compatibility Rules

- Existing byte, burst, chip-select, and CRIS callback signatures remain unchanged.
- Existing successful socket return values and existing negative error values retain their numeric values.
- New status, time, lifecycle, and GPIO contracts are additive except for state-changing `void` helpers upgraded to return status; ordinary C call statements that ignore the result remain source-compatible.
- All root and port objects must be rebuilt together because `_WIZCHIP` layout moves SPI status out of the interface union.
- False success, indefinite wait, null dereference, lock leak, and acceptance of undocumented flags are not compatibility behavior.

## Error Results

Add after the existing `SOCKERR_DEADLINE` definition:

```c
#define SOCKERR_IO        (SOCK_ERROR - 17)
#define SOCKERR_NOTREADY  (SOCK_ERROR - 18)
```

| Result | Meaning |
|--------|---------|
| `SOCKERR_DEADLINE` | A configured host deadline or bounded fallback expired. |
| `SOCKERR_IO` | The transport reported timeout, PIO/DMA, or bus failure. |
| `SOCKERR_NOTREADY` | The requested operation is not allowed in the root or socket's current lifecycle state. |

No operation may translate these values to `SOCK_OK` or a positive byte count.
Status, bounded close, and explicit chip reset remain allowed recovery operations
for a faulted socket and therefore do not fail merely because health is FAULTED.

## Independent SPI Status Registration

`SPI_STATUS` becomes a sibling of `_WIZCHIP.IF`, not a union member.

```c
typedef struct {
    uint8_t (*_check_busy)(void);
    int8_t  (*_get_error)(void);
    void    (*_clear_error)(void);
} wizchip_spi_status_t;

void reg_wizchip_spistatus_cbfunc(
    uint8_t (*check_busy)(void),
    int8_t (*get_error)(void),
    void (*clear_error)(void));
```

Registration rules:

- All three callbacks install as one set.
- A null or partial set restores safe default callbacks and marks status readiness false.
- Installing status callbacks cannot alter byte or burst callback addresses.
- Transfer code clears status before starting, checks busy/error before completing, and makes the first failure sticky.

## Time and Deadline Registration

```c
typedef uint64_t (*wizchip_now_us_cb_t)(void);
typedef void (*wizchip_wait_hook_cb_t)(void);

typedef struct {
    uint32_t command_timeout_us;
    uint32_t operation_timeout_us;
    uint32_t phy_timeout_us;
} wizchip_timeout_config_t;

void reg_wizchip_time_cbfunc(
    wizchip_now_us_cb_t now_us,
    wizchip_wait_hook_cb_t wait_hook);

int8_t wizchip_set_timeout_config(
    const wizchip_timeout_config_t *config);
int8_t wizchip_get_timeout_config(
    wizchip_timeout_config_t *config);
```

Validation rules:

- `config` is non-null and all timeout fields are nonzero.
- RP2040 registers a monotonic microsecond source.
- Without a registered clock, every wait remains bounded by `_WIZCHIP_POLL_MAX_`, but this compatibility path has no portable wall-clock guarantee and is not valid release timing evidence.
- Every release-acceptance platform registers and verifies a monotonic clock callback before entering READY.
- A network completion deadline is never shorter than the current RTR/RCR retransmission window plus the documented margin.

## Root Lifecycle

```c
typedef enum {
    WIZCHIP_STATE_UNINITIALIZED = 0,
    WIZCHIP_STATE_READY,
    WIZCHIP_STATE_RESETTING,
    WIZCHIP_STATE_FAULTED
} wizchip_state_t;

wizchip_state_t wizchip_get_state(void);
int8_t wizchip_get_last_error(void);
void wizchip_clear_last_error(void);
int8_t wizchip_recover(void);
```

- A transport failure transitions READY to FAULTED and preserves the error.
- Ordinary hardware operations reject UNINITIALIZED, RESETTING, and FAULTED.
- `wizchip_clear_last_error()` does not make a faulted device READY; only verified recovery/reinitialization does.

## Lock Registration and Ordering

Publish the existing implementation in the public header:

```c
void reg_wizchip_lock_cbfunc(
    void (*sock_enter)(uint8_t sn),
    void (*sock_exit)(uint8_t sn),
    void (*global_enter)(void),
    void (*global_exit)(void));
```

Rules:

- Socket/global enter and exit functions register only as valid pairs.
- Locks are non-recursive.
- Registration occurs during single-threaded initialization.
- Total acquisition order is global, socket locks in ascending socket order, SPI bus mutex, then any short interrupt-state protection.
- Normal socket operations never acquire the global lock while holding a socket lock.
- No user callback executes while a socket, global, or SPI lock is held unless the callback is explicitly the corresponding lock/transport primitive.

## Checked Low-Level Transactions

The checked implementation is the canonical path; legacy data-returning wrappers delegate to it.

```c
int8_t wizchip_read8_checked(uint32_t address, uint8_t *value);
int8_t wizchip_write8_checked(uint32_t address, uint8_t value);
int8_t wizchip_read_buf_checked(
    uint32_t address, uint8_t *buffer, uint16_t length);
int8_t wizchip_write_buf_checked(
    uint32_t address, const uint8_t *buffer, uint16_t length);
```

- Null buffer with nonzero length returns `SOCKERR_ARG`.
- Zero length returns success without callbacks, CS, locks, or buffer access.
- A busy or failed transport returns `SOCKERR_IO` or `SOCKERR_DEADLINE` and faults the root lifecycle.
- Every 16-bit setter uses one checked VDM buffer write.

## Socket Operation Contract

### Validation and Locking

- Validate socket number, null pointers, lengths, protocol, port, address, and immutable flag dependencies before socket lock acquisition.
- Revalidate hardware/cache state that can change concurrently after acquiring the socket lock.
- A lock-taking public socket operation never calls another lock-taking public operation.
- Every locked operation has one cleanup exit and one unlock.

### Zero Length

- `send`, `recv`, `sendto`, and `recvfrom` return `0` for length zero.
- The return occurs before validating or dereferencing the data buffer; a null data buffer is therefore valid only for length zero.
- No socket pointer, packet state, CS line, or callback changes.

### Flags

Input `protocol` contains only a supported low-nibble mode. Input `flag` accepts `SF_IO_NONBLOCK` for every supported mode plus:

| Mode | Hardware flag mask |
|------|--------------------|
| TCP | `SF_TCP_NODELAY` |
| UDP | `SF_MULTI_ENABLE \| SF_IGMP_VER2 \| SF_BROAD_BLOCK \| SF_UNI_BLOCK` |
| MACRAW | `SF_ETHER_OWN \| SF_BROAD_BLOCK \| SF_MULTI_BLOCK \| SF_IPv6_BLOCK` |
| IPRAW | `0` |

- `SF_IO_NONBLOCK` is software state and is never written into Sn_MR.
- `SF_IGMP_VER2` and `SF_UNI_BLOCK` require `SF_MULTI_ENABLE`.
- MACRAW is accepted only on socket 0.
- Unknown bits return `SOCKERR_SOCKFLAG` before side effects.

### Fault Recovery

- Failure before command acceptance returns error without publishing success.
- Failure after acceptance or pointer mutation marks the socket faulted.
- A faulted socket rejects ordinary data/lifecycle operations except status, bounded close, or explicit chip reset.
- Close reports success only after hardware CLOSED is observed and software state/cache is cleared.
- If hardware is already CLOSED, bounded close clears the software state and succeeds without issuing another command.
- If the close command is not accepted or CLOSED is not observed by the deadline, close returns the concrete deadline/I/O error and leaves the socket FAULTED with its prior state preserved for retry or chip reset.

## PHY Contract

State-changing PHY functions return `int8_t` status:

```c
int8_t wizphy_reset(void);
int8_t wizphy_powerdown(void);
int8_t wizphy_powerup(void);
int8_t wizphy_setphyconf(const wiz_PhyConf *phyconf);
int8_t wizphy_getphyconf(wiz_PhyConf *phyconf);
int8_t wizphy_getphystat(wiz_PhyConf *phyconf);
```

- Null arguments return failure before hardware access.
- Power/mode changes preserve unrelated PHYCFGR bits.
- PHYCFGR.RST is low only during reset and is restored high.
- The 200 microsecond hold is an engineering default implemented with the registered clock/delay path, not an empty loop.
- Completion requires exact masked readback before the PHY deadline.
- Link callbacks receive the actual sampled `PHY_LINK_ON` or `PHY_LINK_OFF` value.

## Configuration and Cache Contract

- Network identity, DNS/DHCP cache, timeout tuple, interrupt masks, memory layout, reset restoration, and PHY read-modify-write are global-lock transactions.
- Buffer capacity setters update hardware and cache only on successful checked write/readback.
- Close clears cached protocol and per-socket packet state.
- Verified reset clears socket state and repopulates buffer caches.
- Sn_TX_FSR/Sn_RX_RSR stable reads return explicit failure if stability is not reached by deadline.
