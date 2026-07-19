#ifndef W5500_DIAG_BOARD_H
#define W5500_DIAG_BOARD_H

#include "w5500_diag_status_contract.h"

#include <stdbool.h>

w5500_diag_board_status_t w5500_diag_board_check_status_contract(bool expected);
w5500_diag_board_status_t w5500_diag_board_init(void);
void w5500_diag_board_reset(void);

#endif
