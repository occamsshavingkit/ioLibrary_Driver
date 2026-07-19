#ifndef W5500_DIAG_JOURNAL_H
#define W5500_DIAG_JOURNAL_H

#include "diag_runner.h"

#include <stdbool.h>
#include <stdint.h>

#define DIAG_JOURNAL_MAGIC UINT32_C(0x57444941)
#define DIAG_JOURNAL_WORD_COUNT 4u

typedef struct {
    diag_stage_id_t stage;
    uint16_t phase;
    uint32_t sequence;
} diag_journal_t;

void diag_journal_encode(const diag_journal_t *journal,
                         uint32_t words[DIAG_JOURNAL_WORD_COUNT]);
bool diag_journal_decode(const uint32_t words[DIAG_JOURNAL_WORD_COUNT],
                         diag_journal_t *journal);
void diag_journal_clear(uint32_t words[DIAG_JOURNAL_WORD_COUNT]);

#endif
