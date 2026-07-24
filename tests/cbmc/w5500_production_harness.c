/*
 * CBMC harness for the production W5500 driver.
 *
 * This analyzes w5500.c, wizchip_conf.c, and socket.c directly. It does not
 * use or reproduce the supplemental socket-state model in test_cbmc_model.c.
 * Hardware data and elapsed time are nondeterministic within the assumptions
 * below; CBMC's pointer checks cover every dereference in the production path.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "socket.h"
#include "wizchip_conf.h"

uint8_t nondet_uint8_t(void);
uint16_t nondet_uint16_t(void);
void __CPROVER_assert(_Bool condition, const char *description);
void __CPROVER_assume(_Bool condition);
void __CPROVER_havoc_object(void *object);

static unsigned int lock_acquires;
static unsigned int lock_releases;
static unsigned int io_lock_acquires;
static unsigned int io_lock_releases;
static uint8_t socket_lock_held[_WIZCHIP_SOCK_NUM_];
static uint8_t global_lock_held;
static uint8_t io_lock_held;
static uint64_t monotonic_us;

static void socket_lock(uint8_t sn)
{
    __CPROVER_assert(sn < _WIZCHIP_SOCK_NUM_,
                     "socket lock index is in range");
    __CPROVER_assert(socket_lock_held[sn] == 0U,
                     "socket lock is not acquired recursively");
    socket_lock_held[sn] = 1U;
    ++lock_acquires;
}

static void socket_unlock(uint8_t sn)
{
    __CPROVER_assert(sn < _WIZCHIP_SOCK_NUM_,
                     "socket unlock index is in range");
    __CPROVER_assert(socket_lock_held[sn] == 1U,
                     "socket unlock follows an acquire");
    socket_lock_held[sn] = 0U;
    ++lock_releases;
}

static void global_lock(void)
{
    __CPROVER_assert(global_lock_held == 0U,
                     "global lock is not acquired recursively");
    global_lock_held = 1U;
    ++lock_acquires;
}

static void global_unlock(void)
{
    __CPROVER_assert(global_lock_held == 1U,
                     "global unlock follows an acquire");
    global_lock_held = 0U;
    ++lock_releases;
}

static void io_lock(void)
{
    __CPROVER_assert(io_lock_held == 0U,
                     "SPI critical section is not recursive");
    io_lock_held = 1U;
    ++io_lock_acquires;
}

static void io_unlock(void)
{
    __CPROVER_assert(io_lock_held == 1U,
                     "SPI critical-section exit follows entry");
    io_lock_held = 0U;
    ++io_lock_releases;
}

static void chip_select(void) {}
static void chip_deselect(void) {}

static uint8_t spi_read_byte(void)
{
    return nondet_uint8_t();
}

static void spi_write_byte(uint8_t value)
{
    (void)value;
}

static void spi_read_burst(uint8_t *buffer, uint16_t length)
{
    __CPROVER_assert(buffer != NULL || length == 0U,
                     "nonempty SPI read has a valid destination");
    if (length != 0U) {
        __CPROVER_havoc_object(buffer);
    }
}

static void spi_write_burst(uint8_t *buffer, uint16_t length)
{
    __CPROVER_assert(buffer != NULL || length == 0U,
                     "nonempty SPI write has a valid source");
}

static uint8_t spi_not_busy(void)
{
    return 0U;
}

static uint8_t spi_no_error(void)
{
    return 0U;
}

static void spi_clear_error(void) {}

static uint64_t time_now(void)
{
    uint8_t increment = nondet_uint8_t();

    __CPROVER_assume(increment >= 1U && increment <= 4U);
    monotonic_us += increment;
    return monotonic_us;
}

static void wait_us(uint64_t delay_us)
{
    __CPROVER_assume(delay_us <= 1U);
    monotonic_us += delay_us;
}

static void assert_valid_state(void)
{
    wizchip_state_t state = wizchip_get_state();

    __CPROVER_assert(state == WIZCHIP_STATE_UNINIT ||
                     state == WIZCHIP_STATE_READY ||
                     state == WIZCHIP_STATE_FAULTED,
                     "chip lifecycle state is valid");
}

int main(void)
{
    uint8_t sn = nondet_uint8_t();
    uint16_t length = nondet_uint16_t();
    uint16_t local_port = nondet_uint16_t();
    uint16_t remote_port = nondet_uint16_t();
    uint8_t payload[4];
    uint8_t peer[4];
    uint16_t received_port = 0U;
    int8_t open_result;
    int32_t send_result;
    int32_t receive_result;
    int8_t close_result;

    __CPROVER_assume(sn < _WIZCHIP_SOCK_NUM_);
    __CPROVER_assume(length >= 1U && length <= sizeof(payload));
    __CPROVER_assume(local_port != 0U);
    __CPROVER_assume(remote_port != 0U);
    __CPROVER_havoc_object(payload);
    __CPROVER_havoc_object(peer);
    __CPROVER_assume(peer[0] != 0U || peer[1] != 0U ||
                     peer[2] != 0U || peer[3] != 0U);

    reg_wizchip_cris_cbfunc(io_lock, io_unlock);
    reg_wizchip_cs_cbfunc(chip_select, chip_deselect);
    reg_wizchip_spi_cbfunc(spi_read_byte, spi_write_byte);
    reg_wizchip_spiburst_cbfunc(spi_read_burst, spi_write_burst);
    reg_wizchip_spistatus_cbfunc(spi_not_busy, spi_no_error,
                                 spi_clear_error);
    __CPROVER_assert(reg_wizchip_lock_cbfunc(socket_lock, socket_unlock,
                                             global_lock, global_unlock) == 0,
                     "lock callbacks register successfully");
    reg_wizchip_time_cbfunc(time_now, wait_us);

    wizchip_mark_faulted();
    __CPROVER_assert(wizchip_recover() == SOCK_OK,
                     "harness enters the ready lifecycle state");
    __CPROVER_assert(wizchip_get_state() == WIZCHIP_STATE_READY,
                     "socket operations start from ready");

    wizchip_txmax_cache[sn] = sizeof(payload);
    wizchip_rxmax_cache[sn] = sizeof(payload);

    open_result = socket(sn, Sn_MR_UDP, local_port, SF_IO_NONBLOCK);
    assert_valid_state();
    if (open_result >= 0) {
        __CPROVER_assert(open_result == (int8_t)sn,
                         "successful open returns the requested socket");
        __CPROVER_assert(wizchip_get_state() == WIZCHIP_STATE_READY,
                         "successful open preserves ready state");
    }

    send_result = sendto(sn, payload, length, peer, remote_port);
    assert_valid_state();
    receive_result = recvfrom(sn, payload, length, peer, &received_port);
    assert_valid_state();
    close_result = close(sn);
    assert_valid_state();

    if (close_result == SOCK_OK) {
        __CPROVER_assert(wizchip_get_state() == WIZCHIP_STATE_READY,
                         "successful close preserves ready state");
    }
    __CPROVER_assert(lock_acquires == lock_releases,
                     "socket and global lock counts balance");
    __CPROVER_assert(io_lock_acquires == io_lock_releases,
                     "SPI critical-section counts balance");
    __CPROVER_assert(global_lock_held == 0U && io_lock_held == 0U &&
                     socket_lock_held[sn] == 0U,
                     "all locks are released at harness exit");

    (void)send_result;
    (void)receive_result;
    return 0;
}
