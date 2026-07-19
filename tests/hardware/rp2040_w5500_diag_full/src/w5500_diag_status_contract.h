#ifndef W5500_DIAG_STATUS_CONTRACT_H
#define W5500_DIAG_STATUS_CONTRACT_H

#include "wizchip_conf.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    W5500_DIAG_STATUS_OK = 0,
    W5500_DIAG_STATUS_NOT_CLAIMED,
    W5500_DIAG_STATUS_NO_API,
    W5500_DIAG_STATUS_ALIAS,
    W5500_DIAG_STATUS_PIO_OPEN
} w5500_diag_board_status_t;

typedef void (*w5500_diag_status_registrar_t)(uint8_t (*check_busy)(void),
                                               uint8_t (*get_error)(void));

w5500_diag_board_status_t w5500_diag_status_contract_classify(
    bool expected, w5500_diag_status_registrar_t registrar);

#endif
