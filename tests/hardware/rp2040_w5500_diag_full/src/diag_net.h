#ifndef W5500_DIAG_NET_H
#define W5500_DIAG_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint8_t payload[24];
} diag_udp_packet_t;

_Static_assert(sizeof(diag_udp_packet_t) == 32u,
               "UDP packet object must be 32 bytes");
_Static_assert(offsetof(diag_udp_packet_t, magic) == 0u,
               "UDP magic offset changed");
_Static_assert(offsetof(diag_udp_packet_t, sequence) == 4u,
               "UDP sequence offset changed");
_Static_assert(offsetof(diag_udp_packet_t, payload) == 8u,
               "UDP payload offset changed");

void diag_net_derive_mac(const uint8_t board_id[8], uint8_t mac[6]);

bool diag_net_validate_static(const uint8_t ip[4], const uint8_t subnet[4],
                              const uint8_t gateway[4], const uint8_t host[4],
                              uint16_t port);

bool diag_net_validate_udp_packet(const uint8_t *packet_bytes, size_t length,
                                  uint32_t expected_sequence,
                                  const uint8_t expected_ip[4],
                                  uint16_t expected_port,
                                  const uint8_t actual_ip[4],
                                  uint16_t actual_port);

void diag_net_encode_udp(uint32_t sequence, uint8_t buffer[32]);

uint16_t diag_net_ptr_delta(uint16_t before, uint16_t after);

const char *diag_net_dhcp_state_name(uint8_t state);

bool diag_net_dhcp_lease_complete(const uint8_t ip[4], const uint8_t gateway[4],
                                  const uint8_t subnet[4], const uint8_t dns[4]);

#endif
