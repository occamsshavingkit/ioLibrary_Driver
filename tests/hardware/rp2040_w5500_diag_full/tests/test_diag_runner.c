#include "diag_runner.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    static const struct {
        diag_stage_id_t id;
        const char *name;
        uint32_t required_mask;
        uint32_t timeout_ms;
        bool requires_network_config;
    } expected[] = {
        {DIAG_STAGE_CALLBACK_LAYOUT, "callback-layout", 0u, 0u, false},
        {DIAG_STAGE_TRANSPORT_INIT, "transport-init", 1u << DIAG_STAGE_CALLBACK_LAYOUT, 2000u, false},
        {DIAG_STAGE_CHIP_RESET, "chip-reset", 1u << DIAG_STAGE_TRANSPORT_INIT, 2000u, false},
        {DIAG_STAGE_VERSION, "version", 1u << DIAG_STAGE_CHIP_RESET, 2000u, false},
        {DIAG_STAGE_MEMORY_INIT, "memory-init", 1u << DIAG_STAGE_VERSION, 2000u, false},
        {DIAG_STAGE_PHY_LINK, "phy-link", 1u << DIAG_STAGE_MEMORY_INIT, 5000u, false},
        {DIAG_STAGE_SINGLE_REGISTER, "single-register", 1u << DIAG_STAGE_MEMORY_INIT, 2000u, false},
        {DIAG_STAGE_BURST_REGISTER, "burst-register", 1u << DIAG_STAGE_MEMORY_INIT, 2000u, false},
        {DIAG_STAGE_POINTER_SEQUENTIAL, "pointer-sequential", 1u << DIAG_STAGE_MEMORY_INIT, 2000u, false},
        {DIAG_STAGE_POINTER_BURST, "pointer-burst", 1u << DIAG_STAGE_MEMORY_INIT, 2000u, false},
        {DIAG_STAGE_POINTER_API, "pointer-api", 1u << DIAG_STAGE_MEMORY_INIT, 2000u, false},
        {DIAG_STAGE_SOCKET_OPEN, "socket-open", (1u << DIAG_STAGE_PHY_LINK) | (1u << DIAG_STAGE_POINTER_API), 2000u, true},
        {DIAG_STAGE_UDP, "udp", 1u << DIAG_STAGE_SOCKET_OPEN, 5000u, false},
        {DIAG_STAGE_DHCP, "dhcp", (1u << DIAG_STAGE_PHY_LINK) | (1u << DIAG_STAGE_POINTER_API), 60000u, false},
    };
    diag_runner_t runner;
    size_t index;

    diag_runner_init(&runner);
    assert(runner.passed_mask == 0u);
    assert(runner.sequence == 0u);
    assert(!runner.recovery_mode);

    assert(sizeof(expected) / sizeof(expected[0]) == DIAG_STAGE_COUNT);
    for (index = 0u; index < DIAG_STAGE_COUNT; ++index) {
        const diag_stage_descriptor_t *stage = diag_stage_descriptor(expected[index].id);

        assert(stage != NULL);
        assert(stage->id == expected[index].id);
        assert(strcmp(stage->name, expected[index].name) == 0);
        assert(stage->required_mask == expected[index].required_mask);
        assert(stage->timeout_ms == expected[index].timeout_ms);
        assert(stage->requires_network_config == expected[index].requires_network_config);
        assert(diag_stage_by_name(expected[index].name) == stage);
    }
    assert(diag_stage_descriptor(DIAG_STAGE_COUNT) == NULL);

    const diag_stage_descriptor_t *pointer = diag_stage_by_name("pointer-api");
    assert(pointer != NULL);
    assert(pointer->id == DIAG_STAGE_POINTER_API);
    assert(pointer->timeout_ms == 2000u);
    assert(diag_stage_by_name("unknown") == NULL);

    assert(diag_runner_can_run(&runner, DIAG_STAGE_CALLBACK_LAYOUT, false));
    assert(!diag_runner_can_run(&runner, DIAG_STAGE_TRANSPORT_INIT, false));

    uint32_t sequence = diag_runner_begin(&runner, DIAG_STAGE_CALLBACK_LAYOUT);
    assert(sequence == 1u);
    assert(diag_runner_begin(&runner, DIAG_STAGE_CALLBACK_LAYOUT) == 2u);
    diag_runner_finish(&runner, DIAG_STAGE_CALLBACK_LAYOUT, DIAG_STAGE_PASS);
    assert(diag_runner_can_run(&runner, DIAG_STAGE_TRANSPORT_INIT, false));

    diag_runner_finish(&runner, DIAG_STAGE_TRANSPORT_INIT, DIAG_STAGE_PASS);
    assert(!diag_runner_can_run(&runner, DIAG_STAGE_SOCKET_OPEN, false));

    runner.passed_mask = (1u << DIAG_STAGE_PHY_LINK) | (1u << DIAG_STAGE_POINTER_API);
    assert(!diag_runner_can_run(&runner, DIAG_STAGE_SOCKET_OPEN, false));
    assert(diag_runner_can_run(&runner, DIAG_STAGE_SOCKET_OPEN, true));
    assert(diag_runner_can_run(&runner, DIAG_STAGE_DHCP, false));

    runner.passed_mask = 1u << DIAG_STAGE_CALLBACK_LAYOUT;
    diag_runner_finish(&runner, DIAG_STAGE_TRANSPORT_INIT, DIAG_STAGE_FAIL);
    assert(!diag_runner_can_run(&runner, DIAG_STAGE_CHIP_RESET, false));
    assert((runner.passed_mask & (1u << DIAG_STAGE_CALLBACK_LAYOUT)) != 0u);
    diag_runner_finish(&runner, DIAG_STAGE_TRANSPORT_INIT, DIAG_STAGE_PASS);
    diag_runner_finish(&runner, DIAG_STAGE_TRANSPORT_INIT, DIAG_STAGE_TIMEOUT);
    assert((runner.passed_mask & (1u << DIAG_STAGE_TRANSPORT_INIT)) == 0u);

    runner.passed_mask = (1u << DIAG_STAGE_COUNT) - 1u;
    diag_runner_prepare_repeat(&runner, DIAG_STAGE_POINTER_API);
    assert((runner.passed_mask & (1u << DIAG_STAGE_POINTER_API)) == 0u);
    assert((runner.passed_mask & (1u << DIAG_STAGE_POINTER_BURST)) != 0u);

    runner.passed_mask = (1u << DIAG_STAGE_COUNT) - 1u;
    diag_runner_prepare_repeat(&runner, DIAG_STAGE_UDP);
    assert((runner.passed_mask & (1u << DIAG_STAGE_SOCKET_OPEN)) == 0u);
    assert((runner.passed_mask & (1u << DIAG_STAGE_POINTER_API)) != 0u);

    runner.passed_mask = (1u << DIAG_STAGE_COUNT) - 1u;
    diag_runner_prepare_repeat(&runner, DIAG_STAGE_DHCP);
    assert((runner.passed_mask & (1u << DIAG_STAGE_DHCP)) == 0u);
    assert((runner.passed_mask & (1u << DIAG_STAGE_UDP)) != 0u);
    assert((runner.passed_mask & (1u << DIAG_STAGE_CHIP_RESET)) != 0u);
    assert((runner.passed_mask & (1u << DIAG_STAGE_TRANSPORT_INIT)) != 0u);

    assert(diag_runner_execution_start(DIAG_STAGE_UDP) == DIAG_STAGE_SOCKET_OPEN);
    assert(diag_runner_execution_start(DIAG_STAGE_DHCP) == DIAG_STAGE_DHCP);
    assert(diag_runner_execution_start(DIAG_STAGE_POINTER_API) == DIAG_STAGE_POINTER_API);
    return 0;
}
