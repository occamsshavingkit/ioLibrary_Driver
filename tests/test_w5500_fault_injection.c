#include <stdint.h>
#include <stdio.h>

#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"

#if !defined(_WIZCHIP_POLL_MAX_)
#error "_WIZCHIP_POLL_MAX_ must be defined"
#endif

static unsigned int failures;
static uint64_t fake_now_us;
static unsigned int wait_hook_calls;
static uint16_t spi_addr;
static uint16_t spi_transaction_addr;
static uint8_t spi_header_bytes;
static uint8_t spi_block;
static uint16_t fake_rtr;
static uint8_t fake_rcr;
static uint16_t fake_tx_fsr;
static uint16_t fake_rx_rsr;
static unsigned int fake_tx_fsr_samples;
static unsigned int fake_rx_rsr_samples;
static unsigned int fake_tx_fsr_reads;
static unsigned int fake_rx_rsr_reads;
static unsigned int fake_sn_mr_reads;
static unsigned int fake_sn_sr_reads;
static uint8_t fake_tx_fsr_unstable;
static uint8_t fake_rx_rsr_unstable;
static uint16_t fake_fault_addr;
static uint8_t fake_fault_enabled;
static uint8_t fake_sn_mode;
static uint8_t fake_sn_command;
static uint8_t fake_sn_status;
static uint8_t fake_sn_ir;
static uint8_t fake_delayed_command;
static uint8_t fake_pending_status;
static uint64_t fake_command_accept_at_us;
static uint64_t fake_status_transition_at_us;
static uint64_t fake_command_accept_delay_us;
static uint64_t fake_status_transition_delay_us;
static uint64_t fake_spi_tick_us;
static unsigned int fake_open_commands;
static unsigned int fake_close_commands;
static uint8_t fake_tx_size[_WIZCHIP_SOCK_NUM_];
static uint8_t fake_rx_size[_WIZCHIP_SOCK_NUM_];

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL [%d]: %s\n", __LINE__, (message)); \
    } \
} while (0)

static uint64_t fake_now(void)
{
    return fake_now_us;
}

static void fake_wait_hook(void)
{
    ++wait_hook_calls;
}

static void fake_critical_enter(void) {}
static void fake_critical_exit(void) {}

static void fake_select(void)
{
    spi_addr = 0;
    spi_transaction_addr = 0;
    spi_header_bytes = 0;
    spi_block = 0u;
}

static void fake_deselect(void) {}

static int fake_socket_number(void)
{
    if (spi_block < WIZCHIP_SREG_BLOCK(0u) ||
        ((spi_block - WIZCHIP_SREG_BLOCK(0u)) % 4u) != 0u) {
        return -1;
    }
    return (int)((spi_block - WIZCHIP_SREG_BLOCK(0u)) / 4u);
}

static uint8_t fake_command_status(uint8_t command)
{
    if (command == Sn_CR_OPEN) {
        switch (fake_sn_mode & 0x0Fu) {
        case Sn_MR_TCP:
            return SOCK_INIT;
        case Sn_MR_UDP:
            return SOCK_UDP;
        case Sn_MR_IPRAW:
            return SOCK_IPRAW;
        case Sn_MR_MACRAW:
            return SOCK_MACRAW;
        default:
            return SOCK_CLOSED;
        }
    }
    if (command == Sn_CR_LISTEN) {
        return SOCK_LISTEN;
    }
    return SOCK_CLOSED;
}

static void fake_accept_command(void)
{
    uint8_t command = fake_sn_command;

    fake_sn_command = 0u;
    fake_pending_status = fake_command_status(command);
    if (fake_status_transition_delay_us == 0u) {
        fake_sn_status = fake_pending_status;
        fake_pending_status = 0u;
    } else {
        fake_status_transition_at_us = fake_now_us +
                                       fake_status_transition_delay_us;
    }
}

static uint8_t fake_spi_read(void)
{
    uint8_t value = 0;
    uint16_t sample;
    int sn = fake_socket_number();

    fake_now_us += fake_spi_tick_us;
    if (fake_pending_status != 0u &&
        fake_now_us >= fake_status_transition_at_us) {
        fake_sn_status = fake_pending_status;
        fake_pending_status = 0u;
    }

    if (sn >= 0 && spi_addr == 0x0000u) {
        value = fake_sn_mode;
        ++fake_sn_mr_reads;
    } else if (sn >= 0 && spi_addr == 0x0001u) {
        if (fake_sn_command != 0u &&
            fake_now_us >= fake_command_accept_at_us) {
            fake_accept_command();
        }
        value = fake_sn_command;
    } else if (sn >= 0 && spi_addr == 0x0002u) {
        value = fake_sn_ir;
    } else if (sn >= 0 && spi_addr == 0x0003u) {
        value = fake_sn_status;
        ++fake_sn_sr_reads;
    } else if (spi_block == WIZCHIP_CREG_BLOCK &&
               spi_addr >= 0x000Fu && spi_addr <= 0x0012u) {
        value = (spi_addr == 0x000Fu) ? 192u : 1u;
    } else if (sn >= 0 &&
               (spi_addr == 0x0020u || spi_addr == 0x0021u)) {
        sample = fake_tx_fsr_unstable
                     ? ((fake_tx_fsr_samples & 1u) ? 0x1234u : 0x5678u)
                     : fake_tx_fsr;
        value = (spi_addr == 0x0020u) ? (uint8_t)(sample >> 8)
                                      : (uint8_t)sample;
        ++fake_tx_fsr_reads;
        if (spi_addr == 0x0021u) {
            ++fake_tx_fsr_samples;
        }
    } else if (sn >= 0 &&
               (spi_addr == 0x0026u || spi_addr == 0x0027u)) {
        sample = fake_rx_rsr_unstable
                     ? ((fake_rx_rsr_samples & 1u) ? 0x9ABCu : 0xDEF0u)
                     : fake_rx_rsr;
        value = (spi_addr == 0x0026u) ? (uint8_t)(sample >> 8)
                                      : (uint8_t)sample;
        ++fake_rx_rsr_reads;
        if (spi_addr == 0x0027u) {
            ++fake_rx_rsr_samples;
        }
    } else if (sn >= 0 && spi_addr == 0x001Eu) {
        value = fake_rx_size[sn];
    } else if (sn >= 0 && spi_addr == 0x001Fu) {
        value = fake_tx_size[sn];
    } else if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x0019u) {
        value = (uint8_t)(fake_rtr >> 8);
    } else if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x001Au) {
        value = (uint8_t)fake_rtr;
    } else if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x001Bu) {
        value = fake_rcr;
    } else if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x0039u) {
        value = 0x04u;
    }
    ++spi_addr;
    return value;
}

static void fake_spi_write(uint8_t value)
{
    int sn;

    if (spi_header_bytes == 0u) {
        spi_addr = (uint16_t)value << 8;
        ++spi_header_bytes;
        return;
    }
    if (spi_header_bytes == 1u) {
        spi_addr |= value;
        spi_transaction_addr = spi_addr;
        ++spi_header_bytes;
        return;
    }
    if (spi_header_bytes == 2u) {
        spi_block = (uint8_t)(value >> 3);
        ++spi_header_bytes;
        return;
    }

    sn = fake_socket_number();

    if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x0000u) {
        if (value == MR_RST) {
            fake_sn_mode = 0u;
            fake_sn_command = 0u;
            fake_pending_status = 0u;
            fake_sn_status = SOCK_CLOSED;
        }
    } else if (sn >= 0 && spi_addr == 0x0000u) {
        fake_sn_mode = value;
    } else if (sn >= 0 && spi_addr == 0x0001u) {
        if (value == Sn_CR_OPEN) {
            ++fake_open_commands;
        }
        if (value == fake_delayed_command) {
            fake_sn_command = value;
            fake_command_accept_at_us = fake_now_us +
                                        fake_command_accept_delay_us;
            if (fake_command_accept_delay_us == 0u) {
                fake_accept_command();
            }
        } else if (value == Sn_CR_CLOSE) {
            ++fake_close_commands;
            fake_sn_command = 0u;
            fake_pending_status = 0u;
            fake_sn_status = SOCK_CLOSED;
        } else if (value == Sn_CR_SEND) {
            fake_sn_ir |= Sn_IR_SENDOK;
        } else {
            fake_sn_command = value;
            fake_accept_command();
        }
    } else if (sn >= 0 && spi_addr == 0x0002u) {
        fake_sn_ir &= (uint8_t)~value;
    } else if (sn >= 0 && spi_addr == 0x001Eu) {
        fake_rx_size[sn] = value;
    } else if (sn >= 0 && spi_addr == 0x001Fu) {
        fake_tx_size[sn] = value;
    } else if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x0019u) {
        fake_rtr = (fake_rtr & 0x00FFu) | ((uint16_t)value << 8);
    } else if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x001Au) {
        fake_rtr = (fake_rtr & 0xFF00u) | value;
    } else if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x001Bu) {
        fake_rcr = value;
    }
    ++spi_addr;
}

static uint8_t fake_spi_busy(void)
{
    return 0u;
}

static int8_t fake_spi_error(void)
{
    if (fake_fault_enabled && spi_transaction_addr == fake_fault_addr) {
        return SOCKERR_IO;
    }
    return 0;
}

static void fake_spi_clear_error(void) {}

static void install_register_fake(void)
{
    reg_wizchip_cris_cbfunc(fake_critical_enter, fake_critical_exit);
    reg_wizchip_cs_cbfunc(fake_select, fake_deselect);
    reg_wizchip_spi_cbfunc(fake_spi_read, fake_spi_write);
    reg_wizchip_spiburst_cbfunc(0, 0);
    reg_wizchip_spistatus_cbfunc(fake_spi_busy, fake_spi_error,
                                 fake_spi_clear_error);
}

static void reset_register_fake(void)
{
    wizchip_socket_state_reset();
    fake_tx_fsr = 0u;
    fake_rx_rsr = 0u;
    fake_tx_fsr_samples = 0u;
    fake_rx_rsr_samples = 0u;
    fake_tx_fsr_reads = 0u;
    fake_rx_rsr_reads = 0u;
    fake_sn_mr_reads = 0u;
    fake_sn_sr_reads = 0u;
    fake_tx_fsr_unstable = 0u;
    fake_rx_rsr_unstable = 0u;
    fake_fault_addr = 0u;
    fake_fault_enabled = 0u;
    fake_sn_mode = 0u;
    fake_sn_command = 0u;
    fake_sn_status = SOCK_CLOSED;
    fake_sn_ir = 0u;
    fake_delayed_command = 0u;
    fake_pending_status = 0u;
    fake_command_accept_at_us = 0u;
    fake_status_transition_at_us = 0u;
    fake_command_accept_delay_us = 0u;
    fake_status_transition_delay_us = 0u;
    fake_spi_tick_us = 0u;
    fake_open_commands = 0u;
    fake_close_commands = 0u;
    for (uint8_t sn = 0u; sn < _WIZCHIP_SOCK_NUM_; ++sn) {
        fake_tx_size[sn] = 2u;
        fake_rx_size[sn] = 2u;
    }
    fake_now_us = 0u;
    wait_hook_calls = 0u;
    if (wizchip_get_state() == WIZCHIP_STATE_FAULTED) {
        (void)wizchip_recover();
    }
    wizchip_clear_last_error();
}

static void enable_fake_fault(uint16_t address)
{
    fake_fault_addr = address;
    fake_fault_enabled = 1u;
    wizchip_invalidate_transport_cache();
}

static void test_time_callback_registration(void)
{
    reg_wizchip_time_cbfunc(fake_now, fake_wait_hook);
    reg_wizchip_time_cbfunc(0, 0);
    CHECK(1, "time callback registration is callable");
}

static void test_deadline_helpers(void)
{
    wizchip_deadline_t deadline;
    wizchip_timeout_config_t config = {100u, 200u, 300u};
    wizchip_timeout_config_t invalid = config;
    uint32_t polls;

    invalid.command_timeout_us = 0u;
    CHECK(wizchip_set_timeout_config(&invalid) != 0,
          "zero command timeout is rejected");
    invalid = config;
    invalid.operation_timeout_us = 0u;
    CHECK(wizchip_set_timeout_config(&invalid) != 0,
          "zero operation timeout is rejected");
    invalid = config;
    invalid.phy_timeout_us = 0u;
    CHECK(wizchip_set_timeout_config(&invalid) != 0,
          "zero PHY timeout is rejected");
    CHECK(wizchip_set_timeout_config(&config) == 0,
          "nonzero timeout configuration is accepted");

    fake_now_us = 1000u;
    wait_hook_calls = 0u;
    reg_wizchip_time_cbfunc(fake_now, fake_wait_hook);
    wizchip_deadline_start(&deadline, 50u);
    fake_now_us = 1049u;
    CHECK(wizchip_deadline_poll(&deadline) == SOCK_OK,
          "deadline remains live before absolute expiration");
    CHECK(wait_hook_calls == 1u, "wait hook is called while polling");
    fake_now_us = 1050u;
    CHECK(wizchip_deadline_poll(&deadline) == SOCKERR_DEADLINE,
          "deadline expires at its absolute timestamp");

    fake_now_us = UINT64_MAX - 5u;
    wizchip_deadline_start(&deadline, 10u);
    fake_now_us = 3u;
    CHECK(wizchip_deadline_poll(&deadline) == SOCK_OK,
          "deadline remains live across monotonic wrap");
    fake_now_us = 4u;
    CHECK(wizchip_deadline_poll(&deadline) == SOCKERR_DEADLINE,
          "wrapped deadline expires after the full interval");

    wait_hook_calls = 0u;
    reg_wizchip_time_cbfunc(0, fake_wait_hook);
    wizchip_deadline_start(&deadline, 1u);
    for (polls = 1u; polls < _WIZCHIP_POLL_MAX_; ++polls) {
        CHECK(wizchip_deadline_poll(&deadline) == SOCK_OK,
              "poll fallback remains live before exhaustion");
    }
    CHECK(wizchip_deadline_poll(&deadline) == SOCKERR_DEADLINE,
          "poll fallback reports deadline on exhaustion");
    CHECK(wait_hook_calls == _WIZCHIP_POLL_MAX_,
          "wait hook is called for every fallback poll");
}

static void test_poll_max_is_nonzero(void)
{
    CHECK(_WIZCHIP_POLL_MAX_ > 0u, "_WIZCHIP_POLL_MAX_ is nonzero");
}

static void test_rtr_rcr_operation_timeout_floor(void)
{
    const uint32_t retry_window_us = 4100000u;
    wizchip_timeout_config_t requested = {100u, 1u, 100u};
    wizchip_timeout_config_t actual = {0u, 0u, 0u};

    reg_wizchip_cris_cbfunc(fake_critical_enter, fake_critical_exit);
    reg_wizchip_cs_cbfunc(fake_select, fake_deselect);
    reg_wizchip_spi_cbfunc(fake_spi_read, fake_spi_write);
    reg_wizchip_spiburst_cbfunc(0, 0);

    setRTR(10000u);
    setRCR(3u);
    CHECK(wizchip_set_timeout_config(&requested) == 0,
          "timeout configuration accepts a value below the retry floor");
    CHECK(wizchip_get_timeout_config(&actual) == 0,
          "timeout configuration can be read back");
    CHECK(actual.operation_timeout_us >= retry_window_us,
          "operation timeout covers RTR * (RCR + 1) plus 100 ms");
}

static void test_stable_sample_helpers_exist(void)
{
    int8_t (*tx_reader)(uint8_t, uint16_t *) = getSn_TX_FSR_checked;
    int8_t (*rx_reader)(uint8_t, uint16_t *) = getSn_RX_RSR_checked;

    CHECK(tx_reader != 0, "status-returning stable Sn_TX_FSR helper exists");
    CHECK(rx_reader != 0, "status-returning stable Sn_RX_RSR helper exists");
}

static void test_stable_nonzero_samples_succeed(void)
{
    uint16_t value = 0u;

    reset_register_fake();
    fake_tx_fsr = 0x1234u;
    CHECK(getSn_TX_FSR_checked(0u, &value) == SOCK_OK,
          "stable nonzero Sn_TX_FSR read succeeds");
    CHECK(value == fake_tx_fsr, "stable Sn_TX_FSR value is returned");

    value = 0u;
    fake_rx_rsr = 0x5678u;
    CHECK(getSn_RX_RSR_checked(0u, &value) == SOCK_OK,
          "stable nonzero Sn_RX_RSR read succeeds");
    CHECK(value == fake_rx_rsr, "stable Sn_RX_RSR value is returned");
}

static void test_register_faults_return_io_error(void)
{
    uint16_t value = 0u;

    reset_register_fake();
    enable_fake_fault(0x0020u);
    CHECK(getSn_TX_FSR_checked(0u, &value) == SOCKERR_IO,
          "Sn_TX_FSR transport failure returns SOCKERR_IO");

    reset_register_fake();
    enable_fake_fault(0x0026u);
    CHECK(getSn_RX_RSR_checked(0u, &value) == SOCKERR_IO,
          "Sn_RX_RSR transport failure returns SOCKERR_IO");
}

static void test_stable_zero_is_distinct_from_failure_zero(void)
{
    uint16_t stable_value = UINT16_MAX;
    uint16_t failed_value = 0u;
    int8_t stable_status;
    int8_t failed_status;

    reset_register_fake();
    stable_status = getSn_TX_FSR_checked(0u, &stable_value);
    enable_fake_fault(0x0020u);
    failed_status = getSn_TX_FSR_checked(0u, &failed_value);
    CHECK(stable_status == SOCK_OK && stable_value == 0u,
          "stable zero Sn_TX_FSR is a successful read");
    CHECK(failed_status == SOCKERR_IO && failed_value == 0u,
          "failure zero Sn_TX_FSR carries an error status");

    reset_register_fake();
    stable_value = UINT16_MAX;
    stable_status = getSn_RX_RSR_checked(0u, &stable_value);
    enable_fake_fault(0x0026u);
    failed_status = getSn_RX_RSR_checked(0u, &failed_value);
    CHECK(stable_status == SOCK_OK && stable_value == 0u,
          "stable zero Sn_RX_RSR is a successful read");
    CHECK(failed_status == SOCKERR_IO && failed_value == 0u,
          "failure zero Sn_RX_RSR carries an error status");
}

static void test_unstable_samples_return_deadline(void)
{
    uint16_t value = 0u;

    reset_register_fake();
    reg_wizchip_time_cbfunc(0, fake_wait_hook);
    fake_tx_fsr_unstable = 1u;
    CHECK(getSn_TX_FSR_checked(0u, &value) == SOCKERR_DEADLINE,
          "unstable Sn_TX_FSR samples expire with a deadline error");

    reset_register_fake();
    fake_rx_rsr_unstable = 1u;
    CHECK(getSn_RX_RSR_checked(0u, &value) == SOCKERR_DEADLINE,
          "unstable Sn_RX_RSR samples expire with a deadline error");
}

static void test_macraw_send_skips_fsr_length_check(void)
{
    uint8_t memory[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    uint8_t payload = 0xA5u;
    int32_t result;

    reset_register_fake();
    fake_tx_fsr = 2048u;
    CHECK(wizchip_init(memory, memory) == 0,
          "register fake initializes socket memory");
    CHECK(socket(0u, Sn_MR_MACRAW, 0u, 0u) == 0,
          "MACRAW socket opens on socket zero");
    fake_tx_fsr_reads = 0u;
    result = sendto(0u, &payload, 1u, 0, 0u);
    CHECK(result == 1,
          "MACRAW send does not depend on an FSR-derived length result");
    CHECK(fake_tx_fsr_reads == 0u,
          "MACRAW send performs no Sn_TX_FSR length check");
}

static void prepare_open_listen_test(void)
{
    wizchip_timeout_config_t config = {5000u, 100200u, 5000u};

    reset_register_fake();
    fake_rtr = 1u;
    fake_rcr = 1u;
    CHECK(wizchip_set_timeout_config(&config) == 0,
          "OPEN/LISTEN timeout configuration is accepted");
    if (wizchip_get_state() == WIZCHIP_STATE_FAULTED) {
        CHECK(wizchip_recover() == 0,
              "OPEN/LISTEN tests recover the register fake");
    }
    CHECK(wizchip_get_state() == WIZCHIP_STATE_READY,
          "OPEN/LISTEN register fake is ready");
    reg_wizchip_time_cbfunc(fake_now, fake_wait_hook);
    fake_spi_tick_us = 100u;
}

static void test_socket_and_listen_command_acceptance_wait_is_bounded(void)
{
    int8_t result;
    uint64_t started_us;

    prepare_open_listen_test();
    fake_delayed_command = Sn_CR_OPEN;
    fake_command_accept_delay_us = 3000u;
    started_us = fake_now_us;
    result = socket(0u, Sn_MR_TCP, 5000u, 0u);
    CHECK(result == 0, "OPEN succeeds after bounded command acceptance");
    CHECK(wait_hook_calls > 0u,
          "OPEN command acceptance services the wait hook");
    CHECK(fake_now_us - started_us <= 5500u,
          "OPEN command acceptance completes within its deadline");

    fake_delayed_command = Sn_CR_LISTEN;
    fake_command_accept_delay_us = 3000u;
    wait_hook_calls = 0u;
    fake_now_us = 0u;
    started_us = fake_now_us;
    result = listen(0u);
    CHECK(result == SOCK_OK,
          "LISTEN succeeds after bounded command acceptance");
    CHECK(wait_hook_calls > 0u,
          "LISTEN command acceptance services the wait hook");
    CHECK(fake_now_us - started_us <= 5500u,
          "LISTEN command acceptance completes within its deadline");
}

static void test_unaccepted_sn_cr_expires_deadline_and_returns_error(void)
{
    int8_t result;
    uint64_t started_us;

    prepare_open_listen_test();
    fake_delayed_command = Sn_CR_OPEN;
    fake_command_accept_delay_us = 10000u;
    started_us = fake_now_us;
    result = socket(0u, Sn_MR_UDP, 5000u, 0u);
    CHECK(result == SOCKERR_DEADLINE,
          "unaccepted Sn_CR OPEN returns a deadline error");
    CHECK(fake_now_us - started_us <= 5500u,
          "unaccepted Sn_CR OPEN returns at the configured deadline");
}

static void test_sn_sr_transitions_from_init_to_listen_within_deadline(void)
{
    int8_t result;
    uint64_t started_us;

    prepare_open_listen_test();
    CHECK(socket(0u, Sn_MR_TCP, 5000u, 0u) == 0,
          "TCP socket reaches INIT before LISTEN transition test");
    fake_delayed_command = Sn_CR_LISTEN;
    fake_status_transition_delay_us = 3000u;
    wait_hook_calls = 0u;
    fake_now_us = 0u;
    started_us = fake_now_us;
    result = listen(0u);
    CHECK(result == SOCK_OK,
          "Sn_SR INIT transitions to LISTEN within the deadline");
    CHECK(fake_sn_status == SOCK_LISTEN,
          "LISTEN success is published only after Sn_SR is LISTEN");
    CHECK(wait_hook_calls > 0u,
          "LISTEN state transition services the wait hook");
    CHECK(fake_now_us - started_us <= 100200u,
          "LISTEN state transition completes within operation deadline");
}

static void test_open_succeeds_within_deadline_for_valid_mode(void)
{
    int8_t result;
    uint64_t started_us;

    prepare_open_listen_test();
    fake_delayed_command = Sn_CR_OPEN;
    fake_command_accept_delay_us = 2000u;
    fake_status_transition_delay_us = 3000u;
    started_us = fake_now_us;
    result = socket(0u, Sn_MR_UDP, 5000u, 0u);
    CHECK(result == 0, "OPEN succeeds within deadline for UDP mode");
    CHECK(fake_sn_status == SOCK_UDP,
          "successful UDP OPEN observes the expected Sn_SR state");
    CHECK(wait_hook_calls > 0u,
          "OPEN state transition services the wait hook");
    CHECK(fake_now_us - started_us <= 100200u,
          "valid OPEN completes within operation deadline");
}

static void test_open_socket_mode_queries_use_cached_state(void)
{
    uint8_t flags = 0u;
    uint8_t pack_info = 0u;
    uint16_t remained_size = UINT16_MAX;

    prepare_open_listen_test();
    CHECK(socket(0u, Sn_MR_UDP, 5000u,
                 (uint8_t)(SF_MULTI_ENABLE | SF_IO_NONBLOCK)) == 0,
          "UDP socket opens with cached hardware and I/O flags");
    fake_sn_mr_reads = 0u;

    CHECK(getsockopt(0u, SO_FLAG, &flags) == SOCK_OK,
          "SO_FLAG succeeds for an open socket");
    CHECK(flags == (uint8_t)(SF_MULTI_ENABLE |
                             (SOCK_IO_NONBLOCK << 3)),
          "SO_FLAG combines cached Sn_MR and I/O mode");
    CHECK(getsockopt(0u, SO_REMAINSIZE, &remained_size) == SOCK_OK,
          "SO_REMAINSIZE succeeds for an open UDP socket");
    CHECK(remained_size == 0u,
          "SO_REMAINSIZE returns the cached datagram remainder");
    CHECK(getsockopt(0u, SO_PACKINFO, &pack_info) == SOCK_OK,
          "SO_PACKINFO succeeds for an open UDP socket");
    CHECK(fake_sn_mr_reads == 0u,
          "mode-derived socket options perform no Sn_MR reads when cached");
}

static void test_closed_socket_mode_query_reads_hardware(void)
{
    uint8_t flags = 0u;

    reset_register_fake();
    fake_sn_mode = SF_MULTI_ENABLE;
    fake_sn_mr_reads = 0u;

    CHECK(getsockopt(0u, SO_FLAG, &flags) == SOCK_OK,
          "SO_FLAG succeeds when no socket mode is cached");
    CHECK(flags == SF_MULTI_ENABLE,
          "SO_FLAG preserves an uncached hardware flag value");
    CHECK(fake_sn_mr_reads == 1u,
          "SO_FLAG falls back to Sn_MR when the cache is invalid");
}

static void test_recvfrom_mode_check_uses_cached_state(void)
{
    uint8_t payload = 0u;
    uint8_t address[4] = {0u};
    uint16_t port = 0u;

    prepare_open_listen_test();
    CHECK(socket(0u, Sn_MR_UDP, 5000u, SF_IO_NONBLOCK) == 0,
          "nonblocking UDP socket opens for recvfrom cache test");
    fake_sn_mr_reads = 0u;

    CHECK(recvfrom(0u, &payload, 1u, address, &port) == SOCK_BUSY,
          "empty nonblocking recvfrom reports busy");
    CHECK(fake_sn_mr_reads == 0u,
          "recvfrom performs no Sn_MR read when the mode cache is valid");
}

static void test_close_avoids_duplicate_status_read(void)
{
    prepare_open_listen_test();
    CHECK(socket(0u, Sn_MR_UDP, 5000u, 0u) == 0,
          "UDP socket opens for close status-read test");
    fake_sn_sr_reads = 0u;

    CHECK(close(0u) == SOCK_OK, "close succeeds for an open socket");
    CHECK(fake_sn_sr_reads == 2u,
          "close reads Sn_SR only for pre-close and completion checks");
}

static void test_faulted_listen_skips_status_read(void)
{
    int8_t result;

    prepare_open_listen_test();
    fake_delayed_command = Sn_CR_OPEN;
    fake_command_accept_delay_us = 10000u;
    CHECK(socket(0u, Sn_MR_TCP, 5000u, 0u) == SOCKERR_DEADLINE,
          "failed OPEN establishes a faulted socket");
    fake_sn_sr_reads = 0u;

    result = listen(0u);
    CHECK(result == SOCKERR_IO,
          "listen rejects a faulted socket before inspecting hardware state");
    CHECK(fake_sn_sr_reads == 0u,
          "faulted listen performs no Sn_SR read");
}

static void prepare_close_fault_test(void)
{
    wizchip_timeout_config_t config = {5000u, 100200u, 5000u};

    reset_register_fake();
    fake_rtr = 1u;
    fake_rcr = 1u;
    CHECK(wizchip_set_timeout_config(&config) == 0,
          "close timeout configuration is accepted");
    CHECK(wizchip_init(NULL, NULL) == 0,
          "chip initializes for close fault checks");
    reg_wizchip_time_cbfunc(fake_now, fake_wait_hook);
    fake_spi_tick_us = 100u;
    fake_sn_mode = Sn_MR_TCP;
    fake_sn_status = SOCK_ESTABLISHED;
}

static int8_t fail_close_command_acceptance(uint64_t *elapsed_us)
{
    uint64_t started_us;
    int8_t result;

    fake_delayed_command = Sn_CR_CLOSE;
    fake_command_accept_delay_us = 10000u;
    started_us = fake_now_us;
    result = close(0u);
    *elapsed_us = fake_now_us - started_us;
    return result;
}

static void test_close_of_already_closed_socket_is_idempotent(void)
{
    unsigned int commands_before;

    prepare_close_fault_test();
    fake_sn_status = SOCK_CLOSED;
    commands_before = fake_close_commands;

    CHECK(close(0u) == SOCK_OK,
          "close succeeds when hardware is already CLOSED");
    CHECK(fake_close_commands == commands_before,
          "already-CLOSED close does not issue another command");
}

static void test_failed_close_is_bounded_and_preserves_state(void)
{
    uint64_t elapsed_us = 0u;
    int8_t result;

    prepare_close_fault_test();
    result = fail_close_command_acceptance(&elapsed_us);

    CHECK(result == SOCKERR_DEADLINE,
          "unaccepted close command returns a deadline error");
    CHECK(elapsed_us <= 5500u,
          "failed close returns at the configured command deadline");
    CHECK(fake_sn_status == SOCK_ESTABLISHED,
          "failed close preserves the prior hardware state");
}

static void test_faulted_socket_rejects_retry_until_chip_reset(void)
{
    uint64_t elapsed_us = 0u;
    unsigned int close_commands_before;
    unsigned int open_commands_before;
    int8_t result;

    prepare_close_fault_test();
    result = fail_close_command_acceptance(&elapsed_us);
    CHECK(result == SOCKERR_DEADLINE,
          "close failure establishes the socket FAULTED state");
    CHECK(wizchip_get_state() == WIZCHIP_STATE_READY,
          "close failure faults only the affected socket");

    fake_delayed_command = 0u;
    fake_sn_command = 0u;
    fake_pending_status = 0u;
    fake_sn_status = SOCK_ESTABLISHED;
    close_commands_before = fake_close_commands;
    open_commands_before = fake_open_commands;
    CHECK(socket(0u, Sn_MR_UDP, 5000u, 0u) == SOCKERR_NOTREADY,
          "ordinary retry on a FAULTED socket is rejected");
    CHECK(fake_close_commands == close_commands_before &&
              fake_open_commands == open_commands_before,
          "FAULTED retry performs no socket hardware command");

    CHECK(wizchip_init(NULL, NULL) == 0,
          "verified chip reset recovers a FAULTED socket");
    CHECK(socket(0u, Sn_MR_UDP, 5000u, 0u) == 0,
          "normal socket operation resumes after chip-reset recovery");
}

int main(void)
{
    install_register_fake();
    test_time_callback_registration();
    test_deadline_helpers();
    test_poll_max_is_nonzero();
    test_rtr_rcr_operation_timeout_floor();
    test_stable_sample_helpers_exist();
    test_stable_nonzero_samples_succeed();
    test_register_faults_return_io_error();
    test_stable_zero_is_distinct_from_failure_zero();
    test_unstable_samples_return_deadline();
    test_macraw_send_skips_fsr_length_check();
    test_socket_and_listen_command_acceptance_wait_is_bounded();
    test_unaccepted_sn_cr_expires_deadline_and_returns_error();
    test_sn_sr_transitions_from_init_to_listen_within_deadline();
    test_open_succeeds_within_deadline_for_valid_mode();
    test_open_socket_mode_queries_use_cached_state();
    test_closed_socket_mode_query_reads_hardware();
    test_recvfrom_mode_check_uses_cached_state();
    test_close_avoids_duplicate_status_read();
    test_faulted_listen_skips_status_read();
    test_close_of_already_closed_socket_is_idempotent();
    test_failed_close_is_bounded_and_preserves_state();
    test_faulted_socket_rejects_retry_until_chip_reset();

    if (failures != 0u) {
        fprintf(stderr, "\n%u FAILURES\n", failures);
        return 1;
    }
    printf("PASS: all %s checks\n", __FILE__);
    return 0;
}
