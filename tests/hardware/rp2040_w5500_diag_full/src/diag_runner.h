#ifndef W5500_DIAG_RUNNER_H
#define W5500_DIAG_RUNNER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DIAG_STAGE_CALLBACK_LAYOUT = 0,
    DIAG_STAGE_TRANSPORT_INIT = 1,
    DIAG_STAGE_CHIP_RESET = 2,
    DIAG_STAGE_VERSION = 3,
    DIAG_STAGE_MEMORY_INIT = 4,
    DIAG_STAGE_PHY_LINK = 5,
    DIAG_STAGE_SINGLE_REGISTER = 6,
    DIAG_STAGE_BURST_REGISTER = 7,
    DIAG_STAGE_POINTER_SEQUENTIAL = 8,
    DIAG_STAGE_POINTER_BURST = 9,
    DIAG_STAGE_POINTER_API = 10,
    DIAG_STAGE_SOCKET_OPEN = 11,
    DIAG_STAGE_UDP = 12,
    DIAG_STAGE_DHCP = 13,
    DIAG_STAGE_COUNT = 14
} diag_stage_id_t;

typedef enum {
    DIAG_STAGE_NOT_RUN = 0,
    DIAG_STAGE_PASS,
    DIAG_STAGE_FAIL,
    DIAG_STAGE_TIMEOUT
} diag_stage_result_t;

enum {
    DIAG_PHASE_SAVE = 1,
    DIAG_PHASE_WRITE_TX = 2,
    DIAG_PHASE_READ_TX = 3,
    DIAG_PHASE_WRITE_RX = 4,
    DIAG_PHASE_READ_RX = 5,
    DIAG_PHASE_RESTORE = 6,

    DIAG_PHASE_SET_NETINFO = 10,
    DIAG_PHASE_SOCKET_OPEN = 11,
    DIAG_PHASE_SET_IOMODE = 12,
    DIAG_PHASE_SOCKET_STATUS = 13,
    DIAG_PHASE_TX_FSR = 14,
    DIAG_PHASE_RX_RSR = 15,

    DIAG_PHASE_TX_WR_BEFORE = 16,
    DIAG_PHASE_SEND_IR_CLEAR = 17,
    DIAG_PHASE_SENDTO = 18,
    DIAG_PHASE_SEND_IR_READ = 19,
    DIAG_PHASE_TX_WR_AFTER = 20,
    DIAG_PHASE_RX_RD_BEFORE = 21,
    DIAG_PHASE_RX_RSR_POLL = 22,
    DIAG_PHASE_RECVFROM = 23,
    DIAG_PHASE_RX_RD_AFTER = 24,

    DIAG_PHASE_CHIP_RESET = 25,
    DIAG_PHASE_MEMORY_INIT = 26,
    DIAG_PHASE_PHY_LINK = 27,
    DIAG_PHASE_DHCP_INIT = 28,
    DIAG_PHASE_DHCP_RUN = 29,
    DIAG_PHASE_LEASE_APPLY = 30,
    DIAG_PHASE_DHCP_STOP = 31,

    DIAG_PHASE_SNAPSHOT_SR = 32,
    DIAG_PHASE_SNAPSHOT_IR = 33,
    DIAG_PHASE_SNAPSHOT_TX_WR = 34,
    DIAG_PHASE_SNAPSHOT_RX_RD = 35,
    DIAG_PHASE_SNAPSHOT_TX_FSR = 36,
    DIAG_PHASE_SNAPSHOT_RX_RSR = 37
};

typedef struct {
    diag_stage_id_t id;
    const char *name;
    uint32_t required_mask;
    uint32_t timeout_ms;
    bool requires_network_config;
} diag_stage_descriptor_t;

typedef struct {
    uint32_t passed_mask;
    uint32_t sequence;
    bool recovery_mode;
} diag_runner_t;

void diag_runner_init(diag_runner_t *runner);
const diag_stage_descriptor_t *diag_stage_descriptor(diag_stage_id_t id);
const diag_stage_descriptor_t *diag_stage_by_name(const char *name);
const char *diag_stage_phase_name(diag_stage_id_t stage, uint16_t phase);
bool diag_runner_can_run(const diag_runner_t *runner, diag_stage_id_t id,
                         bool network_configured);
uint32_t diag_runner_begin(diag_runner_t *runner, diag_stage_id_t id);
void diag_runner_finish(diag_runner_t *runner, diag_stage_id_t id,
                        diag_stage_result_t result);
void diag_runner_prepare_repeat(diag_runner_t *runner, diag_stage_id_t id);
diag_stage_id_t diag_runner_execution_start(diag_stage_id_t id);

#endif
