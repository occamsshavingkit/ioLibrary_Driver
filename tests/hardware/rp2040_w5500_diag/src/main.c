#include "diag_protocol.h"
#include "diag_runner.h"
#include "diag_usb.h"
#include "diag_watchdog.h"
#include "diag_w5500_stages.h"
#include "diag_net.h"

#include "bsp/board_api.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/time.h"
#include "pico/unique_id.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DIAG_FIRMWARE_ID "rp2040-w5500-diag"

typedef diag_stage_result_t (*diag_stage_handler_t)(diag_stage_context_t *context);

static const diag_stage_handler_t stage_handlers[DIAG_STAGE_COUNT] = {
    [DIAG_STAGE_CALLBACK_LAYOUT] = diag_stage_callback_layout,
    [DIAG_STAGE_TRANSPORT_INIT] = diag_stage_transport_init,
    [DIAG_STAGE_CHIP_RESET] = diag_stage_chip_reset,
    [DIAG_STAGE_VERSION] = diag_stage_version,
    [DIAG_STAGE_MEMORY_INIT] = diag_stage_memory_init,
    [DIAG_STAGE_PHY_LINK] = diag_stage_phy_link,
    [DIAG_STAGE_SINGLE_REGISTER] = diag_stage_single_register,
    [DIAG_STAGE_BURST_REGISTER] = diag_stage_burst_register,
    [DIAG_STAGE_POINTER_SEQUENTIAL] = diag_stage_pointer_sequential,
    [DIAG_STAGE_POINTER_BURST] = diag_stage_pointer_burst,
    [DIAG_STAGE_POINTER_API] = diag_stage_pointer_api,
    [DIAG_STAGE_SOCKET_OPEN] = diag_stage_socket_open,
    [DIAG_STAGE_UDP] = diag_stage_udp,
    [DIAG_STAGE_DHCP] = diag_stage_dhcp,
};

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
                     "commands=help,status,list,run,repeat,net,reboot");
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
    char completion[48];
    int written;

    for (id = DIAG_STAGE_CALLBACK_LAYOUT; id < DIAG_STAGE_COUNT; ++id) {
        const diag_stage_descriptor_t *stage = diag_stage_descriptor(id);
        char details[128];

        if (stage == NULL) {
            return;
        }
        written = snprintf(details, sizeof(details),
                           "id=%u name=%s timeout_ms=%" PRIu32
                            " requires=0x%08" PRIx32 " network=%s available=%s",
                            (unsigned int)stage->id, stage->name,
                            stage->timeout_ms, stage->required_mask,
                            stage->requires_network_config ? "true" : "false",
                            stage_handlers[id] != NULL ? "true" : "false");
        if (written < 0 || (size_t)written >= sizeof(details) ||
            !emit_event(sequence, "shell", "LIST", details)) {
            return;
        }
    }

    written = snprintf(completion, sizeof(completion),
                       "complete=true count=%u", (unsigned int)DIAG_STAGE_COUNT);
    if (written < 0 || (size_t)written >= sizeof(completion)) {
        return;
    }
    (void)emit_event(sequence, "shell", "LIST", completion);
}

static diag_stage_result_t run_stage(diag_stage_context_t *context,
                                     diag_stage_id_t id)
{
    const diag_stage_descriptor_t *descriptor = diag_stage_descriptor(id);
    diag_stage_handler_t handler;
    diag_stage_result_t result;
    char start_details[32];
    int written;

    context->details[0] = '\0';
    if (descriptor == NULL) {
        (void)emit_event(++context->runner->sequence, "shell", "ERROR",
                         "reason=invalid-stage");
        return DIAG_STAGE_NOT_RUN;
    }
    handler = stage_handlers[id];
    if (handler == NULL) {
        char details[64];

        written = snprintf(details, sizeof(details),
                           "reason=stage-unavailable name=%s", descriptor->name);
        if (written >= 0 && (size_t)written < sizeof(details)) {
            (void)emit_event(++context->runner->sequence, "shell", "ERROR",
                             details);
        }
        return DIAG_STAGE_NOT_RUN;
    }
    if (!diag_runner_can_run(context->runner, id,
                             context->network_configured)) {
        char details[72];

        written = snprintf(details, sizeof(details),
                           "reason=prerequisite-blocked name=%s",
                           descriptor->name);
        if (written >= 0 && (size_t)written < sizeof(details)) {
            (void)emit_event(++context->runner->sequence, "shell", "ERROR",
                             details);
        }
        return DIAG_STAGE_NOT_RUN;
    }

    context->sequence = diag_runner_begin(context->runner, id);
    written = snprintf(start_details, sizeof(start_details),
                       "timeout_ms=%" PRIu32, descriptor->timeout_ms);
    if (written < 0 || (size_t)written >= sizeof(start_details) ||
        !emit_event(context->sequence, descriptor->name, "START",
                    start_details)) {
        diag_runner_finish(context->runner, id, DIAG_STAGE_FAIL);
        return DIAG_STAGE_FAIL;
    }

    result = handler(context);
    diag_runner_finish(context->runner, id, result);
    (void)emit_event(context->sequence, descriptor->name,
                     result == DIAG_STAGE_PASS ? "PASS" : "FAIL",
                     context->details);
    return result;
}

static diag_stage_result_t run_all(diag_stage_context_t *context, diag_stage_id_t limit_id)
{
    diag_stage_id_t id;
    diag_stage_result_t overall = DIAG_STAGE_PASS;

    diag_runner_prepare_repeat(context->runner, DIAG_STAGE_CALLBACK_LAYOUT);
    for (id = DIAG_STAGE_CALLBACK_LAYOUT; id <= limit_id; ++id) {
        if (!diag_runner_can_run(context->runner, id,
                                 context->network_configured)) {
            continue;
        }
        diag_stage_result_t result = run_stage(context, id);

        if (result == DIAG_STAGE_FAIL || result == DIAG_STAGE_TIMEOUT) {
            overall = DIAG_STAGE_FAIL;
        }
    }
    return overall;
}

static void handle_command(diag_stage_context_t *context, const char *line)
{
    diag_command_t command;
    const diag_stage_descriptor_t *descriptor;
    uint32_t sequence;
    uint32_t iteration;

    if (!diag_parse_command(line, &command)) {
        sequence = ++context->runner->sequence;
        (void)emit_event(sequence, "shell", "ERROR", "reason=invalid-command");
        return;
    }

    switch (command.kind) {
    case DIAG_COMMAND_HELP:
        sequence = ++context->runner->sequence;
        emit_help(sequence);
        break;
    case DIAG_COMMAND_STATUS:
        sequence = ++context->runner->sequence;
        emit_status(sequence, context->runner);
        break;
    case DIAG_COMMAND_LIST:
        sequence = ++context->runner->sequence;
        emit_stage_list(sequence);
        break;
    case DIAG_COMMAND_RUN:
        descriptor = diag_stage_by_name(command.stage);
        if (descriptor == NULL) {
            (void)emit_event(++context->runner->sequence, "shell", "ERROR",
                             "reason=invalid-stage");
        } else {
            diag_runner_prepare_repeat(context->runner, descriptor->id);
            diag_stage_id_t id;
            diag_stage_id_t start_id =
                diag_runner_execution_start(descriptor->id);

            for (id = start_id; id <= descriptor->id; ++id) {
                if (run_stage(context, id) != DIAG_STAGE_PASS) {
                    break;
                }
            }
        }
        break;
    case DIAG_COMMAND_RUN_ALL:
        (void)run_all(context, DIAG_STAGE_DHCP);
        break;
    case DIAG_COMMAND_REPEAT:
        descriptor = diag_stage_by_name(command.stage);
        if (descriptor == NULL) {
            (void)emit_event(++context->runner->sequence, "shell", "ERROR",
                             "reason=invalid-stage");
            break;
        }
        for (iteration = 0u; iteration < command.count; ++iteration) {
            diag_runner_prepare_repeat(context->runner, descriptor->id);
            diag_stage_id_t id;
            diag_stage_id_t start_id =
                diag_runner_execution_start(descriptor->id);
            bool failed = false;

            for (id = start_id; id <= descriptor->id; ++id) {
                if (run_stage(context, id) != DIAG_STAGE_PASS) {
                    failed = true;
                    break;
                }
            }
            if (failed) {
                break;
            }
        }
        break;
    case DIAG_COMMAND_NET: {
        pico_unique_board_id_t board_id;
        wiz_NetInfo temporary = {0};
        char details[128];
        int written;

        sequence = ++context->runner->sequence;

        if (!diag_net_validate_static(command.device_ip, command.subnet,
                                      command.gateway, command.host_ip,
                                      command.host_port)) {
            (void)emit_event(sequence, "shell", "ERROR", "reason=invalid-network");
            break;
        }

        pico_get_unique_board_id(&board_id);
        memcpy(temporary.ip, command.device_ip, sizeof(temporary.ip));
        memcpy(temporary.sn, command.subnet, sizeof(temporary.sn));
        memcpy(temporary.gw, command.gateway, sizeof(temporary.gw));
        diag_net_derive_mac(board_id.id, temporary.mac);
        temporary.dhcp = NETINFO_STATIC;

        memcpy(context->host_ip, command.host_ip, sizeof(context->host_ip));
        context->host_port = command.host_port;
        context->network = temporary;
        context->runner->passed_mask &= ~((1u << DIAG_STAGE_SOCKET_OPEN) |
                                          (1u << DIAG_STAGE_UDP) |
                                          (1u << DIAG_STAGE_DHCP));
        context->network_configured = true;

        written = snprintf(details, sizeof(details),
                           "device=%u.%u.%u.%u host=%u.%u.%u.%u port=%u",
                           command.device_ip[0], command.device_ip[1],
                           command.device_ip[2], command.device_ip[3],
                           command.host_ip[0], command.host_ip[1],
                           command.host_ip[2], command.host_ip[3],
                           command.host_port);
        if (written >= 0 && (size_t)written < sizeof(details)) {
            (void)emit_event(sequence, "shell", "NET", details);
        }
        break;
    }
    case DIAG_COMMAND_REBOOT:
        sequence = ++context->runner->sequence;
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
        sequence = ++context->runner->sequence;
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
    bool automatic_smoke_done = false;
    char command_line[DIAG_LINE_MAX + 1u];
    static diag_stage_context_t context;

    set_sys_clock_khz(133000u, true);
    board_init();
    diag_usb_init();

    diag_runner_init(&runner);
    context.runner = &runner;
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
                                    "reset=watchdog phase=%s",
                                    diag_stage_phase_name(
                                        recovered_journal.stage,
                                        recovered_journal.phase));

            if (stage != NULL && written >= 0 &&
                (size_t)written < sizeof(timeout_details) &&
                emit_event(recovered_journal.sequence, stage->name, "TIMEOUT",
                           timeout_details)) {
                timeout_announced = true;
            }
        }
        if (diag_usb_connected() && boot_announced && !recovered &&
            !runner.recovery_mode && !automatic_smoke_done) {
            automatic_smoke_done = true;
            (void)run_all(&context, DIAG_STAGE_POINTER_API);
        }
        if (!diag_usb_connected()) {
            boot_announced = false;
            timeout_announced = false;
        } else if (boot_announced && (!recovered || timeout_announced) &&
                    diag_usb_poll_line(command_line, sizeof(command_line))) {
            handle_command(&context, command_line);
        }
    }
}
