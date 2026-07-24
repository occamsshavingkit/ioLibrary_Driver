#include "diag_net.h"

#include <string.h>

void diag_net_derive_mac(const uint8_t board_id[8], uint8_t mac[6])
{
    if (mac == NULL) {
        return;
    }
    mac[0] = 0x02u;
    if (board_id != NULL) {
        mac[1] = board_id[3];
        mac[2] = board_id[4];
        mac[3] = board_id[5];
        mac[4] = board_id[6];
        mac[5] = board_id[7];
    } else {
        mac[1] = 0;
        mac[2] = 0;
        mac[3] = 0;
        mac[4] = 0;
        mac[5] = 0;
    }
}

static uint32_t ip_to_u32(const uint8_t ip[4])
{
    return ((uint32_t)ip[0] << 24) |
           ((uint32_t)ip[1] << 16) |
           ((uint32_t)ip[2] << 8)  |
           ((uint32_t)ip[3]);
}

static bool is_valid_subnet(uint32_t mask)
{
    uint32_t inverse;

    if (mask == 0u || mask == UINT32_MAX) {
        return false;
    }
    inverse = ~mask;
    return (inverse & (inverse + 1u)) == 0u;
}

static bool validate_address(uint32_t ip_val, uint32_t subnet_val)
{
    uint8_t first_byte = (uint8_t)(ip_val >> 24);

    if (first_byte == 0u || first_byte == 127u || first_byte >= 224u) {
        return false;
    }
    if ((ip_val & ~subnet_val) == 0u) {
        return false;
    }
    if ((ip_val & ~subnet_val) == ~subnet_val) {
        return false;
    }
    return true;
}

bool diag_net_validate_static(const uint8_t ip[4], const uint8_t subnet[4],
                              const uint8_t gateway[4], const uint8_t host[4],
                              uint16_t port)
{
    if (ip == NULL || subnet == NULL || gateway == NULL || host == NULL) {
        return false;
    }
    if (port == 0u) {
        return false;
    }

    uint32_t ip_val = ip_to_u32(ip);
    uint32_t subnet_val = ip_to_u32(subnet);
    uint32_t gateway_val = ip_to_u32(gateway);
    uint32_t host_val = ip_to_u32(host);

    if (!is_valid_subnet(subnet_val)) {
        return false;
    }
    if (!validate_address(ip_val, subnet_val)) {
        return false;
    }
    if (!validate_address(gateway_val, subnet_val)) {
        return false;
    }
    if (!validate_address(host_val, subnet_val)) {
        return false;
    }
    if ((ip_val & subnet_val) != (host_val & subnet_val)) {
        return false;
    }
    if ((ip_val & subnet_val) != (gateway_val & subnet_val)) {
        return false;
    }
    if (ip_val == host_val) {
        return false;
    }
    return true;
}

bool diag_net_validate_udp_packet(const uint8_t *packet_bytes, size_t length,
                                  uint32_t expected_sequence,
                                  const uint8_t expected_ip[4], uint16_t expected_port,
                                  const uint8_t actual_ip[4], uint16_t actual_port)
{
    if (packet_bytes == NULL || expected_ip == NULL || actual_ip == NULL) {
        return false;
    }
    if (length != 32u) {
        return false;
    }
    if (actual_port != expected_port) {
        return false;
    }
    if (memcmp(actual_ip, expected_ip, 4) != 0) {
        return false;
    }
    if (packet_bytes[0] != 0x44u || packet_bytes[1] != 0x55u ||
        packet_bytes[2] != 0x50u || packet_bytes[3] != 0x31u) {
        return false;
    }

    uint32_t seq = ((uint32_t)packet_bytes[4] << 24) |
                   ((uint32_t)packet_bytes[5] << 16) |
                   ((uint32_t)packet_bytes[6] << 8)  |
                   ((uint32_t)packet_bytes[7]);

    if (seq != expected_sequence) {
        return false;
    }

    for (size_t i = 0u; i < 24u; ++i) {
        if (packet_bytes[8u + i] != (uint8_t)((expected_sequence + i) & 0xFFu)) {
            return false;
        }
    }
    return true;
}

void diag_net_encode_udp(uint32_t sequence, uint8_t buffer[32])
{
    if (buffer == NULL) {
        return;
    }
    buffer[0] = 0x44u;
    buffer[1] = 0x55u;
    buffer[2] = 0x50u;
    buffer[3] = 0x31u;

    buffer[4] = (uint8_t)(sequence >> 24);
    buffer[5] = (uint8_t)(sequence >> 16);
    buffer[6] = (uint8_t)(sequence >> 8);
    buffer[7] = (uint8_t)(sequence);

    for (size_t i = 0u; i < 24u; ++i) {
        buffer[8u + i] = (uint8_t)((sequence + i) & 0xFFu);
    }
}

uint16_t diag_net_ptr_delta(uint16_t before, uint16_t after)
{
    return (uint16_t)(after - before);
}

const char *diag_net_dhcp_state_name(uint8_t state)
{
    switch (state) {
    case 0:
        return "failed";
    case 1:
        return "running";
    case 2:
        return "assigned";
    case 3:
        return "changed";
    case 4:
        return "leased";
    case 5:
        return "stopped";
    default:
        return "unknown";
    }
}

bool diag_net_dhcp_lease_complete(const uint8_t ip[4], const uint8_t gateway[4],
                                  const uint8_t subnet[4], const uint8_t dns[4])
{
    if (ip == NULL || gateway == NULL || subnet == NULL || dns == NULL) {
        return false;
    }

    uint32_t ip_val = ip_to_u32(ip);
    uint32_t subnet_val = ip_to_u32(subnet);
    uint32_t gateway_val = ip_to_u32(gateway);

    if (!is_valid_subnet(subnet_val)) {
        return false;
    }
    if (!validate_address(ip_val, subnet_val)) {
        return false;
    }
    if (!validate_address(gateway_val, subnet_val)) {
        return false;
    }

    uint32_t dns_val = ip_to_u32(dns);
    uint8_t dns_first = dns[0];

    if (dns_val == 0u || dns_val == UINT32_MAX || dns_first == 127u ||
        dns_first >= 224u) {
        return false;
    }

    if ((ip_val & subnet_val) != (gateway_val & subnet_val)) {
        return false;
    }
    return true;
}
