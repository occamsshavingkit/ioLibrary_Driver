#include "diag_protocol.h"
#include "diag_runner.h"
#include "diag_usb.h"
#include "diag_watchdog.h"

#include "bsp/board_api.h"
#include "hardware/watchdog.h"
#include "pico/time.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define DIAG_FIRMWARE_ID "rp2040-w5500-diag"

static bool emit_event(uint32_t sequence, const char *stage, const char *event,
                       const char *details)
{
    char line[DIAG_LINE_MAX + 1u];

    if (diag_format_event(line, sizeof(line), sequence, stage, event, details) < 0) {
        return false;
    }
    return diag_usb_write_line(line);
}

static void emit_help(uint32_t sequence)
{
    (void)emit_event(sequence, "shell", "HELP",
                     "commands=help,status,list,reboot");
}

static void emit_status(uint32_t sequence, const diag_runner_t *runner)
{
    char details[80];
    int written = snprintf(details, sizeof(details),
                           "recovery=%s passed_mask=0x%08" PRIx32,
                           runner->recovery_mode ? "true" : "false",
                           runner->passed_mask);

    if (written >= 0 && (size_t)written < sizeof(details)) {
        (void)emit_event(sequence, "shell", "STATUS", details);
    }
}

static void emit_stage_list(uint32_t sequence)
{
    diag_stage_id_t id;

    for (id = DIAG_STAGE_CALLBACK_LAYOUT; id < DIAG_STAGE_COUNT; ++id) {
        const diag_stage_descriptor_t *stage = diag_stage_descriptor(id);
        char details[128];
        int written;

        if (stage == NULL) {
            return;
        }
        written = snprintf(details, sizeof(details),
                           "id=%u name=%s timeout_ms=%" PRIu32
                           " requires=0x%08" PRIx32 " network=%s",
                           (unsigned int)stage->id, stage->name,
                           stage->timeout_ms, stage->required_mask,
                           stage->requires_network_config ? "true" : "false");
        if (written < 0 || (size_t)written >= sizeof(details) ||
            !emit_event(sequence, "shell", "LIST", details)) {
            return;
        }
    }
}

static void handle_command(diag_runner_t *runner, const char *line)
{
    diag_command_t command;
    uint32_t sequence = ++runner->sequence;

    if (!diag_parse_command(line, &command)) {
        (void)emit_event(sequence, "shell", "ERROR", "reason=invalid-command");
        return;
    }

    switch (command.kind) {
    case DIAG_COMMAND_HELP:
        emit_help(sequence);
        break;
    case DIAG_COMMAND_STATUS:
        emit_status(sequence, runner);
        break;
    case DIAG_COMMAND_LIST:
        emit_stage_list(sequence);
        break;
    case DIAG_COMMAND_REBOOT:
        (void)emit_event(sequence, "shell", "REBOOT", "reason=requested");
        {
            absolute_time_t deadline = make_timeout_time_ms(10u);

            while (!time_reached(deadline)) {
                diag_usb_task();
            }
        }
        watchdog_reboot(0u, 0u, 0u);
        break;
    default:
        (void)emit_event(sequence, "shell", "ERROR", "reason=unsupported-command");
        break;
    }
}

int main(void)
{
    diag_runner_t runner;
    diag_journal_t recovered_journal;
    bool recovered;
    bool boot_announced = false;
    bool timeout_announced = false;
    char command_line[DIAG_LINE_MAX + 1u];

    board_init();
    diag_usb_init();

    diag_runner_init(&runner);
    recovered = diag_watchdog_recover(&recovered_journal);
    if (recovered) {
        runner.sequence = recovered_journal.sequence;
        runner.recovery_mode = true;
    }

    for (;;) {
        diag_usb_task();
        if (diag_usb_connected() && !boot_announced) {
            char boot_details[80];
            int written = snprintf(boot_details, sizeof(boot_details),
                                   "firmware=%s recovery=%s", DIAG_FIRMWARE_ID,
                                   runner.recovery_mode ? "true" : "false");

            if (written >= 0 && (size_t)written < sizeof(boot_details) &&
                emit_event(0u, "system", "BOOT", boot_details)) {
                boot_announced = true;
            }
        }
        if (diag_usb_connected() && boot_announced && recovered &&
            !timeout_announced) {
            const diag_stage_descriptor_t *stage =
                diag_stage_descriptor(recovered_journal.stage);
            char timeout_details[80];
            int written = snprintf(timeout_details, sizeof(timeout_details),
                                   "phase=%u recovery=watchdog",
                                   (unsigned int)recovered_journal.phase);

            if (stage != NULL && written >= 0 &&
                (size_t)written < sizeof(timeout_details) &&
                emit_event(recovered_journal.sequence, stage->name, "TIMEOUT",
                           timeout_details)) {
                timeout_announced = true;
            }
        }
        if (!diag_usb_connected()) {
            boot_announced = false;
            timeout_announced = false;
        } else if (boot_announced && (!recovered || timeout_announced) &&
                   diag_usb_poll_line(command_line, sizeof(command_line))) {
            handle_command(&runner, command_line);
        }
    }
}
