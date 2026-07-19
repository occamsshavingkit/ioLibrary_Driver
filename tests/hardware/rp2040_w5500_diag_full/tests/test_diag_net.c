#include "diag_net.h"
#include "diag_runner.h"

#include <assert.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    /* 1. MAC derivation test */
    {
        const uint8_t board_id[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        uint8_t mac[6] = {0};
        diag_net_derive_mac(board_id, mac);
        assert(mac[0] == 0x02u);
        assert(mac[1] == board_id[3]);
        assert(mac[2] == board_id[4]);
        assert(mac[3] == board_id[5]);
        assert(mac[4] == board_id[6]);
        assert(mac[5] == board_id[7]);
    }

    /* 2. Static IPv4 Configuration Validation tests */
    {
        /* Valid configuration */
        uint8_t ip[4] = {192, 168, 1, 10};
        uint8_t subnet[4] = {255, 255, 255, 0};
        uint8_t gateway[4] = {192, 168, 1, 1};
        uint8_t host[4] = {192, 168, 1, 20};
        uint16_t port = 12345;

        assert(diag_net_validate_static(ip, subnet, gateway, host, port));

        /* Reject zero IP */
        uint8_t zero_ip[4] = {0, 0, 0, 0};
        assert(!diag_net_validate_static(zero_ip, subnet, gateway, host, port));

        /* Reject loopback IP */
        uint8_t loopback_ip[4] = {127, 0, 0, 1};
        assert(!diag_net_validate_static(loopback_ip, subnet, gateway, host, port));

        /* Reject multicast IP */
        uint8_t multicast_ip[4] = {224, 0, 0, 1};
        assert(!diag_net_validate_static(multicast_ip, subnet, gateway, host, port));

        /* Reject broadcast IP */
        uint8_t broadcast_ip[4] = {255, 255, 255, 255};
        assert(!diag_net_validate_static(broadcast_ip, subnet, gateway, host, port));

        /* Reject network host address */
        uint8_t net_ip[4] = {192, 168, 1, 0};
        assert(!diag_net_validate_static(net_ip, subnet, gateway, host, port));

        /* Reject broadcast host address */
        uint8_t broad_ip[4] = {192, 168, 1, 255};
        assert(!diag_net_validate_static(broad_ip, subnet, gateway, host, port));

        /* Reject non-contiguous subnet */
        uint8_t bad_subnet[4] = {255, 255, 254, 128};
        assert(!diag_net_validate_static(ip, bad_subnet, gateway, host, port));

        /* Reject zero subnet */
        uint8_t zero_subnet[4] = {0, 0, 0, 0};
        assert(!diag_net_validate_static(ip, zero_subnet, gateway, host, port));

        /* Reject different subnet for host */
        uint8_t diff_host[4] = {192, 168, 2, 20};
        assert(!diag_net_validate_static(ip, subnet, gateway, diff_host, port));

        /* Reject different subnet for gateway */
        uint8_t diff_gateway[4] = {192, 168, 2, 1};
        assert(!diag_net_validate_static(ip, subnet, diff_gateway, host, port));

        /* Reject zero gateway */
        assert(!diag_net_validate_static(ip, subnet, zero_ip, host, port));

        /* Reject identical device/host IP */
        assert(!diag_net_validate_static(ip, subnet, gateway, ip, port));

        /* Reject zero port */
        assert(!diag_net_validate_static(ip, subnet, gateway, host, 0));
    }

    /* 3. Canonical 32-byte UDP wire encoding and validation tests */
    {
        uint32_t seq = 0x12345678u;
        uint8_t buffer[32] = {0};
        diag_net_encode_udp(seq, buffer);

        /* Fixed vector check */
        assert(buffer[0] == 0x44);
        assert(buffer[1] == 0x55);
        assert(buffer[2] == 0x50);
        assert(buffer[3] == 0x31);
        assert(buffer[4] == 0x12);
        assert(buffer[5] == 0x34);
        assert(buffer[6] == 0x56);
        assert(buffer[7] == 0x78);

        for (int i = 0; i < 24; ++i) {
            assert(buffer[8 + i] == ((seq + i) & 0xff));
        }

        uint8_t ip[4] = {192, 168, 1, 10};
        uint16_t port = 12345;

        /* Validation success */
        assert(diag_net_validate_udp_packet(buffer, 32, seq, ip, port, ip, port));

        /* Reject incorrect sequence */
        assert(!diag_net_validate_udp_packet(buffer, 32, 0x11111111u, ip, port, ip, port));

        /* Reject incorrect source IP */
        uint8_t bad_ip[4] = {192, 168, 1, 11};
        assert(!diag_net_validate_udp_packet(buffer, 32, seq, ip, port, bad_ip, port));

        /* Reject incorrect source port */
        assert(!diag_net_validate_udp_packet(buffer, 32, seq, ip, port, ip, 9999));

        /* Reject incorrect length */
        assert(!diag_net_validate_udp_packet(buffer, 31, seq, ip, port, ip, port));

        /* Reject incorrect payload data */
        buffer[31] ^= 1u;
        assert(!diag_net_validate_udp_packet(buffer, 32, seq, ip, port, ip, port));
    }

    /* 4. 16-bit pointer delta including wraparound */
    {
        assert(diag_net_ptr_delta(65530, 10) == 16);
        assert(diag_net_ptr_delta(100, 100) == 0);
        assert(diag_net_ptr_delta(100, 200) == 100);
        assert(diag_net_ptr_delta(50000, 49999) == 65535);
    }

    /* 5. DHCP state names */
    {
        assert(strcmp(diag_net_dhcp_state_name(0), "failed") == 0);
        assert(strcmp(diag_net_dhcp_state_name(1), "running") == 0);
        assert(strcmp(diag_net_dhcp_state_name(2), "assigned") == 0);
        assert(strcmp(diag_net_dhcp_state_name(3), "changed") == 0);
        assert(strcmp(diag_net_dhcp_state_name(4), "leased") == 0);
        assert(strcmp(diag_net_dhcp_state_name(5), "stopped") == 0);
        assert(strcmp(diag_net_dhcp_state_name(6), "unknown") == 0);
    }

    /* 6. DHCP lease completeness policy */
    {
        uint8_t ip[4] = {192, 168, 1, 10};
        uint8_t gateway[4] = {192, 168, 1, 1};
        uint8_t subnet[4] = {255, 255, 255, 0};
        uint8_t dns[4] = {8, 8, 8, 8};

        assert(diag_net_dhcp_lease_complete(ip, gateway, subnet, dns));

        /* Zero check */
        uint8_t zero_ip[4] = {0};
        assert(!diag_net_dhcp_lease_complete(zero_ip, gateway, subnet, dns));

        /* Non-unicast check */
        uint8_t multicast_dns[4] = {224, 0, 0, 1};
        assert(!diag_net_dhcp_lease_complete(ip, gateway, subnet, multicast_dns));

        uint8_t zero_dns[4] = {0};
        assert(!diag_net_dhcp_lease_complete(ip, gateway, subnet, zero_dns));

        /* Invalid subnet contiguous */
        uint8_t bad_subnet[4] = {255, 255, 254, 128};
        assert(!diag_net_dhcp_lease_complete(ip, gateway, bad_subnet, dns));

        /* IP and Gateway on same subnet */
        uint8_t diff_gateway[4] = {192, 168, 2, 1};
        assert(!diag_net_dhcp_lease_complete(ip, diff_gateway, subnet, dns));
    }

    /* 7. Phase name mapping tests for socket-open, UDP, DHCP, and snapshots */
    {
        /* socket-open phase names */
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_SET_NETINFO), "set-netinfo") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_SOCKET_OPEN), "socket-open") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_SET_IOMODE), "set-iomode") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_SOCKET_STATUS), "socket-status") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_TX_FSR), "tx-fsr") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_RX_RSR), "rx-rsr") == 0);

        /* UDP phase names */
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_TX_WR_BEFORE), "tx-wr-before") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_SEND_IR_CLEAR), "send-ir-clear") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_SENDTO), "sendto") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_SEND_IR_READ), "send-ir-read") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_TX_WR_AFTER), "tx-wr-after") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_RX_RD_BEFORE), "rx-rd-before") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_RX_RSR_POLL), "rx-rsr-poll") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_RECVFROM), "recvfrom") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_RX_RD_AFTER), "rx-rd-after") == 0);

        /* DHCP phase names */
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_DHCP, DIAG_PHASE_CHIP_RESET), "chip-reset") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_DHCP, DIAG_PHASE_MEMORY_INIT), "memory-init") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_DHCP, DIAG_PHASE_PHY_LINK), "phy-link") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_DHCP, DIAG_PHASE_SET_NETINFO), "set-netinfo") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_DHCP, DIAG_PHASE_DHCP_INIT), "dhcp-init") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_DHCP, DIAG_PHASE_DHCP_RUN), "dhcp-run") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_DHCP, DIAG_PHASE_LEASE_APPLY), "lease-apply") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_DHCP, DIAG_PHASE_DHCP_STOP), "dhcp-stop") == 0);

        /* snapshot phase names */
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_SNAPSHOT_SR), "snapshot-sr") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_SNAPSHOT_IR), "snapshot-ir") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_DHCP, DIAG_PHASE_SNAPSHOT_TX_WR), "snapshot-tx-wr") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_UDP, DIAG_PHASE_SNAPSHOT_RX_RD), "snapshot-rx-rd") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_SNAPSHOT_TX_FSR), "snapshot-tx-fsr") == 0);
        assert(strcmp(diag_stage_phase_name(DIAG_STAGE_DHCP, DIAG_PHASE_SNAPSHOT_RX_RSR), "snapshot-rx-rsr") == 0);
    }

    return 0;
}
