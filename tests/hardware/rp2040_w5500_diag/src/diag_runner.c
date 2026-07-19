#include "diag_runner.h"

#include <stddef.h>
#include <string.h>

#define STAGE_BIT(id) (UINT32_C(1) << (id))

static const diag_stage_descriptor_t stages[DIAG_STAGE_COUNT] = {
    {DIAG_STAGE_CALLBACK_LAYOUT, "callback-layout", 0u, 0u, false},
    {DIAG_STAGE_TRANSPORT_INIT, "transport-init", STAGE_BIT(DIAG_STAGE_CALLBACK_LAYOUT), 2000u, false},
    {DIAG_STAGE_CHIP_RESET, "chip-reset", STAGE_BIT(DIAG_STAGE_TRANSPORT_INIT), 2000u, false},
    {DIAG_STAGE_VERSION, "version", STAGE_BIT(DIAG_STAGE_CHIP_RESET), 2000u, false},
    {DIAG_STAGE_MEMORY_INIT, "memory-init", STAGE_BIT(DIAG_STAGE_VERSION), 2000u, false},
    {DIAG_STAGE_PHY_LINK, "phy-link", STAGE_BIT(DIAG_STAGE_MEMORY_INIT), 5000u, false},
    {DIAG_STAGE_SINGLE_REGISTER, "single-register", STAGE_BIT(DIAG_STAGE_MEMORY_INIT), 2000u, false},
    {DIAG_STAGE_BURST_REGISTER, "burst-register", STAGE_BIT(DIAG_STAGE_MEMORY_INIT), 2000u, false},
    {DIAG_STAGE_POINTER_SEQUENTIAL, "pointer-sequential", STAGE_BIT(DIAG_STAGE_MEMORY_INIT), 2000u, false},
    {DIAG_STAGE_POINTER_BURST, "pointer-burst", STAGE_BIT(DIAG_STAGE_MEMORY_INIT), 2000u, false},
    {DIAG_STAGE_POINTER_API, "pointer-api", STAGE_BIT(DIAG_STAGE_MEMORY_INIT), 2000u, false},
    {DIAG_STAGE_SOCKET_OPEN, "socket-open", STAGE_BIT(DIAG_STAGE_PHY_LINK) | STAGE_BIT(DIAG_STAGE_POINTER_API), 2000u, true},
    {DIAG_STAGE_UDP, "udp", STAGE_BIT(DIAG_STAGE_SOCKET_OPEN), 5000u, false},
    {DIAG_STAGE_DHCP, "dhcp", STAGE_BIT(DIAG_STAGE_PHY_LINK) | STAGE_BIT(DIAG_STAGE_POINTER_API), 60000u, false},
};

_Static_assert(DIAG_STAGE_COUNT <= 32, "stage mask exceeds uint32_t");
_Static_assert(sizeof(stages) / sizeof(stages[0]) == DIAG_STAGE_COUNT,
               "stage catalog does not match stable IDs");

static bool valid_stage(diag_stage_id_t id)
{
    return id < DIAG_STAGE_COUNT;
}

void diag_runner_init(diag_runner_t *runner)
{
    if (runner != NULL) {
        *runner = (diag_runner_t){0};
    }
}

const diag_stage_descriptor_t *diag_stage_descriptor(diag_stage_id_t id)
{
    if (!valid_stage(id)) {
        return NULL;
    }
    return &stages[id];
}

const diag_stage_descriptor_t *diag_stage_by_name(const char *name)
{
    size_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0u; index < DIAG_STAGE_COUNT; ++index) {
        if (strcmp(name, stages[index].name) == 0) {
            return &stages[index];
        }
    }
    return NULL;
}

bool diag_runner_can_run(const diag_runner_t *runner, diag_stage_id_t id,
                         bool network_configured)
{
    const diag_stage_descriptor_t *stage = diag_stage_descriptor(id);

    if (runner == NULL || stage == NULL ||
        (stage->requires_network_config && !network_configured)) {
        return false;
    }
    return (runner->passed_mask & stage->required_mask) == stage->required_mask;
}

uint32_t diag_runner_begin(diag_runner_t *runner, diag_stage_id_t id)
{
    if (runner == NULL || !valid_stage(id)) {
        return 0u;
    }
    runner->passed_mask &= ~STAGE_BIT(id);
    ++runner->sequence;
    return runner->sequence;
}

void diag_runner_finish(diag_runner_t *runner, diag_stage_id_t id,
                        diag_stage_result_t result)
{
    if (runner == NULL || !valid_stage(id)) {
        return;
    }
    if (result == DIAG_STAGE_PASS) {
        runner->passed_mask |= STAGE_BIT(id);
    } else {
        runner->passed_mask &= ~STAGE_BIT(id);
    }
}

void diag_runner_prepare_repeat(diag_runner_t *runner, diag_stage_id_t id)
{
    diag_stage_id_t restart = id;

    if (runner == NULL || !valid_stage(id)) {
        return;
    }
    if (id == DIAG_STAGE_UDP) {
        restart = DIAG_STAGE_SOCKET_OPEN;
    } else if (id == DIAG_STAGE_DHCP) {
        restart = DIAG_STAGE_CHIP_RESET;
    }
    runner->passed_mask &= STAGE_BIT(restart) - 1u;
}
