#ifndef W5500_DIAG_PROTOCOL_H
#define W5500_DIAG_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIAG_PROTOCOL_VERSION 1u
#define DIAG_LINE_MAX 256u
#define DIAG_STAGE_NAME_MAX 24u

typedef enum {
    DIAG_COMMAND_INVALID = 0,
    DIAG_COMMAND_HELP,
    DIAG_COMMAND_STATUS,
    DIAG_COMMAND_LIST,
    DIAG_COMMAND_RUN,
    DIAG_COMMAND_RUN_ALL,
    DIAG_COMMAND_REPEAT,
    DIAG_COMMAND_NET,
    DIAG_COMMAND_REBOOT
} diag_command_kind_t;

typedef struct {
    diag_command_kind_t kind;
    char stage[DIAG_STAGE_NAME_MAX];
    uint32_t count;
    uint8_t device_ip[4];
    uint8_t subnet[4];
    uint8_t gateway[4];
    uint8_t host_ip[4];
    uint16_t host_port;
} diag_command_t;

bool diag_parse_command(const char *line, diag_command_t *command);
int diag_format_event(char *buffer, size_t capacity, uint32_t sequence,
                      const char *stage, const char *event, const char *details);

#endif
