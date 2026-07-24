#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define close posix_close
#include <unistd.h>
#undef close

#include "wizchip_conf.h"
#include "socket.h"
#include "w5500.h"

#define THREAD_COUNT 4U
#define ITERATION_COUNT 1000U
#define SOCKET_COUNT 8U
#define STATE_ITERATION_COUNT 10000U
#define STRESS_THREAD_COUNT 2U
#define STRESS_ITERATION_COUNT 5000U
#define STRESS_WATCHDOG_SECONDS 120U

static unsigned int failures;
static unsigned int socket_acquires;
static unsigned int socket_releases;
static unsigned int global_acquires;
static unsigned int global_releases;
static unsigned int ownership_violations;
static unsigned int ordering_violations;
static uint16_t spi_addr;
static uint8_t spi_header_bytes;
static uint8_t spi_block;
static uint8_t fake_sn_mode;
static uint8_t fake_sn_status;
static uint8_t fake_sn_ir;
static uint16_t fake_sn_rx_size;
static unsigned int fake_select_calls;
static unsigned int fake_close_commands;
static unsigned int fake_listen_commands;
static unsigned int close_cleanup_lock_violations;
static unsigned int cleanup_before_unlock_violations;
static uint8_t inspect_close_release;
static uint8_t mutate_status_on_lock;
static uint8_t mutation_socket;
static uint8_t lock_entry_status;
static pthread_barrier_t state_update_barrier;

typedef struct {
    uint8_t mode;
    uint8_t status;
    uint8_t interrupt;
    uint16_t tx_write_pointer;
    uint16_t rx_read_pointer;
} stress_socket_t;

typedef struct {
    unsigned int worker_index;
    unsigned int operations_completed;
    unsigned int operation_failures;
    unsigned int state_losses;
    uint8_t visited[SOCKET_COUNT];
} stress_worker_t;

static stress_socket_t stress_sockets[SOCKET_COUNT];
static pthread_mutex_t stress_bus_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t watchdog_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t watchdog_condition = PTHREAD_COND_INITIALIZER;
static uint16_t stress_spi_addr;
static uint8_t stress_spi_block;
static uint8_t stress_header_bytes;
static int watchdog_finished;
static volatile sig_atomic_t watchdog_deadline_overruns;

extern uint8_t sock_pack_info[_WIZCHIP_SOCK_NUM_];

static pthread_mutex_t tracker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t global_mutex;
static pthread_mutex_t socket_mutexes[SOCKET_COUNT];
static _Thread_local unsigned int lock_level;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL [%d]: %s\n", __LINE__, (message)); \
    } \
} while (0)

static void record_violation(unsigned int *counter)
{
    int rc = pthread_mutex_lock(&tracker_mutex);

    if (rc == 0) {
        ++*counter;
        (void)pthread_mutex_unlock(&tracker_mutex);
    }
}

static void socket_enter(uint8_t sn)
{
    int rc = pthread_mutex_lock(&socket_mutexes[sn]);

    if (rc != 0) {
        record_violation(&ownership_violations);
        return;
    }
    if (lock_level != 1U) {
        record_violation(&ordering_violations);
    }
    lock_level = 2U;
    (void)pthread_mutex_lock(&tracker_mutex);
    ++socket_acquires;
    (void)pthread_mutex_unlock(&tracker_mutex);
    if (mutate_status_on_lock != 0U && sn == mutation_socket) {
        fake_sn_status = lock_entry_status;
    }
}

static void socket_exit(uint8_t sn)
{
    if (lock_level != 2U) {
        record_violation(&ordering_violations);
    }
    if (inspect_close_release != 0U &&
        (fake_sn_status != SOCK_CLOSED ||
         sock_pack_info[sn] != PACK_NONE)) {
        record_violation(&cleanup_before_unlock_violations);
    }
    lock_level = 1U;
    (void)pthread_mutex_lock(&tracker_mutex);
    ++socket_releases;
    (void)pthread_mutex_unlock(&tracker_mutex);
    if (pthread_mutex_unlock(&socket_mutexes[sn]) != 0) {
        record_violation(&ownership_violations);
    }
}

static void global_enter(void)
{
    int rc = pthread_mutex_lock(&global_mutex);

    if (rc != 0) {
        record_violation(&ownership_violations);
        return;
    }
    if (lock_level != 0U) {
        record_violation(&ordering_violations);
    }
    lock_level = 1U;
    (void)pthread_mutex_lock(&tracker_mutex);
    ++global_acquires;
    (void)pthread_mutex_unlock(&tracker_mutex);
}

static void global_exit(void)
{
    if (lock_level != 1U) {
        record_violation(&ordering_violations);
    }
    lock_level = 0U;
    (void)pthread_mutex_lock(&tracker_mutex);
    ++global_releases;
    (void)pthread_mutex_unlock(&tracker_mutex);
    if (pthread_mutex_unlock(&global_mutex) != 0) {
        record_violation(&ownership_violations);
    }
}

static void reset_counts(void)
{
    socket_acquires = 0U;
    socket_releases = 0U;
    global_acquires = 0U;
    global_releases = 0U;
    ownership_violations = 0U;
    ordering_violations = 0U;
    close_cleanup_lock_violations = 0U;
    cleanup_before_unlock_violations = 0U;
}

static void test_complete_pair_installation(void)
{
    reset_counts();
    reg_wizchip_lock_cbfunc(socket_enter, socket_exit, global_enter, global_exit);

    WIZCHIP_GLOBAL_LOCK();
    WIZCHIP_SOCK_LOCK(0U);
    WIZCHIP_SOCK_UNLOCK(0U);
    WIZCHIP_GLOBAL_UNLOCK();

    CHECK(socket_acquires == 1U, "complete socket enter callback installed");
    CHECK(socket_releases == 1U, "complete socket exit callback installed");
    CHECK(global_acquires == 1U, "complete global enter callback installed");
    CHECK(global_releases == 1U, "complete global exit callback installed");

    reg_wizchip_lock_cbfunc(socket_enter, NULL, global_enter, global_exit);
    WIZCHIP_GLOBAL_LOCK();
    WIZCHIP_SOCK_LOCK(0U);
    WIZCHIP_SOCK_UNLOCK(0U);
    WIZCHIP_GLOBAL_UNLOCK();

    CHECK(socket_acquires == 1U, "incomplete socket pair selects safe defaults");
    CHECK(socket_releases == 1U, "incomplete socket pair is not partially installed");
    CHECK(global_acquires == 2U, "independent complete global pair installed");
    CHECK(global_releases == 2U, "independent global pair remains balanced");
}

static void test_null_callbacks_are_safe_defaults(void)
{
    reset_counts();
    reg_wizchip_lock_cbfunc(NULL, NULL, NULL, NULL);

    WIZCHIP_GLOBAL_LOCK();
    WIZCHIP_SOCK_LOCK(0U);
    WIZCHIP_SOCK_UNLOCK(0U);
    WIZCHIP_GLOBAL_UNLOCK();

    CHECK(socket_acquires == 0U, "null socket callbacks are no-ops");
    CHECK(socket_releases == 0U, "null socket callbacks require no release");
    CHECK(global_acquires == 0U, "null global callbacks are no-ops");
    CHECK(global_releases == 0U, "null global callbacks require no release");
}

static void *lock_worker(void *argument)
{
    uint8_t sn = (uint8_t)(uintptr_t)argument;
    unsigned int iteration;

    for (iteration = 0U; iteration < ITERATION_COUNT; ++iteration) {
        WIZCHIP_GLOBAL_LOCK();
        WIZCHIP_SOCK_LOCK(sn);
        WIZCHIP_SOCK_UNLOCK(sn);
        WIZCHIP_GLOBAL_UNLOCK();
    }
    return NULL;
}

static void test_lock_balance_non_recursive_and_ordered(void)
{
    pthread_t threads[THREAD_COUNT];
    unsigned int created = 0U;
    unsigned int i;

    reset_counts();
    reg_wizchip_lock_cbfunc(socket_enter, socket_exit, global_enter, global_exit);

    for (i = 0U; i < THREAD_COUNT; ++i) {
        int rc = pthread_create(&threads[i], NULL, lock_worker,
                                (void *)(uintptr_t)i);

        CHECK(rc == 0, "pthread_create succeeds");
        if (rc != 0) {
            break;
        }
        ++created;
    }
    for (i = 0U; i < created; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0, "pthread_join succeeds");
    }

    CHECK(socket_acquires == THREAD_COUNT * ITERATION_COUNT,
          "all socket lock acquisitions tracked");
    CHECK(socket_releases == socket_acquires,
          "socket lock acquire/release calls are balanced");
    CHECK(global_acquires == THREAD_COUNT * ITERATION_COUNT,
          "all global lock acquisitions tracked");
    CHECK(global_releases == global_acquires,
          "global lock acquire/release calls are balanced");
    CHECK(ownership_violations == 0U, "locks are owned non-recursively");
    CHECK(ordering_violations == 0U, "global lock is acquired before socket lock");
}

static void fake_critical_enter(void) {}
static void fake_critical_exit(void) {}

static void fake_select(void)
{
    ++fake_select_calls;
    spi_addr = 0U;
    spi_header_bytes = 0U;
    spi_block = 0U;
}

static void fake_deselect(void) {}

static int simple_fake_is_socket_block(void)
{
    return spi_block >= WIZCHIP_SREG_BLOCK(0U) &&
           ((spi_block - WIZCHIP_SREG_BLOCK(0U)) % 4U) == 0U;
}

static uint8_t fake_spi_read(void)
{
    uint8_t value = 0U;

    if (simple_fake_is_socket_block() && spi_addr == 0x0000U) {
        value = fake_sn_mode;
    } else if (simple_fake_is_socket_block() && spi_addr == 0x0001U) {
        value = 0U;
    } else if (simple_fake_is_socket_block() && spi_addr == 0x0002U) {
        value = fake_sn_ir;
    } else if (simple_fake_is_socket_block() && spi_addr == 0x0003U) {
        value = fake_sn_status;
    } else if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x000FU) {
        value = 192U;
    } else if (simple_fake_is_socket_block() && spi_addr == 0x001EU) {
        value = 2U;
    } else if (simple_fake_is_socket_block() && spi_addr == 0x001FU) {
        value = 2U;
    } else if (simple_fake_is_socket_block() && spi_addr == 0x0020U) {
        value = 0x08U;
    } else if (simple_fake_is_socket_block() && spi_addr == 0x0026U) {
        value = (uint8_t)(fake_sn_rx_size >> 8);
    } else if (simple_fake_is_socket_block() && spi_addr == 0x0027U) {
        value = (uint8_t)fake_sn_rx_size;
    } else if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x0039U) {
        value = 0x04U;
    }
    ++spi_addr;
    return value;
}

static void fake_spi_write(uint8_t value)
{
    if (spi_header_bytes == 0U) {
        spi_addr = (uint16_t)value << 8;
        ++spi_header_bytes;
        return;
    }
    if (spi_header_bytes == 1U) {
        spi_addr |= value;
        ++spi_header_bytes;
        return;
    }
    if (spi_header_bytes == 2U) {
        spi_block = (uint8_t)(value >> 3);
        ++spi_header_bytes;
        return;
    }

    if (spi_block == WIZCHIP_CREG_BLOCK && spi_addr == 0x0000U) {
        if (value == MR_RST) {
            fake_sn_mode = 0U;
            fake_sn_status = SOCK_CLOSED;
            fake_sn_ir = 0U;
        }
    } else if (simple_fake_is_socket_block() && spi_addr == 0x0000U) {
        fake_sn_mode = value;
    } else if (simple_fake_is_socket_block() && spi_addr == 0x0001U) {
        if (value == Sn_CR_OPEN) {
            switch (fake_sn_mode & 0x0FU) {
            case Sn_MR_TCP:
                fake_sn_status = SOCK_INIT;
                break;
            case Sn_MR_UDP:
                fake_sn_status = SOCK_UDP;
                break;
            case Sn_MR_MACRAW:
                fake_sn_status = SOCK_MACRAW;
                break;
            default:
                fake_sn_status = SOCK_CLOSED;
                break;
            }
        } else if (value == Sn_CR_LISTEN) {
            ++fake_listen_commands;
            if (fake_sn_status == SOCK_INIT) {
                fake_sn_status = SOCK_LISTEN;
            }
        } else if (value == Sn_CR_CONNECT) {
            fake_sn_status = SOCK_ESTABLISHED;
        } else if (value == Sn_CR_DISCON) {
            fake_sn_status = SOCK_CLOSED;
        } else if (value == Sn_CR_CLOSE) {
            ++fake_close_commands;
            if (lock_level != 2U) {
                ++close_cleanup_lock_violations;
            }
            fake_sn_status = SOCK_CLOSED;
        } else if (value == Sn_CR_SEND) {
            fake_sn_ir |= Sn_IR_SENDOK;
        }
    } else if (simple_fake_is_socket_block() && spi_addr == 0x0002U) {
        if (value == 0xFFU && lock_level != 2U) {
            ++close_cleanup_lock_violations;
        }
        fake_sn_ir &= (uint8_t)~value;
    }
    ++spi_addr;
}

static void fake_spi_read_burst(uint8_t *buffer, uint16_t length)
{
    uint16_t i;

    for (i = 0U; i < length; ++i) {
        buffer[i] = fake_spi_read();
    }
}

static void fake_spi_write_burst(uint8_t *buffer, uint16_t length)
{
    uint16_t i;

    for (i = 0U; i < length; ++i) {
        fake_spi_write(buffer[i]);
    }
}

static uint8_t fake_spi_busy(void)
{
    return 0U;
}

static int8_t fake_spi_error(void)
{
    return 0;
}

static void fake_spi_clear_error(void) {}

static void install_register_fake(void)
{
    reg_wizchip_cris_cbfunc(fake_critical_enter, fake_critical_exit);
    reg_wizchip_cs_cbfunc(fake_select, fake_deselect);
    reg_wizchip_spi_cbfunc(fake_spi_read, fake_spi_write);
    reg_wizchip_spiburst_cbfunc(fake_spi_read_burst, fake_spi_write_burst);
    reg_wizchip_spistatus_cbfunc(fake_spi_busy, fake_spi_error,
                                  fake_spi_clear_error);
}

static void prepare_socket_operation_test(void)
{
    install_register_fake();
    reg_wizchip_lock_cbfunc(NULL, NULL, NULL, NULL);
    fake_sn_mode = 0U;
    fake_sn_status = SOCK_CLOSED;
    fake_sn_ir = 0U;
    fake_sn_rx_size = 0U;
    fake_select_calls = 0U;
    fake_close_commands = 0U;
    fake_listen_commands = 0U;
    mutate_status_on_lock = 0U;
    inspect_close_release = 0U;
    CHECK(wizchip_init(NULL, NULL) == 0,
          "chip initializes for socket/listen lock checks");
    reset_counts();
    fake_select_calls = 0U;
    reg_wizchip_lock_cbfunc(socket_enter, socket_exit,
                            global_enter, global_exit);
}

static void prepare_tcp_stream(uint8_t sn, uint8_t status)
{
    prepare_socket_operation_test();
    reg_wizchip_lock_cbfunc(NULL, NULL, NULL, NULL);
    CHECK(socket(sn, Sn_MR_TCP, 5000U, 0U) == (int8_t)sn,
          "TCP socket initializes for stream I/O checks");
    fake_sn_status = status;
    fake_sn_ir = 0U;
    fake_sn_rx_size = 0U;
    wizchip_txmax_cache[sn] = 2048U;
    wizchip_rxmax_cache[sn] = 2048U;
    fake_close_commands = 0U;
    reset_counts();
    fake_select_calls = 0U;
    reg_wizchip_lock_cbfunc(socket_enter, socket_exit,
                            global_enter, global_exit);
}

static void repair_leaked_test_lock(uint8_t sn)
{
    if (socket_acquires != socket_releases) {
        lock_level = 0U;
        CHECK(pthread_mutex_unlock(&socket_mutexes[sn]) == 0,
              "test harness releases a lock leaked by the expected red path");
    }
}

static void test_stream_io_rejects_invalid_number_before_lock(void)
{
    uint8_t byte = 0xA5U;

    prepare_socket_operation_test();

    CHECK(send(SOCKET_COUNT, &byte, 1U) == SOCKERR_SOCKNUM,
          "send rejects an invalid socket number");
    CHECK(recv(SOCKET_COUNT, &byte, 1U) == SOCKERR_SOCKNUM,
          "recv rejects an invalid socket number");
    CHECK(socket_acquires == 0U,
          "invalid stream socket numbers are rejected before locking");
    CHECK(socket_releases == 0U,
          "invalid stream socket numbers do not invoke unlock");
    CHECK(fake_select_calls == 0U,
          "invalid stream socket numbers cause no register access");
}

static void test_stream_io_rejects_null_nonzero_buffer_before_lock(void)
{
    prepare_tcp_stream(0U, SOCK_ESTABLISHED);

    CHECK(send(0U, NULL, 1U) == SOCKERR_ARG,
          "send rejects a null nonzero buffer");
    CHECK(recv(0U, NULL, 1U) == SOCKERR_ARG,
          "recv rejects a null nonzero buffer");
    CHECK(socket_acquires == 0U,
          "null nonzero stream buffers are rejected before locking");
    CHECK(socket_releases == 0U,
          "null nonzero stream buffers do not invoke unlock");
    CHECK(fake_select_calls == 0U,
          "null nonzero stream buffers cause no register access");
}

static void test_stream_io_zero_length_is_pre_access_noop(void)
{
    uint8_t byte = 0x5AU;

    prepare_tcp_stream(1U, SOCK_ESTABLISHED);
    CHECK(send(1U, &byte, 0U) == 0,
          "zero-length send succeeds as a no-op");
    CHECK(byte == 0x5AU, "zero-length send does not touch its buffer");
    CHECK(socket_acquires == 0U,
          "zero-length send returns before locking");
    CHECK(socket_releases == 0U,
          "zero-length send does not invoke unlock");
    CHECK(fake_select_calls == 0U,
          "zero-length send performs no register access");
    repair_leaked_test_lock(1U);

    prepare_tcp_stream(2U, SOCK_ESTABLISHED);
    CHECK(recv(2U, &byte, 0U) == 0,
          "zero-length recv succeeds as a no-op");
    CHECK(byte == 0x5AU, "zero-length recv does not touch its buffer");
    CHECK(socket_acquires == 0U,
          "zero-length recv returns before locking");
    CHECK(socket_releases == 0U,
          "zero-length recv does not invoke unlock");
    CHECK(fake_select_calls == 0U,
          "zero-length recv performs no register access");
    repair_leaked_test_lock(2U);
}

static void test_recv_close_wait_cleanup_is_lock_owned(void)
{
    uint8_t byte = 0U;

    prepare_tcp_stream(3U, SOCK_CLOSE_WAIT);
    sock_pack_info[3U] = PACK_REMAINED;
    inspect_close_release = 1U;

    CHECK(recv(3U, &byte, 1U) == SOCKERR_SOCKSTATUS,
          "recv reports drained CLOSE_WAIT as closed");

    inspect_close_release = 0U;
    CHECK(socket_acquires == 1U,
          "CLOSE_WAIT recv acquires its lock exactly once");
    CHECK(socket_releases == 1U,
          "CLOSE_WAIT recv releases its lock exactly once");
    CHECK(fake_close_commands == 1U,
          "CLOSE_WAIT recv performs one hardware close");
    CHECK(close_cleanup_lock_violations == 0U,
          "CLOSE_WAIT cleanup executes while the recv lock is owned");
    CHECK(cleanup_before_unlock_violations == 0U,
          "CLOSE_WAIT cleanup finishes before recv unlocks");
    CHECK(ownership_violations == 0U,
          "CLOSE_WAIT recv does not recurse through public close");
}

static void test_stream_io_unlocks_once_on_locked_exits(void)
{
    uint8_t byte = 0xC3U;

    prepare_tcp_stream(4U, SOCK_ESTABLISHED);
    CHECK(send(4U, &byte, 1U) == 1,
          "send succeeds for lock balance checks");
    CHECK(socket_acquires == 1U && socket_releases == 1U,
          "successful send unlocks exactly once");

    prepare_tcp_stream(5U, SOCK_ESTABLISHED);
    fake_sn_rx_size = 1U;
    CHECK(recv(5U, &byte, 1U) == 1,
          "recv succeeds for lock balance checks");
    CHECK(socket_acquires == 1U && socket_releases == 1U,
          "successful recv unlocks exactly once");

    prepare_tcp_stream(6U, SOCK_INIT);
    CHECK(send(6U, &byte, 1U) == SOCKERR_SOCKSTATUS,
          "send rejects an invalid mutable socket state");
    CHECK(socket_acquires == 1U && socket_releases == 1U,
          "send status rejection unlocks exactly once");

    prepare_tcp_stream(6U, SOCK_INIT);
    CHECK(recv(6U, &byte, 1U) == SOCKERR_SOCKSTATUS,
          "recv rejects an invalid mutable socket state");
    CHECK(socket_acquires == 1U && socket_releases == 1U,
          "recv status rejection unlocks exactly once");

    prepare_socket_operation_test();
    CHECK(send(7U, &byte, 1U) == SOCKERR_SOCKMODE,
          "send rejects a non-TCP socket mode");
    CHECK(socket_acquires == 1U && socket_releases == 1U,
          "send mode rejection unlocks exactly once");
    repair_leaked_test_lock(7U);

    prepare_socket_operation_test();
    CHECK(recv(7U, &byte, 1U) == SOCKERR_SOCKMODE,
          "recv rejects a non-TCP socket mode");
    CHECK(socket_acquires == 1U && socket_releases == 1U,
          "recv mode rejection unlocks exactly once");
    repair_leaked_test_lock(7U);
}

static void test_socket_rejects_invalid_number_before_lock(void)
{
    int8_t result;

    prepare_socket_operation_test();
    result = socket(SOCKET_COUNT, Sn_MR_UDP, 5000U, 0U);

    CHECK(result == SOCKERR_SOCKNUM,
          "socket rejects an invalid socket number");
    CHECK(socket_acquires == 0U,
          "invalid socket number is rejected before lock acquisition");
    CHECK(socket_releases == 0U,
          "invalid socket number does not touch the unlock callback");
}

static void test_socket_rejects_occupied_slot_after_lock(void)
{
    int8_t result;

    prepare_socket_operation_test();
    mutation_socket = 0U;
    lock_entry_status = SOCK_ESTABLISHED;
    mutate_status_on_lock = 1U;

    result = socket(0U, Sn_MR_UDP, 5000U, 0U);
    mutate_status_on_lock = 0U;

    CHECK(result < 0,
          "socket rejects a slot that becomes occupied before lock ownership");
    CHECK(socket_acquires == 1U,
          "occupied socket state is checked after acquiring the lock");
    CHECK(socket_releases == 1U,
          "occupied socket rejection releases the lock exactly once");
    CHECK(fake_close_commands == 0U,
          "occupied socket rejection does not close the existing socket");
    CHECK(fake_sn_status == SOCK_ESTABLISHED,
          "occupied socket rejection preserves the existing socket state");
}

static void test_socket_and_listen_unlock_once_per_locked_exit(void)
{
    int8_t result;

    prepare_socket_operation_test();
    result = socket(0U, Sn_MR_TCP, 5000U, 0U);
    CHECK(result == 0, "socket succeeds for lock balance checks");
    CHECK(socket_acquires == 1U,
          "successful socket acquires its lock exactly once");
    CHECK(socket_releases == socket_acquires,
          "successful socket has one unlock per lock acquisition");

    reset_counts();
    result = listen(0U);
    CHECK(result == SOCK_OK, "listen succeeds for lock balance checks");
    CHECK(socket_acquires == 1U,
          "successful listen acquires its lock exactly once");
    CHECK(socket_releases == socket_acquires,
          "successful listen has one unlock per lock acquisition");
}

static void test_listen_revalidates_mutable_state_after_lock(void)
{
    int8_t result;

    prepare_socket_operation_test();
    CHECK(socket(0U, Sn_MR_TCP, 5000U, 0U) == 0,
          "TCP socket initializes for listen state revalidation");
    reset_counts();
    fake_close_commands = 0U;
    fake_listen_commands = 0U;
    mutation_socket = 0U;
    lock_entry_status = SOCK_ESTABLISHED;
    mutate_status_on_lock = 1U;

    result = listen(0U);
    mutate_status_on_lock = 0U;

    CHECK(result == SOCKERR_SOCKINIT,
          "listen rejects a socket that leaves INIT before lock ownership");
    CHECK(socket_acquires == 1U,
          "listen revalidates mutable state while holding the lock");
    CHECK(socket_releases == socket_acquires,
          "listen state rejection has one unlock per lock acquisition");
    CHECK(fake_listen_commands == 0U,
          "listen state rejection occurs before issuing LISTEN");
    CHECK(fake_close_commands == 0U,
          "listen state rejection does not close the changed socket");
}

static void test_socket_does_not_recurse_through_public_close(void)
{
    int8_t result;

    prepare_socket_operation_test();
    result = socket(0U, Sn_MR_UDP, 5000U, 0U);

    CHECK(result == 0, "socket opens a closed slot without public close recursion");
    CHECK(socket_acquires == 1U,
          "socket acquires one non-recursive lock while preparing a closed slot");
    CHECK(socket_releases == 1U,
          "socket releases one non-recursive lock while preparing a closed slot");
    CHECK(ownership_violations == 0U,
          "socket does not call lock-taking public close internally");
}

static void test_connect_rejects_null_address_before_lock(void)
{
    int8_t result;

    prepare_socket_operation_test();
    CHECK(socket(0U, Sn_MR_MACRAW, 5000U, 0U) == 0,
          "MACRAW socket initializes for connect argument ordering");
    reset_counts();

    result = connect(0U, NULL, 80U);

    CHECK(result == SOCKERR_ARG,
          "connect rejects a null nonzero-length address");
    CHECK(socket_acquires == 0U,
          "connect rejects a null address before lock acquisition");
    CHECK(socket_releases == 0U,
          "connect null-address rejection does not touch unlock");
    repair_leaked_test_lock(0U);
}

static void test_connect_rejects_zero_port(void)
{
    uint8_t address[4] = {192U, 0U, 2U, 1U};
    int8_t result;

    prepare_socket_operation_test();
    CHECK(socket(0U, Sn_MR_TCP, 5000U, 0U) == 0,
          "TCP socket initializes for connect port validation");
    reset_counts();

    result = connect(0U, address, 0U);

    CHECK(result == SOCKERR_PORTZERO,
          "connect rejects destination port zero");
    CHECK(socket_releases == socket_acquires,
          "connect port rejection leaves lock callbacks balanced");
}

static void test_connect_rejects_macraw_and_unlocks_once(void)
{
    uint8_t address[4] = {192U, 0U, 2U, 1U};
    int8_t result;

    prepare_socket_operation_test();
    CHECK(socket(0U, Sn_MR_MACRAW, 5000U, 0U) == 0,
          "MACRAW socket initializes for connect mode validation");
    reset_counts();

    result = connect(0U, address, 80U);

    CHECK(result == SOCKERR_SOCKMODE,
          "connect rejects MACRAW mode");
    CHECK(socket_acquires == 1U,
          "connect mode validation occurs while owning one socket lock");
    CHECK(socket_releases == 1U,
          "connect mode rejection unlocks exactly once");
    repair_leaked_test_lock(0U);
}

static void test_connect_requires_init_state_and_unlocks_once(void)
{
    uint8_t address[4] = {192U, 0U, 2U, 1U};
    int8_t result;

    prepare_socket_operation_test();
    CHECK(socket(0U, Sn_MR_TCP, 5000U, 0U) == 0,
          "TCP socket initializes for connect state validation");
    fake_sn_status = SOCK_ESTABLISHED;
    reset_counts();

    result = connect(0U, address, 80U);

    CHECK(result == SOCKERR_SOCKINIT,
          "connect accepts only a socket in INIT state");
    CHECK(socket_acquires == 1U,
          "connect state validation owns one socket lock");
    CHECK(socket_releases == 1U,
          "connect state rejection unlocks exactly once");
    repair_leaked_test_lock(0U);
}

static void test_connect_success_unlocks_once(void)
{
    uint8_t address[4] = {192U, 0U, 2U, 1U};
    int8_t result;

    prepare_socket_operation_test();
    CHECK(socket(0U, Sn_MR_TCP, 5000U, 0U) == 0,
          "TCP socket initializes for successful connect");
    reset_counts();

    result = connect(0U, address, 80U);

    CHECK(result == SOCK_OK, "connect succeeds from INIT state");
    CHECK(socket_acquires == 1U,
          "successful connect acquires exactly one socket lock");
    CHECK(socket_releases == 1U,
          "successful connect unlocks exactly once");
}

static void test_disconnect_rejects_invalid_number_before_lock(void)
{
    int8_t result;

    prepare_socket_operation_test();
    result = disconnect(SOCKET_COUNT);

    CHECK(result == SOCKERR_SOCKNUM,
          "disconnect rejects an invalid socket number");
    CHECK(socket_acquires == 0U,
          "disconnect validates socket number before lock acquisition");
    CHECK(socket_releases == 0U,
          "disconnect invalid-number rejection does not touch unlock");
}

static void test_disconnect_mode_failure_unlocks_once(void)
{
    int8_t result;

    prepare_socket_operation_test();
    CHECK(socket(0U, Sn_MR_MACRAW, 5000U, 0U) == 0,
          "MACRAW socket initializes for disconnect mode validation");
    reset_counts();

    result = disconnect(0U);

    CHECK(result == SOCKERR_SOCKMODE,
          "disconnect rejects MACRAW mode");
    CHECK(socket_acquires == 1U,
          "disconnect mode validation owns one socket lock");
    CHECK(socket_releases == 1U,
          "disconnect mode rejection unlocks exactly once");
    repair_leaked_test_lock(0U);
}

static void test_disconnect_success_unlocks_once(void)
{
    int8_t result;

    prepare_socket_operation_test();
    CHECK(socket(0U, Sn_MR_TCP, 5000U, 0U) == 0,
          "TCP socket initializes for successful disconnect");
    fake_sn_status = SOCK_ESTABLISHED;
    reset_counts();

    result = disconnect(0U);

    CHECK(result == SOCK_OK, "disconnect succeeds for an established socket");
    CHECK(socket_acquires == 1U,
          "successful disconnect acquires exactly one socket lock");
    CHECK(socket_releases == 1U,
          "successful disconnect unlocks exactly once");
}

static void test_close_acquires_one_lock_and_cleans_up_while_owned(void)
{
    unsigned int acquires_before;
    unsigned int releases_before;

    install_register_fake();
    reg_wizchip_lock_cbfunc(NULL, NULL, NULL, NULL);
    CHECK(wizchip_init(NULL, NULL) == 0,
          "chip initializes for close lock ownership checks");
    fake_sn_status = SOCK_ESTABLISHED;
    fake_sn_ir = Sn_IR_SENDOK;
    fake_close_commands = 0U;
    sock_pack_info[0] = PACK_REMAINED;
    reset_counts();
    reg_wizchip_lock_cbfunc(socket_enter, socket_exit,
                            global_enter, global_exit);
    acquires_before = socket_acquires;
    releases_before = socket_releases;
    inspect_close_release = 1U;

    CHECK(close(0U) == SOCK_OK, "close succeeds for an active socket");

    inspect_close_release = 0U;
    CHECK(socket_acquires - acquires_before == 1U,
          "close acquires its socket lock exactly once");
    CHECK(socket_releases - releases_before == 1U,
          "close releases its socket lock exactly once");
    CHECK(fake_close_commands == 1U,
          "active close issues one hardware close command");
    CHECK(close_cleanup_lock_violations == 0U,
          "close performs hardware cleanup while owning its socket lock");
    CHECK(cleanup_before_unlock_violations == 0U,
          "close clears hardware and software resources before unlocking");
    CHECK(ownership_violations == 0U,
          "close does not recursively acquire a non-recursive lock");
}

static __attribute__((noinline)) int8_t update_socket_io_mode(void)
{
    uint8_t mode = SOCK_IO_NONBLOCK;

    return ctlsocket(3U, CS_SET_IOMODE, &mode);
}

static __attribute__((noinline)) int32_t update_socket_send_state(void)
{
    uint8_t mode = SOCK_IO_BLOCK;
    uint8_t byte = 0x5AU;

    if (ctlsocket(5U, CS_SET_IOMODE, &mode) != SOCK_OK) {
        return SOCKERR_ARG;
    }
    return send(5U, &byte, 1U);
}

static void *io_mode_worker(void *argument)
{
    unsigned int *worker_failures = argument;
    unsigned int iteration;
    int rc = pthread_barrier_wait(&state_update_barrier);

    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
        ++*worker_failures;
        return NULL;
    }
    for (iteration = 0U; iteration < STATE_ITERATION_COUNT; ++iteration) {
        if (update_socket_io_mode() != SOCK_OK) {
            ++*worker_failures;
        }
    }
    return NULL;
}

static void *send_state_worker(void *argument)
{
    unsigned int *worker_failures = argument;
    unsigned int iteration;
    int rc = pthread_barrier_wait(&state_update_barrier);

    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
        ++*worker_failures;
        return NULL;
    }
    for (iteration = 0U; iteration < STATE_ITERATION_COUNT; ++iteration) {
        if (update_socket_send_state() < 0) {
            ++*worker_failures;
        }
    }
    return NULL;
}

static void test_simultaneous_io_mode_and_send_state_updates(void)
{
    pthread_t io_thread;
    pthread_t send_thread;
    unsigned int io_failures = 0U;
    unsigned int send_failures = 0U;
    uint8_t mode = SOCK_IO_BLOCK;
    uint8_t byte = 0xA5U;
    uint8_t send_state;
    int8_t socket_result;
    int barrier_initialized;
    int io_created;
    int send_created;

    install_register_fake();
    fake_sn_mode = 0U;
    fake_sn_status = SOCK_CLOSED;
    fake_sn_ir = 0U;
    reg_wizchip_lock_cbfunc(NULL, NULL, NULL, NULL);
    CHECK(wizchip_init(NULL, NULL) == 0,
          "chip state initializes for socket-state updates");
    socket_result = socket(5U, Sn_MR_TCP, 5000U, 0U);
    CHECK(socket_result == 5,
          "socket 5 initializes for send-state updates");
    if (socket_result != 5) {
        return;
    }
    fake_sn_status = SOCK_ESTABLISHED;
    CHECK(ctlsocket(3U, CS_SET_IOMODE, &mode) == SOCK_OK,
          "socket 3 I/O mode initializes");
    CHECK(ctlsocket(5U, CS_SET_IOMODE, &mode) == SOCK_OK,
          "socket 5 I/O mode initializes");
    reg_wizchip_lock_cbfunc(socket_enter, socket_exit, global_enter, global_exit);

    barrier_initialized = pthread_barrier_init(&state_update_barrier, NULL, 2U);
    CHECK(barrier_initialized == 0,
          "state update barrier initializes");
    if (barrier_initialized != 0) {
        return;
    }
    io_created = pthread_create(&io_thread, NULL, io_mode_worker, &io_failures);
    CHECK(io_created == 0, "I/O-mode update thread creates");
    send_created = pthread_create(&send_thread, NULL, send_state_worker,
                                  &send_failures);
    CHECK(send_created == 0, "send-state update thread creates");

    if ((io_created == 0) != (send_created == 0)) {
        int rc = pthread_barrier_wait(&state_update_barrier);

        CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD,
              "main releases the sole state update worker");
    }

    if (io_created == 0) {
        CHECK(pthread_join(io_thread, NULL) == 0,
              "I/O-mode update thread joins");
    }
    if (send_created == 0) {
        CHECK(pthread_join(send_thread, NULL) == 0,
              "send-state update thread joins");
    }
    CHECK(pthread_barrier_destroy(&state_update_barrier) == 0,
          "state update barrier destroys");
    CHECK(io_failures == 0U, "all I/O-mode updates succeed");
    CHECK(send_failures == 0U, "all send-state updates succeed");

    mode = SOCK_IO_BLOCK;
    CHECK(ctlsocket(3U, CS_GET_IOMODE, &mode) == SOCK_OK,
          "socket 3 I/O mode reads back");
    CHECK(mode == SOCK_IO_NONBLOCK,
          "socket 3 I/O-mode state remains 1 after concurrent updates");
    mode = SOCK_IO_NONBLOCK;
    CHECK(ctlsocket(5U, CS_SET_IOMODE, &mode) == SOCK_OK,
          "socket 5 switches to nonblocking mode for send-state observation");
    fake_sn_ir = 0U;
    send_state = (send(5U, &byte, 1U) == SOCK_BUSY) ? 1U : 0U;
    CHECK(send_state == 1U,
          "socket 5 send state remains 1 after concurrent updates");
}

static int stress_socket_number(uint8_t block)
{
    if (block < WIZCHIP_SREG_BLOCK(0U) ||
        ((block - WIZCHIP_SREG_BLOCK(0U)) % 4U) != 0U) {
        return -1;
    }
    return (int)((block - WIZCHIP_SREG_BLOCK(0U)) / 4U);
}

static void stress_critical_enter(void)
{
    if (pthread_mutex_lock(&stress_bus_mutex) != 0) {
        record_violation(&ownership_violations);
    }
}

static void stress_critical_exit(void)
{
    if (pthread_mutex_unlock(&stress_bus_mutex) != 0) {
        record_violation(&ownership_violations);
    }
}

static void stress_select(void)
{
    stress_spi_addr = 0U;
    stress_spi_block = 0U;
    stress_header_bytes = 0U;
}

static void stress_deselect(void) {}

static uint8_t stress_read_socket(const stress_socket_t *socket_state,
                                  uint16_t address)
{
    switch (address) {
    case 0x0000U:
        return socket_state->mode;
    case 0x0001U:
        return 0U;
    case 0x0002U:
        return socket_state->interrupt;
    case 0x0003U:
        return socket_state->status;
    case 0x001EU:
    case 0x001FU:
        return 2U;
    case 0x0020U:
        return 0x08U;
    case 0x0021U:
        return 0U;
    case 0x0024U:
        return (uint8_t)(socket_state->tx_write_pointer >> 8);
    case 0x0025U:
        return (uint8_t)socket_state->tx_write_pointer;
    case 0x0026U:
        return 0U;
    case 0x0027U:
        return 1U;
    case 0x0028U:
        return (uint8_t)(socket_state->rx_read_pointer >> 8);
    case 0x0029U:
        return (uint8_t)socket_state->rx_read_pointer;
    default:
        return 0U;
    }
}

static uint8_t stress_spi_read(void)
{
    int sn = stress_socket_number(stress_spi_block);
    uint8_t value = 0U;

    if (sn >= 0 && sn < (int)SOCKET_COUNT) {
        value = stress_read_socket(&stress_sockets[sn], stress_spi_addr);
    } else if (stress_spi_block == WIZCHIP_CREG_BLOCK) {
        if (stress_spi_addr == 0x000FU) {
            value = 192U;
        } else if (stress_spi_addr == 0x0039U) {
            value = 0x04U;
        }
    } else if ((stress_spi_block % 4U) == 3U) {
        value = 0x5AU;
    }
    ++stress_spi_addr;
    return value;
}

static void stress_apply_command(stress_socket_t *socket_state, uint8_t command)
{
    switch (command) {
    case Sn_CR_OPEN:
        socket_state->status = ((socket_state->mode & 0x0FU) == Sn_MR_TCP)
                                   ? SOCK_INIT
                                   : SOCK_CLOSED;
        socket_state->interrupt = 0U;
        socket_state->tx_write_pointer = 0U;
        socket_state->rx_read_pointer = 0U;
        break;
    case Sn_CR_CONNECT:
        socket_state->status = SOCK_ESTABLISHED;
        break;
    case Sn_CR_SEND:
        socket_state->interrupt |= Sn_IR_SENDOK;
        break;
    case Sn_CR_RECV:
        break;
    case Sn_CR_DISCON:
    case Sn_CR_CLOSE:
        socket_state->status = SOCK_CLOSED;
        break;
    default:
        break;
    }
}

static void stress_write_socket(stress_socket_t *socket_state,
                                uint16_t address, uint8_t value)
{
    switch (address) {
    case 0x0000U:
        socket_state->mode = value;
        break;
    case 0x0001U:
        stress_apply_command(socket_state, value);
        break;
    case 0x0002U:
        socket_state->interrupt &= (uint8_t)~value;
        break;
    case 0x0024U:
        socket_state->tx_write_pointer =
            (uint16_t)((socket_state->tx_write_pointer & 0x00FFU) |
                       ((uint16_t)value << 8));
        break;
    case 0x0025U:
        socket_state->tx_write_pointer =
            (uint16_t)((socket_state->tx_write_pointer & 0xFF00U) | value);
        break;
    case 0x0028U:
        socket_state->rx_read_pointer =
            (uint16_t)((socket_state->rx_read_pointer & 0x00FFU) |
                       ((uint16_t)value << 8));
        break;
    case 0x0029U:
        socket_state->rx_read_pointer =
            (uint16_t)((socket_state->rx_read_pointer & 0xFF00U) | value);
        break;
    default:
        break;
    }
}

static void stress_spi_write(uint8_t value)
{
    int sn;

    if (stress_header_bytes == 0U) {
        stress_spi_addr = (uint16_t)value << 8;
        ++stress_header_bytes;
        return;
    }
    if (stress_header_bytes == 1U) {
        stress_spi_addr |= value;
        ++stress_header_bytes;
        return;
    }
    if (stress_header_bytes == 2U) {
        stress_spi_block = value >> 3;
        ++stress_header_bytes;
        return;
    }

    sn = stress_socket_number(stress_spi_block);
    if (sn >= 0 && sn < (int)SOCKET_COUNT) {
        stress_write_socket(&stress_sockets[sn], stress_spi_addr, value);
    }
    ++stress_spi_addr;
}

static void stress_spi_read_burst(uint8_t *buffer, uint16_t length)
{
    uint16_t i;

    for (i = 0U; i < length; ++i) {
        buffer[i] = stress_spi_read();
    }
}

static void stress_spi_write_burst(uint8_t *buffer, uint16_t length)
{
    uint16_t i;

    for (i = 0U; i < length; ++i) {
        stress_spi_write(buffer[i]);
    }
}

static uint8_t stress_spi_busy(void)
{
    return 0U;
}

static int8_t stress_spi_error(void)
{
    return 0;
}

static void stress_spi_clear_error(void) {}

static uint64_t stress_now_us(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0U;
    }
    return (uint64_t)now.tv_sec * 1000000U +
           (uint64_t)now.tv_nsec / 1000U;
}

static void install_stress_register_fake(void)
{
    reg_wizchip_cris_cbfunc(stress_critical_enter, stress_critical_exit);
    reg_wizchip_cs_cbfunc(stress_select, stress_deselect);
    reg_wizchip_spi_cbfunc(stress_spi_read, stress_spi_write);
    reg_wizchip_spiburst_cbfunc(stress_spi_read_burst,
                                stress_spi_write_burst);
    reg_wizchip_spistatus_cbfunc(stress_spi_busy, stress_spi_error,
                                 stress_spi_clear_error);
}

static uint8_t stress_expected_status(unsigned int operation)
{
    switch (operation) {
    case 0U:
        return SOCK_INIT;
    case 1U:
    case 2U:
    case 3U:
        return SOCK_ESTABLISHED;
    default:
        return SOCK_CLOSED;
    }
}

static int stress_operation_succeeded(unsigned int operation, uint8_t sn)
{
    uint8_t address[4] = {192U, 0U, 2U, (uint8_t)(sn + 1U)};
    uint8_t byte = (uint8_t)(0xA0U + sn);
    int32_t result;

    switch (operation) {
    case 0U:
        result = socket(sn, Sn_MR_TCP, (uint16_t)(5000U + sn), 0U);
        return result >= 0;
    case 1U:
        result = connect(sn, address, 80U);
        return result == SOCK_OK;
    case 2U:
        result = send(sn, &byte, 1U);
        return result == 1;
    case 3U:
        result = recv(sn, &byte, 1U);
        return result == 1;
    case 4U:
        result = disconnect(sn);
        return result == SOCK_OK;
    default:
        result = close(sn);
        return result == SOCK_OK;
    }
}

static void *stress_worker(void *argument)
{
    stress_worker_t *worker = argument;
    unsigned int iteration;

    for (iteration = 0U; iteration < STRESS_ITERATION_COUNT; ++iteration) {
        unsigned int operation = iteration % 6U;
        unsigned int cycle = iteration / 6U;
        uint8_t sn = (uint8_t)(worker->worker_index +
                               2U * (cycle % (SOCKET_COUNT / 2U)));
        uint8_t observed_status;

        worker->visited[sn] = 1U;
        if (!stress_operation_succeeded(operation, sn)) {
            ++worker->operation_failures;
        }
        if (pthread_mutex_lock(&stress_bus_mutex) != 0) {
            ++worker->operation_failures;
            continue;
        }
        observed_status = stress_sockets[sn].status;
        if (pthread_mutex_unlock(&stress_bus_mutex) != 0) {
            ++worker->operation_failures;
        }
        if (observed_status != stress_expected_status(operation)) {
            ++worker->state_losses;
        }
        ++worker->operations_completed;
    }
    return NULL;
}

static void watchdog_alarm_handler(int signal_number)
{
    static const char message[] =
        "FAIL: 120-second concurrency stress watchdog expired\n";
    ssize_t written;

    (void)signal_number;
    watchdog_deadline_overruns = 1;
    written = write(STDERR_FILENO, message, sizeof(message) - 1U);
    (void)written;
    _Exit(124);
}

static void *watchdog_worker(void *argument)
{
    (void)argument;
    (void)alarm(STRESS_WATCHDOG_SECONDS);
    if (pthread_mutex_lock(&watchdog_mutex) != 0) {
        watchdog_deadline_overruns = 1;
        return NULL;
    }
    while (watchdog_finished == 0) {
        if (pthread_cond_wait(&watchdog_condition, &watchdog_mutex) != 0) {
            watchdog_deadline_overruns = 1;
            break;
        }
    }
    (void)pthread_mutex_unlock(&watchdog_mutex);
    (void)alarm(0U);
    return NULL;
}

static void test_final_two_context_eight_socket_stress(void)
{
    pthread_t workers[STRESS_THREAD_COUNT];
    pthread_t watchdog;
    stress_worker_t worker_data[STRESS_THREAD_COUNT] = {{0}};
    uint8_t memory_sizes[SOCKET_COUNT] = {2U, 2U, 2U, 2U, 2U, 2U, 2U, 2U};
    struct sigaction action = {0};
    struct sigaction previous_action;
    struct timespec started;
    struct timespec finished;
    unsigned int created = 0U;
    unsigned int total_operations = 0U;
    unsigned int total_operation_failures = 0U;
    unsigned int total_state_losses = 0U;
    unsigned int visited_sockets = 0U;
    unsigned int i;
    int watchdog_created;

    action.sa_handler = watchdog_alarm_handler;
    CHECK(sigemptyset(&action.sa_mask) == 0, "watchdog signal mask initializes");
    CHECK(sigaction(SIGALRM, &action, &previous_action) == 0,
          "watchdog alarm handler installs");
    watchdog_finished = 0;
    watchdog_deadline_overruns = 0;
    watchdog_created = pthread_create(&watchdog, NULL, watchdog_worker, NULL);
    CHECK(watchdog_created == 0, "watchdog pthread creates");

    install_stress_register_fake();
    reg_wizchip_time_cbfunc(stress_now_us, NULL);
    reg_wizchip_lock_cbfunc(NULL, NULL, NULL, NULL);
    for (i = 0U; i < SOCKET_COUNT; ++i) {
        stress_sockets[i].mode = 0U;
        stress_sockets[i].status = SOCK_CLOSED;
        stress_sockets[i].interrupt = 0U;
        stress_sockets[i].tx_write_pointer = 0U;
        stress_sockets[i].rx_read_pointer = 0U;
    }
    if (wizchip_get_state() == WIZCHIP_STATE_FAULTED) {
        CHECK(wizchip_recover() == 0,
              "stress setup recovers prior injected chip faults");
    }
    wizchip_clear_last_error();
    CHECK(wizchip_init(memory_sizes, memory_sizes) == 0,
          "stress model initializes all eight socket buffers");
    reset_counts();
    reg_wizchip_lock_cbfunc(socket_enter, socket_exit,
                            global_enter, global_exit);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &started) == 0,
          "stress start time is recorded");

    for (i = 0U; i < STRESS_THREAD_COUNT; ++i) {
        int rc;

        worker_data[i].worker_index = i;
        rc = pthread_create(&workers[i], NULL, stress_worker, &worker_data[i]);
        CHECK(rc == 0, "stress pthread creates");
        if (rc != 0) {
            break;
        }
        ++created;
    }
    for (i = 0U; i < created; ++i) {
        CHECK(pthread_join(workers[i], NULL) == 0, "stress pthread joins");
    }
    reg_wizchip_time_cbfunc(NULL, NULL);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &finished) == 0,
          "stress finish time is recorded");

    if (pthread_mutex_lock(&watchdog_mutex) == 0) {
        watchdog_finished = 1;
        CHECK(pthread_cond_signal(&watchdog_condition) == 0,
              "watchdog completion is signaled");
        CHECK(pthread_mutex_unlock(&watchdog_mutex) == 0,
              "watchdog completion mutex unlocks");
    } else {
        CHECK(0, "watchdog completion mutex locks");
    }
    if (watchdog_created == 0) {
        CHECK(pthread_join(watchdog, NULL) == 0, "watchdog pthread joins");
    }
    (void)alarm(0U);
    CHECK(sigaction(SIGALRM, &previous_action, NULL) == 0,
          "watchdog alarm handler restores");

    for (i = 0U; i < STRESS_THREAD_COUNT; ++i) {
        unsigned int sn;

        total_operations += worker_data[i].operations_completed;
        total_operation_failures += worker_data[i].operation_failures;
        total_state_losses += worker_data[i].state_losses;
        for (sn = 0U; sn < SOCKET_COUNT; ++sn) {
            visited_sockets += worker_data[i].visited[sn];
        }
    }
    CHECK(created == STRESS_THREAD_COUNT, "both stress contexts run");
    CHECK(total_operations == STRESS_THREAD_COUNT * STRESS_ITERATION_COUNT,
          "exactly 10,000 mixed socket operations complete");
    CHECK(visited_sockets == SOCKET_COUNT, "stress covers all eight sockets");
    CHECK(total_operation_failures == 0U,
          "all mixed socket operations return expected results");
    CHECK(total_state_losses == 0U, "stress records zero socket state loss");
    CHECK(socket_acquires == socket_releases,
          "stress socket lock enter and exit counts are balanced");
    CHECK(global_acquires == global_releases,
          "stress global lock enter and exit counts are balanced");
    CHECK(ownership_violations == 0U,
          "stress records zero lock ownership violations");
    CHECK(watchdog_deadline_overruns == 0,
          "stress records zero 120-second deadline overruns");
    CHECK((finished.tv_sec - started.tv_sec) <
              (time_t)STRESS_WATCHDOG_SECONDS,
          "stress completes before the watchdog deadline");
}

static void initialize_mutexes(void)
{
    pthread_mutexattr_t attributes;
    unsigned int i;

    CHECK(pthread_mutexattr_init(&attributes) == 0, "mutex attributes initialize");
    CHECK(pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_ERRORCHECK) == 0,
          "error-checking mutex type configured");
    CHECK(pthread_mutex_init(&global_mutex, &attributes) == 0,
          "global mutex initializes");
    for (i = 0U; i < SOCKET_COUNT; ++i) {
        CHECK(pthread_mutex_init(&socket_mutexes[i], &attributes) == 0,
              "socket mutex initializes");
    }
    CHECK(pthread_mutexattr_destroy(&attributes) == 0,
          "mutex attributes destroy");
}

static void destroy_mutexes(void)
{
    unsigned int i;

    CHECK(pthread_mutex_destroy(&global_mutex) == 0, "global mutex destroys");
    for (i = 0U; i < SOCKET_COUNT; ++i) {
        CHECK(pthread_mutex_destroy(&socket_mutexes[i]) == 0,
              "socket mutex destroys");
    }
}

int main(void)
{
    initialize_mutexes();
    test_complete_pair_installation();
    test_null_callbacks_are_safe_defaults();
    test_lock_balance_non_recursive_and_ordered();
    test_socket_rejects_invalid_number_before_lock();
    test_socket_rejects_occupied_slot_after_lock();
    test_socket_and_listen_unlock_once_per_locked_exit();
    test_listen_revalidates_mutable_state_after_lock();
    test_socket_does_not_recurse_through_public_close();
    test_connect_rejects_null_address_before_lock();
    test_connect_rejects_zero_port();
    test_connect_rejects_macraw_and_unlocks_once();
    test_connect_requires_init_state_and_unlocks_once();
    test_connect_success_unlocks_once();
    test_disconnect_rejects_invalid_number_before_lock();
    test_disconnect_mode_failure_unlocks_once();
    test_disconnect_success_unlocks_once();
    test_close_acquires_one_lock_and_cleans_up_while_owned();
    test_stream_io_rejects_invalid_number_before_lock();
    test_stream_io_rejects_null_nonzero_buffer_before_lock();
    test_stream_io_zero_length_is_pre_access_noop();
    test_recv_close_wait_cleanup_is_lock_owned();
    test_stream_io_unlocks_once_on_locked_exits();
    test_simultaneous_io_mode_and_send_state_updates();
    test_final_two_context_eight_socket_stress();
    reg_wizchip_lock_cbfunc(NULL, NULL, NULL, NULL);
    destroy_mutexes();

    if (failures != 0U) {
        fprintf(stderr, "%u test failure(s)\n", failures);
        return 1;
    }
    printf("PASS: lock registration concurrency regressions\n");
    return 0;
}
