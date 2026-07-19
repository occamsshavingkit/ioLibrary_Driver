#ifndef W5500_DIAG_WATCHDOG_H
#define W5500_DIAG_WATCHDOG_H

#include "diag_journal.h"

#include <stdbool.h>
#include <stdint.h>

bool diag_watchdog_recover(diag_journal_t *journal);
void diag_watchdog_begin(diag_stage_id_t stage, uint16_t phase,
                         uint32_t sequence, uint32_t timeout_ms);
void diag_watchdog_complete(void);
void diag_watchdog_feed(void);

#endif
