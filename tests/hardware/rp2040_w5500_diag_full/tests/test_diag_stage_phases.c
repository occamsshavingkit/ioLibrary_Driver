#include "diag_runner.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_POINTER_API,
                                        DIAG_PHASE_SAVE),
                  "save") == 0);
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_POINTER_API,
                                        DIAG_PHASE_WRITE_TX),
                  "write-tx") == 0);
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_POINTER_API,
                                        DIAG_PHASE_READ_TX),
                  "read-tx") == 0);
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_POINTER_API,
                                        DIAG_PHASE_WRITE_RX),
                  "write-rx") == 0);
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_POINTER_API,
                                        DIAG_PHASE_READ_RX),
                  "read-rx") == 0);
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_POINTER_API,
                                        DIAG_PHASE_RESTORE),
                  "restore") == 0);

    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_POINTER_SEQUENTIAL,
                                        DIAG_PHASE_WRITE_TX),
                  "write-tx") == 0);
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_POINTER_BURST,
                                        DIAG_PHASE_READ_RX),
                  "read-rx") == 0);
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_VERSION, 1u),
                  "driver-call") == 0);
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_VERSION,
                                        DIAG_PHASE_WRITE_TX),
                  "unknown") == 0);
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_POINTER_API, 0u),
                  "unknown") == 0);
    assert(strcmp(diag_stage_phase_name(DIAG_STAGE_COUNT, DIAG_PHASE_SAVE),
                  "unknown") == 0);
    return 0;
}
