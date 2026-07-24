#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "socket.h"

#define FLAG_COUNT 256u
#define UNKNOWN_LOW_BITS 0x0Eu

/* This exhaustive boundary test keeps a minimal frame scanner so every flag
 * case can prove zero CS transactions on rejection and exactly one OPEN
 * command on acceptance. Register semantics remain covered by the shared
 * W5500 model in the other production-linked tests. */
typedef struct {
    uint8_t mode;
    uint8_t status;
    uint16_t port;
} fake_socket_t;

static fake_socket_t fake_sockets[_WIZCHIP_SOCK_NUM_];
static unsigned int failures;
static unsigned int checks;
static unsigned int spi_transactions;
static unsigned int open_commands;
static uint16_t spi_address;
static uint8_t spi_control;
static uint8_t spi_header_bytes;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL [%d]: %s\n", __LINE__, (message)); \
    } \
} while (0)

static void fake_critical_enter(void) {}
static void fake_critical_exit(void) {}

static void fake_select(void)
{
    spi_address = 0u;
    spi_control = 0u;
    spi_header_bytes = 0u;
    ++spi_transactions;
}

static void fake_deselect(void) {}

static int fake_socket_number(void)
{
    uint8_t block = (uint8_t)(spi_control >> 3);

    if (block < 1u || ((block - 1u) % 4u) != 0u) {
        return -1;
    }
    return (int)((block - 1u) / 4u);
}

static uint8_t open_status(uint8_t mode)
{
    switch (mode & 0x0Fu) {
    case Sn_MR_TCP:
        return SOCK_INIT;
    case Sn_MR_UDP:
        return SOCK_UDP;
    case Sn_MR_MACRAW:
        return SOCK_MACRAW;
    case Sn_MR_IPRAW:
        return SOCK_IPRAW;
    default:
        return SOCK_CLOSED;
    }
}

static uint8_t fake_spi_read(void)
{
    int sn = fake_socket_number();
    uint8_t value = 0u;

    if (sn >= 0 && sn < _WIZCHIP_SOCK_NUM_) {
        switch (spi_address) {
        case 0x0000u:
            value = fake_sockets[sn].mode;
            break;
        case 0x0001u:
            value = 0u;
            break;
        case 0x0003u:
            value = fake_sockets[sn].status;
            break;
        case 0x0004u:
            value = (uint8_t)(fake_sockets[sn].port >> 8);
            break;
        case 0x0005u:
            value = (uint8_t)fake_sockets[sn].port;
            break;
        default:
            break;
        }
    } else {
        if (spi_address >= 0x000Fu && spi_address <= 0x0012u) {
            value = (spi_address == 0x000Fu) ? 192u : 1u;
        } else if (spi_address == 0x0039u) {
            value = 0x04u;
        }
    }
    ++spi_address;
    return value;
}

static void fake_spi_write(uint8_t value)
{
    int sn;

    if (spi_header_bytes == 0u) {
        spi_address = (uint16_t)value << 8;
        ++spi_header_bytes;
        return;
    }
    if (spi_header_bytes == 1u) {
        spi_address |= value;
        ++spi_header_bytes;
        return;
    }
    if (spi_header_bytes == 2u) {
        spi_control = value;
        ++spi_header_bytes;
        return;
    }

    sn = fake_socket_number();
    if (sn >= 0 && sn < _WIZCHIP_SOCK_NUM_) {
        switch (spi_address) {
        case 0x0000u:
            fake_sockets[sn].mode = value;
            break;
        case 0x0001u:
            if (value == Sn_CR_OPEN) {
                ++open_commands;
                fake_sockets[sn].status = open_status(fake_sockets[sn].mode);
            } else if (value == Sn_CR_CLOSE) {
                fake_sockets[sn].status = SOCK_CLOSED;
            }
            break;
        case 0x0004u:
            fake_sockets[sn].port = (uint16_t)value << 8;
            break;
        case 0x0005u:
            fake_sockets[sn].port |= value;
            break;
        default:
            break;
        }
    } else if (spi_address == 0x0000u && value == MR_RST) {
        memset(fake_sockets, 0, sizeof(fake_sockets));
    }
    ++spi_address;
}

static uint8_t fake_spi_busy(void)
{
    return 0u;
}

static int8_t fake_spi_error(void)
{
    return 0;
}

static void fake_spi_clear_error(void) {}

static void install_fake(void)
{
    reg_wizchip_cris_cbfunc(fake_critical_enter, fake_critical_exit);
    reg_wizchip_cs_cbfunc(fake_select, fake_deselect);
    reg_wizchip_spi_cbfunc(fake_spi_read, fake_spi_write);
    reg_wizchip_spiburst_cbfunc(0, 0);
    reg_wizchip_spistatus_cbfunc(fake_spi_busy, fake_spi_error,
                                 fake_spi_clear_error);
}

static void reset_case(void)
{
    memset(fake_sockets, 0, sizeof(fake_sockets));
    wizchip_socket_state_reset();
    spi_transactions = 0u;
    open_commands = 0u;
}

static uint8_t expected_hardware_flags(uint8_t flags)
{
    return (uint8_t)(flags & (uint8_t)~SF_IO_NONBLOCK);
}

static int tcp_flags_valid(uint8_t flags)
{
    return (flags & (uint8_t)~(SF_TCP_NODELAY | SF_IO_NONBLOCK)) == 0u;
}

static int udp_flags_valid(uint8_t flags)
{
    uint8_t allowed = SF_IGMP_VER2 | SF_MULTI_ENABLE | SF_BROAD_BLOCK |
                      SF_UNI_BLOCK | SF_IO_NONBLOCK;

    if ((flags & (uint8_t)~allowed) != 0u) {
        return 0;
    }
    if ((flags & (SF_IGMP_VER2 | SF_UNI_BLOCK)) != 0u &&
        (flags & SF_MULTI_ENABLE) == 0u) {
        return 0;
    }
    return 1;
}

static int macraw_flags_valid(uint8_t flags)
{
    uint8_t allowed = SF_ETHER_OWN | SF_BROAD_BLOCK | SF_MULTI_BLOCK |
                      SF_IPv6_BLOCK | SF_IO_NONBLOCK;

    return (flags & (uint8_t)~allowed) == 0u;
}

static int ipraw_flags_valid(uint8_t flags)
{
    return (flags & (uint8_t)~SF_IO_NONBLOCK) == 0u;
}

static void check_flag_case(uint8_t mode, uint8_t flags, int valid,
                            const char *mode_name)
{
    char message[128];
    int8_t result;
    uint8_t io_mode = UINT8_MAX;

    reset_case();
    result = socket(0u, mode, 49152u, flags);
    (void)snprintf(message, sizeof(message),
                   "%s flags 0x%02X return expected status (got %d)",
                   mode_name, flags, result);
    CHECK(valid ? result >= 0 : result == SOCKERR_SOCKFLAG, message);

    if (valid) {
        (void)snprintf(message, sizeof(message),
                       "%s flags 0x%02X write exact Sn_MR", mode_name,
                       flags);
        CHECK(fake_sockets[0].mode ==
                  (uint8_t)(mode | expected_hardware_flags(flags)),
              message);
        (void)snprintf(message, sizeof(message),
                       "%s flags 0x%02X issue one OPEN", mode_name, flags);
        CHECK(open_commands == 1u, message);
        CHECK(ctlsocket(0u, CS_GET_IOMODE, &io_mode) == SOCK_OK,
              "accepted socket exposes its software I/O mode");
        (void)snprintf(message, sizeof(message),
                       "%s flags 0x%02X preserve nonblocking in software",
                       mode_name, flags);
        CHECK(io_mode == ((flags & SF_IO_NONBLOCK) != 0u ?
                          SOCK_IO_NONBLOCK : SOCK_IO_BLOCK),
              message);
    } else {
        (void)snprintf(message, sizeof(message),
                       "%s flags 0x%02X have no SPI side effects", mode_name,
                       flags);
        CHECK(spi_transactions == 0u, message);
        CHECK(open_commands == 0u,
              "rejected flags do not issue an OPEN command");
    }
}

static void test_exhaustive_mode_flag_matrix(void)
{
    unsigned int flags;

    for (flags = 0u; flags < FLAG_COUNT; ++flags) {
        check_flag_case(Sn_MR_TCP, (uint8_t)flags,
                        tcp_flags_valid((uint8_t)flags), "TCP");
        check_flag_case(Sn_MR_UDP, (uint8_t)flags,
                        udp_flags_valid((uint8_t)flags), "UDP");
        check_flag_case(Sn_MR_MACRAW, (uint8_t)flags,
                        macraw_flags_valid((uint8_t)flags), "MACRAW/0");
        check_flag_case(Sn_MR_IPRAW, (uint8_t)flags,
                        ipraw_flags_valid((uint8_t)flags), "IPRAW");
    }
}

static void test_macraw_requires_socket_zero(void)
{
    unsigned int flags;
    int8_t result;

    for (flags = 0u; flags < FLAG_COUNT; ++flags) {
        reset_case();
        result = socket(1u, Sn_MR_MACRAW, 0u, (uint8_t)flags);
        CHECK(result == SOCKERR_SOCKMODE,
              "MACRAW is rejected on every nonzero socket");
        CHECK(spi_transactions == 0u,
              "nonzero MACRAW rejection has no hardware side effects");
    }
}

static void test_flag_dependencies(void)
{
    reset_case();
    CHECK(socket(0u, Sn_MR_UDP, 50000u, SF_IGMP_VER2) ==
              SOCKERR_SOCKFLAG,
          "IGMPv2 requires multicast enable");
    CHECK(spi_transactions == 0u,
          "missing IGMP dependency is rejected before SPI access");

    reset_case();
    CHECK(socket(0u, Sn_MR_UDP, 50000u, SF_UNI_BLOCK) ==
              SOCKERR_SOCKFLAG,
          "unicast block requires multicast enable");
    CHECK(spi_transactions == 0u,
          "missing unicast-block dependency is rejected before SPI access");

    reset_case();
    CHECK(socket(0u, Sn_MR_UDP, 50000u,
                 SF_MULTI_ENABLE | SF_IGMP_VER2 | SF_UNI_BLOCK) >= 0,
          "multicast dependencies are accepted when enabled");
}

static void test_unknown_bits_have_no_side_effects(void)
{
    static const uint8_t modes[] = {
        Sn_MR_TCP, Sn_MR_UDP, Sn_MR_MACRAW, Sn_MR_IPRAW
    };
    size_t index;

    for (index = 0u; index < sizeof(modes); ++index) {
        reset_case();
        CHECK(socket(0u, modes[index], 50001u, UNKNOWN_LOW_BITS) ==
                  SOCKERR_SOCKFLAG,
              "unknown low flag bits are rejected");
        CHECK(spi_transactions == 0u,
              "unknown flag bits cause no register access");
        CHECK(fake_sockets[0].mode == 0u &&
                  fake_sockets[0].status == SOCK_CLOSED &&
                  fake_sockets[0].port == 0u,
              "unknown flag bits leave hardware state unchanged");
    }
}

static void test_rejection_preserves_previous_socket_state(void)
{
    fake_socket_t before;
    uint8_t io_mode = UINT8_MAX;
    unsigned int transactions_before;

    reset_case();
    CHECK(socket(0u, Sn_MR_UDP, 50002u,
                 SF_MULTI_ENABLE | SF_IO_NONBLOCK) >= 0,
          "baseline UDP socket opens");
    before = fake_sockets[0];
    transactions_before = spi_transactions;

    CHECK(socket(0u, Sn_MR_UDP, 50003u, SF_IGMP_VER2) ==
              SOCKERR_SOCKFLAG,
          "invalid replacement flags are rejected before socket state checks");
    CHECK(memcmp(&before, &fake_sockets[0], sizeof(before)) == 0,
          "flag rejection preserves previous hardware socket state");
    CHECK(spi_transactions == transactions_before,
          "flag rejection performs no additional SPI transaction");
    CHECK(ctlsocket(0u, CS_GET_IOMODE, &io_mode) == SOCK_OK,
          "previous software I/O mode remains readable");
    CHECK(io_mode == SOCK_IO_NONBLOCK,
          "flag rejection preserves previous software socket state");
}

int main(void)
{
    install_fake();
    CHECK(wizchip_init(0, 0) == 0, "register fake initializes W5500");

    test_exhaustive_mode_flag_matrix();
    test_macraw_requires_socket_zero();
    test_flag_dependencies();
    test_unknown_bits_have_no_side_effects();
    test_rejection_preserves_previous_socket_state();

    if (failures != 0u) {
        fprintf(stderr, "\n%u/%u raw-flag checks failed (expected RED)\n",
                failures, checks);
        return 1;
    }
    printf("PASS: %u raw-flag checks\n", checks);
    return 0;
}
