#include "diag_protocol.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIAG_MAX_COMMAND_TOKENS 8u

static size_t tokenize(const char *line,
                       char tokens[DIAG_MAX_COMMAND_TOKENS][DIAG_STAGE_NAME_MAX])
{
    const char *cursor = line;
    size_t count = 0u;

    while (*cursor != '\0') {
        int consumed = 0;

        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        if (count == DIAG_MAX_COMMAND_TOKENS ||
            sscanf(cursor, "%23s%n", tokens[count], &consumed) != 1 ||
            consumed <= 0) {
            return DIAG_MAX_COMMAND_TOKENS + 1u;
        }
        cursor += consumed;
        ++count;
    }

    return count;
}

static bool parse_u32(const char *text, uint32_t minimum, uint32_t maximum,
                      uint32_t *value)
{
    const char *cursor = text;
    char *end = NULL;
    unsigned long parsed;

    if (*cursor == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (!isdigit((unsigned char)*cursor)) {
            return false;
        }
        ++cursor;
    }

    parsed = strtoul(text, &end, 10);
    if (*end != '\0' || parsed < minimum || parsed > maximum) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool parse_ipv4(const char *text, uint8_t address[4])
{
    const char *cursor = text;
    size_t index;

    for (index = 0u; index < 4u; ++index) {
        uint32_t octet = 0u;
        size_t digits = 0u;

        while (isdigit((unsigned char)*cursor)) {
            octet = (octet * 10u) + (uint32_t)(*cursor - '0');
            if (octet > 255u) {
                return false;
            }
            ++cursor;
            ++digits;
        }
        if (digits == 0u || (index < 3u && *cursor != '.') ||
            (index == 3u && *cursor != '\0')) {
            return false;
        }
        address[index] = (uint8_t)octet;
        if (index < 3u) {
            ++cursor;
        }
    }

    return true;
}

static bool has_whitespace(const char *text)
{
    while (*text != '\0') {
        if (isspace((unsigned char)*text)) {
            return true;
        }
        ++text;
    }
    return false;
}

bool diag_parse_command(const char *line, diag_command_t *command)
{
    char tokens[DIAG_MAX_COMMAND_TOKENS][DIAG_STAGE_NAME_MAX] = {{0}};
    diag_command_t parsed = {0};
    size_t token_count;

    if (line == NULL || command == NULL) {
        return false;
    }
    *command = parsed;
    token_count = tokenize(line, tokens);

    if (token_count == 1u) {
        if (strcmp(tokens[0], "help") == 0) {
            parsed.kind = DIAG_COMMAND_HELP;
        } else if (strcmp(tokens[0], "status") == 0) {
            parsed.kind = DIAG_COMMAND_STATUS;
        } else if (strcmp(tokens[0], "list") == 0) {
            parsed.kind = DIAG_COMMAND_LIST;
        } else if (strcmp(tokens[0], "reboot") == 0) {
            parsed.kind = DIAG_COMMAND_REBOOT;
        }
    } else if (token_count == 2u && strcmp(tokens[0], "run") == 0) {
        if (strcmp(tokens[1], "all") == 0) {
            parsed.kind = DIAG_COMMAND_RUN_ALL;
        } else {
            parsed.kind = DIAG_COMMAND_RUN;
            memcpy(parsed.stage, tokens[1], sizeof(parsed.stage));
        }
    } else if (token_count == 3u && strcmp(tokens[0], "repeat") == 0 &&
               parse_u32(tokens[2], 1u, 100000u, &parsed.count)) {
        parsed.kind = DIAG_COMMAND_REPEAT;
        memcpy(parsed.stage, tokens[1], sizeof(parsed.stage));
    } else if (token_count == 6u && strcmp(tokens[0], "net") == 0) {
        uint32_t port;

        if (parse_ipv4(tokens[1], parsed.device_ip) &&
            parse_ipv4(tokens[2], parsed.subnet) &&
            parse_ipv4(tokens[3], parsed.gateway) &&
            parse_ipv4(tokens[4], parsed.host_ip) &&
            parse_u32(tokens[5], 1u, 65535u, &port)) {
            parsed.kind = DIAG_COMMAND_NET;
            parsed.host_port = (uint16_t)port;
        }
    }

    if (parsed.kind == DIAG_COMMAND_INVALID) {
        return false;
    }
    *command = parsed;
    return true;
}

int diag_format_event(char *buffer, size_t capacity, uint32_t sequence,
                      const char *stage, const char *event, const char *details)
{
    int written;

    if (buffer == NULL || capacity == 0u || stage == NULL || event == NULL ||
        details == NULL || stage[0] == '\0' || event[0] == '\0' ||
        has_whitespace(stage) || has_whitespace(event) ||
        strchr(details, '\n') != NULL || strchr(details, '\r') != NULL) {
        return -1;
    }

    written = snprintf(buffer, capacity,
                       "DIAG protocol=%u seq=%" PRIu32 " stage=%s event=%s%s%s\n",
                       DIAG_PROTOCOL_VERSION, sequence, stage, event,
                       details[0] == '\0' ? "" : " ", details);
    if (written < 0 || (size_t)written >= capacity) {
        return -1;
    }
    return written;
}
