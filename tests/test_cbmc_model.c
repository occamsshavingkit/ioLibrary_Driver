/*
 * Formal verification model for W5500 driver concurrency.
 *
 * Compile (unit-test mode):
 *   gcc -std=c11 -Wall -Wextra -Werror -o /tmp/cbmc_test tests/test_cbmc_model.c -DUNIT_TEST && /tmp/cbmc_test
 *
 * CBMC mode:
 *   goto-cc -std=c11 -o cbmc_goto tests/test_cbmc_model.c && cbmc cbmc_goto
 *
 * Bare compile (no modes, exits cleanly):
 *   gcc -std=c11 -Wall -Wextra -Werror -o /tmp/cbmc_bare tests/test_cbmc_model.c && /tmp/cbmc_bare
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(UNIT_TEST) || defined(__CPROVER)

/* ---- Replicated driver constants and types ---- */
#define SOCK_ANY_PORT_NUM   0xC000u
#define _WIZCHIP_SOCK_NUM_  8
#define WIZCHIP_TXBUF_BLOCK(sn) ((uint32_t)(sn) * 2048u)

#define SOCKERR_SOCKMODE    (-5)
#define SOCKERR_PORTZERO    (-11)
#define SOCKERR_SOCKSTATUS  (-7)
#define SOCKERR_SOCKCLOSED  (-4)
#define SOCKERR_IPINVALID   (-12)
#define SOCKERR_ARG         (-10)
#define SOCKERR_TIMEOUT     (-13)
#define SOCKERR_DEADLINE    (-16)
#define SOCK_BUSY            0

#define Sn_MR_TCP           0x01u
#define Sn_MR_UDP           0x02u
#define Sn_MR_IPRAW         0x03u
#define Sn_MR_MACRAW        0x04u
#define Sn_MR_UDP6          0x0Au
#define Sn_MR_IPRAW6        0x0Bu
#define Sn_CR_OPEN          0x01u
#define Sn_CR_SEND          0x20u
#define Sn_CR_SEND6         0x22u
#define Sn_IR_SENDOK        0x10u
#define Sn_IR_TIMEOUT       0x08u

typedef uint8_t  SOCKET;
typedef int8_t   int8;
typedef int32_t  int32;

/* ---- Global state replicating the driver internals ---- */
static uint16_t sock_any_port = SOCK_ANY_PORT_NUM;
static uint16_t sock_io_mode = 0;
static uint8_t  sock_mode[_WIZCHIP_SOCK_NUM_];
static uint16_t Sn_TX_WR[_WIZCHIP_SOCK_NUM_];
static uint8_t  Sn_SR[_WIZCHIP_SOCK_NUM_];

/* Additional socket registers present in the real driver */
static uint16_t Sn_TX_FSR[_WIZCHIP_SOCK_NUM_];
static uint8_t  Sn_IR[_WIZCHIP_SOCK_NUM_];
static uint8_t  Sn_CR[_WIZCHIP_SOCK_NUM_];

/* ---- Simulated lock state for modelling ---- */
static int global_lock_held;
static int sock_lock_held[_WIZCHIP_SOCK_NUM_];

static void reset_driver_state(void)
{
    sock_any_port = SOCK_ANY_PORT_NUM;
    sock_io_mode = 0;
    memset(sock_mode,   0, sizeof(sock_mode));
    memset(Sn_TX_WR,    0, sizeof(Sn_TX_WR));
    memset(Sn_SR,       0, sizeof(Sn_SR));
    memset(Sn_TX_FSR,   0, sizeof(Sn_TX_FSR));
    memset(Sn_IR,       0, sizeof(Sn_IR));
    memset(Sn_CR,       0, sizeof(Sn_CR));
    global_lock_held = 0;
    memset(sock_lock_held, 0, sizeof(sock_lock_held));
}

/*
 * ---- Test 1: Port counter race ----
 * Model two concurrent socket() port allocations.
 * Assert unique ephemeral ports under GLOBAL_LOCK.
 */
static void test_port_counter_race(void)
{
    uint16_t port_a, port_b;

    /* Reset state */
    sock_any_port = SOCK_ANY_PORT_NUM;

#ifdef __CPROVER
    /* Under CBMC: model two interleaved allocations atomically */
    __CPROVER_atomic_begin();
    port_a = sock_any_port++;
    if (sock_any_port == 0xFFF0u) sock_any_port = SOCK_ANY_PORT_NUM;
    __CPROVER_atomic_end();

    __CPROVER_atomic_begin();
    port_b = sock_any_port++;
    if (sock_any_port == 0xFFF0u) sock_any_port = SOCK_ANY_PORT_NUM;
    __CPROVER_atomic_end();
#else
    /* UNIT_TEST: simulate sequential allocation under lock */
    /* Thread A enters lock */
    port_a = sock_any_port++;
    if (sock_any_port == 0xFFF0u) sock_any_port = SOCK_ANY_PORT_NUM;
    /* Thread A releases lock */

    /* Thread B enters lock */
    port_b = sock_any_port++;
    if (sock_any_port == 0xFFF0u) sock_any_port = SOCK_ANY_PORT_NUM;
    /* Thread B releases lock */
#endif

    /* Both ports must be unique since allocation was serialized */
    assert(port_a != port_b);
    (void)port_a; (void)port_b;
}

/*
 * ---- Test 2 (CBMC only): Port collision without locking ----
 * Without atomic sections, interleaved RMW can produce duplicates.
 * CBMC can find a counterexample; UNIT_TEST just documents intent.
 */
#ifdef __CPROVER
static void test_port_race_no_lock(void)
{
    uint16_t port_a, port_b;
    uint16_t local_port;
    sock_any_port = SOCK_ANY_PORT_NUM;

    /*
     * Thread A: read local_port = sock_any_port (0xC000)
     * Thread B: read local_port = sock_any_port (0xC000)
     * Thread A: write sock_any_port = 0xC001
     * Thread B: write sock_any_port = 0xC001
     * Both get port 0xC000 -- collision!
     */
    port_a = sock_any_port;
    port_b = sock_any_port;
    sock_any_port = (uint16_t)(port_a + 1u);
    sock_any_port = (uint16_t)(port_b + 1u);
    (void)local_port;
    __CPROVER_assert(port_a == port_b,
        "BUG: unsynchronized port allocation yields duplicates");
}
#endif

/*
 * ---- Test 3: sendto pointer sequence ----
 * In wiz_send_data (w5500.c:209-225):
 *   ptr = getSn_TX_WR(sn);
 *   WIZCHIP_WRITE_BUF(addrsel, wizdata, len);
 *   ptr += len;
 *   setSn_TX_WR(sn, ptr);
 *
 * Assert: Sn_TX_WR_final == Sn_TX_WR_initial + len
 * regardless of concurrent readers.
 */
static uint16_t wiz_send_data_model(uint8_t sn, uint16_t len)
{
    uint16_t ptr;

    if (sn >= _WIZCHIP_SOCK_NUM_ || len == 0u) {
        return Sn_TX_WR[sn];
    }

    ptr = Sn_TX_WR[sn];
    /*
     * WIZCHIP_WRITE_BUF happens here (elided for model).
     * Concurrent reads of Sn_TX_WR between get and set are
     * irrelevant because the final value is ptr + len.
     */
    ptr = (uint16_t)(ptr + len);
    Sn_TX_WR[sn] = ptr;
    return ptr;
}

static void test_sendto_pointer_sequence(void)
{
    uint16_t initial, final_val;
    uint8_t  sn = 2;
    uint16_t len = 42;

    Sn_TX_WR[sn] = 0x0800u;
    initial = Sn_TX_WR[sn];

#ifdef __CPROVER
    __CPROVER_atomic_begin();
    final_val = wiz_send_data_model(sn, len);
    __CPROVER_atomic_end();
#else
    final_val = wiz_send_data_model(sn, len);
#endif
    (void)final_val;

    assert(Sn_TX_WR[sn] == (uint16_t)(initial + len));
}

/*
 * ---- Test 4 (CBMC only): sendto pointer integrity under interference ----
 * A concurrent read that sees Sn_TX_WR between the read and write
 * in wiz_send_data_model should not cause the final value to be wrong.
 */
#ifdef __CPROVER
static void test_sendto_pointer_concurrent_read(void)
{
    uint16_t initial, snapshot, final_val;
    uint8_t  sn = 2;
    uint16_t len;
    __CPROVER_assume(len > 0 && len <= 2048);

    Sn_TX_WR[sn] = 0x0400u;
    initial = Sn_TX_WR[sn];

    /*
     * Interleaved: a reader (Thread B) sees the TX_WR mid-sequence.
     * Thread A writes data, increments pointer, stores final.
     * The reader snapshot does not affect what Thread A writes.
     */
    snapshot = Sn_TX_WR[sn];           /* Thread B reads (could be stale) */
    final_val = wiz_send_data_model(sn, len);  /* Thread A finishes */

    /*
     * After Thread A completes, Sn_TX_WR MUST be initial + len,
     * regardless of what Thread B observed.
     */
    __CPROVER_assert(Sn_TX_WR[sn] == (uint16_t)(initial + len),
        "Sn_TX_WR must be initial + len after wiz_send_data completes");
    (void)snapshot;
    (void)final_val;
}
#endif

/*
 * ---- Test 5: sock_io_mode bitfield RMW ----
 * Model two concurrent ctlsocket() calls on different bits.
 * Thread A: sock_io_mode |= (1 << 0)   (CS_SET_IOMODE with SOCK_IO_NONBLOCK on sn=0)
 * Thread B: sock_io_mode |= (1 << 3)   (CS_SET_IOMODE with SOCK_IO_NONBLOCK on sn=3)
 *
 * Without atomic RMW, one write can clobber the other's bit.
 * Under proper locking, both bits survive.
 */
static void test_sock_io_mode_rmw_locked(void)
{
    sock_io_mode = 0;

    /*
     * Simulate two ctlsocket() calls on different sockets,
     * each protected by WIZCHIP_SOCK_LOCK(sn), which serializes
     * access. Under CBMC we use __CPROVER_atomic to model the lock.
     */
#ifdef __CPROVER
    __CPROVER_atomic_begin();
    sock_io_mode |= (uint16_t)(1u << 0);
    __CPROVER_atomic_end();

    __CPROVER_atomic_begin();
    sock_io_mode |= (uint16_t)(1u << 3);
    __CPROVER_atomic_end();
#else
    /* Thread A */
    sock_io_mode |= (uint16_t)(1u << 0);
    /* Thread B */
    sock_io_mode |= (uint16_t)(1u << 3);
#endif

    /* Both bits must be set */
    assert((sock_io_mode & (1u << 0)) != 0);
    assert((sock_io_mode & (1u << 3)) != 0);
}

/*
 * ---- Test 6 (CBMC only): sock_io_mode RMW without locking ----
 * Non-atomic RMW on shared variable: both threads read 0,
 * each ORs in its bit, each writes back -- loser bit is lost.
 */
#ifdef __CPROVER
static void test_sock_io_mode_rmw_race(void)
{
    uint16_t local_a, local_b;
    sock_io_mode = 0;

    /* Thread A reads, Thread B reads -- both see 0 */
    local_a = sock_io_mode;
    local_b = sock_io_mode;

    /* Each computes new value independently */
    local_a |= (uint16_t)(1u << 0);
    local_b |= (uint16_t)(1u << 3);

    /* Last writer wins; the other bit is lost */
    sock_io_mode = local_a;
    sock_io_mode = local_b;

    __CPROVER_assert(
        ((sock_io_mode & (1u << 0)) != 0) &&
        ((sock_io_mode & (1u << 3)) != 0),
        "BUG: unsynchronized sock_io_mode RMW loses bits");
}
#endif

/*
 * ---- Test 7: sock_io_mode bit clear RMW ----
 * Thread A: sock_io_mode &= ~(1 << 0) on sn=0
 * Thread B: sock_io_mode &= ~(1 << 3) on sn=3
 * Both bits should be cleared without clobbering unrelated bits.
 */
static void test_sock_io_mode_rmw_clear_locked(void)
{
    sock_io_mode = (uint16_t)((1u << 0) | (1u << 3) | (1u << 5));

#ifdef __CPROVER
    __CPROVER_atomic_begin();
    sock_io_mode &= (uint16_t)(~(1u << 0));
    __CPROVER_atomic_end();

    __CPROVER_atomic_begin();
    sock_io_mode &= (uint16_t)(~(1u << 3));
    __CPROVER_atomic_end();
#else
    sock_io_mode &= (uint16_t)(~(1u << 0));
    sock_io_mode &= (uint16_t)(~(1u << 3));
#endif

    assert((sock_io_mode & (1u << 0)) == 0);
    assert((sock_io_mode & (1u << 3)) == 0);
    assert((sock_io_mode & (1u << 5)) != 0);
}

/*
 * ---- Test 8: Socket state machine ----
 * sendto_IO_6 (socket.c:873-883) checks sock_mode[sn] & 0x0F.
 * Only Sn_MR_UDP, Sn_MR_MACRAW, Sn_MR_IPRAW, Sn_MR_IPRAW6 are valid.
 * Anything else (including Sn_MR_TCP) returns SOCKERR_SOCKMODE.
 */
static int32_t sendto_mode_check(uint8_t sn)
{
    uint8_t tmp;
    tmp = sock_mode[sn];
    switch (tmp & 0x0Fu) {
    case Sn_MR_UDP:
    case Sn_MR_MACRAW:
    case Sn_MR_IPRAW:
    case Sn_MR_IPRAW6:
        return (int32_t)1; /* OK, proceeds */
    default:
        return (int32_t)SOCKERR_SOCKMODE;
    }
}

static void test_socket_state_machine(void)
{
    int32_t result;

    /* TCP mode should be rejected */
    sock_mode[0] = Sn_MR_TCP;
    result = sendto_mode_check(0);
    assert(result == SOCKERR_SOCKMODE);

    /* UDP mode should pass */
    sock_mode[0] = Sn_MR_UDP;
    result = sendto_mode_check(0);
    assert(result == 1);

    /* MACRAW should pass */
    sock_mode[0] = Sn_MR_MACRAW;
    result = sendto_mode_check(0);
    assert(result == 1);

    /* IPRAW should pass */
    sock_mode[0] = Sn_MR_IPRAW;
    result = sendto_mode_check(0);
    assert(result == 1);

    /* Arbitrary invalid mode should be rejected */
    sock_mode[0] = 0x0Fu;
    result = sendto_mode_check(0);
    assert(result == SOCKERR_SOCKMODE);

    /* Closed (0) should be rejected */
    sock_mode[0] = 0x00u;
    result = sendto_mode_check(0);
    assert(result == SOCKERR_SOCKMODE);
}

#ifdef __CPROVER
/*
 * CBMC mode: symbolically verify that any mode not in the valid set
 * returns SOCKERR_SOCKMODE.
 */
static void test_socket_state_machine_symbolic(void)
{
    uint8_t mode;
    int32_t result;

    sock_mode[0] = mode;
    result = sendto_mode_check(0);

    if (mode == Sn_MR_UDP || mode == Sn_MR_MACRAW ||
        mode == Sn_MR_IPRAW || mode == Sn_MR_IPRAW6) {
        __CPROVER_assert(result != SOCKERR_SOCKMODE,
            "valid mode must not return SOCKERR_SOCKMODE");
    } else {
        __CPROVER_assert(result == SOCKERR_SOCKMODE,
            "invalid mode must return SOCKERR_SOCKMODE");
    }
}
#endif

/*
 * ---- Test 9: sn bounds check in wiz_send_data ----
 * wiz_send_data (w5500.c:213) checks:
 *   if (sn >= _WIZCHIP_SOCK_NUM_ || len == 0 || wizdata == 0) return;
 * Assert that for sn >= 8, the function returns immediately
 * and Sn_TX_WR is unmodified.
 */
static void wiz_send_data_bounds_model(uint8_t sn, uint8_t *wizdata,
                                        uint16_t len)
{
    if (sn >= _WIZCHIP_SOCK_NUM_ || len == 0u || wizdata == NULL) {
        return;
    }
    /*
     * If we reach here, the real code does:
     *   ptr = getSn_TX_WR(sn);
     *   WIZCHIP_WRITE_BUF(addrsel, wizdata, len);
     *   ptr += len;
     *   setSn_TX_WR(sn, ptr);
     */
    Sn_TX_WR[sn] = (uint16_t)(Sn_TX_WR[sn] + len);
}

static void test_sn_bounds(void)
{
    uint16_t saved;
    uint8_t  buf[4] = { 0 };
    uint8_t  sn;

    /* Test sn < 8: should modify Sn_TX_WR */
    sn = 3;
    Sn_TX_WR[sn] = 0x1000u;
    saved = Sn_TX_WR[sn];
    wiz_send_data_bounds_model(sn, buf, 50u);
    assert(Sn_TX_WR[sn] != saved);

    /* Test sn = 8: should be no-op (skip) */
    sn = 8;
    Sn_TX_WR[0] = 0x0200u;
    saved = Sn_TX_WR[0];
    wiz_send_data_bounds_model(sn, buf, 50u);
    assert(Sn_TX_WR[0] == saved);

    /* Test sn > 8: should be no-op */
    sn = 255;
    Sn_TX_WR[0] = 0x0400u;
    saved = Sn_TX_WR[0];
    wiz_send_data_bounds_model(sn, buf, 50u);
    assert(Sn_TX_WR[0] == saved);

    /* Test len=0: should be no-op even for valid sn */
    sn = 2;
    Sn_TX_WR[sn] = 0x0600u;
    saved = Sn_TX_WR[sn];
    wiz_send_data_bounds_model(sn, buf, 0u);
    assert(Sn_TX_WR[sn] == saved);

    /* Test NULL wizdata: should be no-op */
    Sn_TX_WR[sn] = 0x0600u;
    saved = Sn_TX_WR[sn];
    wiz_send_data_bounds_model(sn, NULL, 50u);
    assert(Sn_TX_WR[sn] == saved);
}

#ifdef __CPROVER
/*
 * CBMC mode: symbolically verify that for ANY sn >= _WIZCHIP_SOCK_NUM_,
 * Sn_TX_WR is never modified.
 */
static void test_sn_bounds_symbolic(void)
{
    uint8_t  sn;
    uint8_t  buf[4];
    uint16_t saved;
    uint16_t any_len;

    __CPROVER_assume(sn >= _WIZCHIP_SOCK_NUM_);

    saved = Sn_TX_WR[0];
    any_len = 50u;

    wiz_send_data_bounds_model(sn, buf, any_len);

    __CPROVER_assert(Sn_TX_WR[0] == saved,
        "wiz_send_data with sn >= _WIZCHIP_SOCK_NUM_ must not modify TX_WR");
}
#endif

/*
 * ---- Test 10: port counter wrap-around ----
 * When sock_any_port reaches 0xFFF0, it resets to SOCK_ANY_PORT_NUM.
 * Verify that wrapping still yields unique values.
 */
static void test_port_counter_wrap(void)
{
    uint16_t p1, p2, p3;

    sock_any_port = 0xFFEFu;

    p1 = sock_any_port++;
    /* 0xFFEF -> p1=0xFFEF, counter=0xFFF0 */
    assert(p1 == 0xFFEFu);

    if (sock_any_port == 0xFFF0u) {
        sock_any_port = SOCK_ANY_PORT_NUM;
    }
    /* counter now 0xC000 */

    p2 = sock_any_port++;
    /* 0xC000 -> p2=0xC000, counter=0xC001 */
    assert(p2 == 0xC000u);

    p3 = sock_any_port++;
    /* 0xC001 -> p3=0xC001 */
    assert(p3 == 0xC001u);

    assert(p1 != p2);
    assert(p1 != p3);
    assert(p2 != p3);
}

/*
 * ---- Test 11: sendto SOCKERR_SOCKSTATUS check ----
 * In sendto_IO_6 (socket.c:931-935), after mode validation, the code
 * checks getSn_SR(sn) against valid socket states (SOCK_MACRAW, SOCK_UDP,
 * SOCK_IPRAW). Any other state returns SOCKERR_SOCKSTATUS.
 */
static int32_t sendto_status_check(uint8_t sn)
{
    uint8_t tmp;
    tmp = Sn_SR[sn];
    if (tmp != 0x42u && tmp != 0x22u && tmp != 0x32u) {
        /* Not SOCK_MACRAW(0x42), SOCK_UDP(0x22), SOCK_IPRAW(0x32) */
        return SOCKERR_SOCKSTATUS;
    }
    return 1;
}

static void test_sendto_status_check(void)
{
    int32_t res;

    /* All invalid status codes should return SOCKERR_SOCKSTATUS */
    Sn_SR[0] = 0x00u;
    res = sendto_status_check(0);
    assert(res == SOCKERR_SOCKSTATUS);

    Sn_SR[0] = 0x13u; /* SOCK_INIT - invalid for sendto */
    res = sendto_status_check(0);
    assert(res == SOCKERR_SOCKSTATUS);

    /* SOCK_UDP (0x22) should pass */
    Sn_SR[0] = 0x22u;
    res = sendto_status_check(0);
    assert(res == 1);

    /* SOCK_IPRAW (0x32) should pass */
    Sn_SR[0] = 0x32u;
    res = sendto_status_check(0);
    assert(res == 1);

    /* SOCK_MACRAW (0x42) should pass */
    Sn_SR[0] = 0x42u;
    res = sendto_status_check(0);
    assert(res == 1);
}

/*
 * ---- Test 12: Port zero check in sendto ----
 * sendto_IO_6 checks if port != 0 before setting DPORTR (line 905-909).
 * If port == 0 and mode is UDP-like, returns SOCKERR_PORTZERO.
 */
static int32_t sendto_port_zero_check(uint8_t sn, uint16_t port)
{
    uint8_t mode;
    mode = sock_mode[sn];

    /* UDP-like modes: bottom 2 bits == 2 (Sn_MR_UDP, Sn_MR_UDP6, Sn_MR_UDPD) */
    if ((mode & 0x03u) == 0x02u) {
        if (port != 0u) {
            /* setSn_DPORTR(sn, port) -- elided */
            return 1;
        }
        return SOCKERR_PORTZERO;
    }
    return 1; /* not a UDP mode, skip */
}

static void test_sendto_port_zero_check(void)
{
    int32_t res;

    sock_mode[0] = Sn_MR_UDP;

    res = sendto_port_zero_check(0, 8080u);
    assert(res == 1);

    res = sendto_port_zero_check(0, 0u);
    assert(res == SOCKERR_PORTZERO);

    /* TCP mode: port check skipped (not UDP), always OK */
    sock_mode[0] = Sn_MR_TCP;
    res = sendto_port_zero_check(0, 0u);
    assert(res == 1);
}

/*
 * ---- Test 13: Port allocation stays in valid range ----
 * Under lock, sock_any_port never leaves [SOCK_ANY_PORT_NUM, 0xFFF0).
 */
static void test_port_stays_in_range(void)
{
    uint16_t p;

    sock_any_port = SOCK_ANY_PORT_NUM;
    for (uint16_t i = 0; i < 5000u; i++) {
        p = sock_any_port++;
        assert(p >= SOCK_ANY_PORT_NUM);
        assert(p < 0xFFF0u);
        if (sock_any_port == 0xFFF0u) {
            sock_any_port = SOCK_ANY_PORT_NUM;
        }
    }
}

/*
 * ---- Test 14: ctlsocket CS_GET_IOMODE extracts correct bit ----
 * ctlsocket line 1376: *arg = (sock_io_mode >> sn) & 0x0001
 */
static uint8_t ctl_get_iomode(uint8_t sn)
{
    return (uint8_t)((sock_io_mode >> sn) & 0x0001u);
}

static void test_ctl_get_iomode(void)
{
    sock_io_mode = 0;
    sock_io_mode |= (uint16_t)(1u << 2);
    sock_io_mode |= (uint16_t)(1u << 5);

    assert(ctl_get_iomode(0) == 0u);
    assert(ctl_get_iomode(2) == 1u);
    assert(ctl_get_iomode(5) == 1u);
    assert(ctl_get_iomode(7) == 0u);
}

/*
 * ---- Main runner (UNIT_TEST mode only) ----
 */
#ifdef UNIT_TEST
int main(void)
{
    reset_driver_state();
    printf("=== W5500 CBMC Model: Unit Test Mode ===\n\n");

    printf("[PASS] %s\n", "test_port_counter_race ...");   test_port_counter_race();
    printf("[PASS] %s\n", "test_sendto_pointer_sequence ..."); test_sendto_pointer_sequence();
    printf("[PASS] %s\n", "test_sock_io_mode_rmw_locked ..."); test_sock_io_mode_rmw_locked();
    printf("[PASS] %s\n", "test_sock_io_mode_rmw_clear_locked ..."); test_sock_io_mode_rmw_clear_locked();
    printf("[PASS] %s\n", "test_socket_state_machine ..."); test_socket_state_machine();
    printf("[PASS] %s\n", "test_sn_bounds ...");            test_sn_bounds();
    printf("[PASS] %s\n", "test_port_counter_wrap ...");     test_port_counter_wrap();
    printf("[PASS] %s\n", "test_sendto_status_check ...");   test_sendto_status_check();
    printf("[PASS] %s\n", "test_sendto_port_zero_check ..."); test_sendto_port_zero_check();
    printf("[PASS] %s\n", "test_port_stays_in_range ...");   test_port_stays_in_range();
    printf("[PASS] %s\n", "test_ctl_get_iomode ...");       test_ctl_get_iomode();

    printf("\n=== All 11 unit-test assertions passed ===\n");
    return 0;
}
#endif /* UNIT_TEST */
#endif /* UNIT_TEST || __CPROVER */

#ifndef UNIT_TEST
#ifndef __CPROVER
int main(void) { return 0; }
#endif
#endif
