#include "diag_journal.h"

#include "diag_protocol.h"

#include <stddef.h>

#define DIAG_JOURNAL_CHECKSUM_XOR UINT32_C(0xA5A55A5A)

static uint32_t journal_checksum(const uint32_t words[DIAG_JOURNAL_WORD_COUNT])
{
    return words[0] ^ words[1] ^ words[2] ^ DIAG_JOURNAL_CHECKSUM_XOR;
}

void diag_journal_encode(const diag_journal_t *journal,
                         uint32_t words[DIAG_JOURNAL_WORD_COUNT])
{
    if (journal == NULL || words == NULL) {
        return;
    }

    words[0] = DIAG_JOURNAL_MAGIC;
    words[1] = ((uint32_t)DIAG_PROTOCOL_VERSION << 24) |
               ((uint32_t)journal->stage << 16) | journal->phase;
    words[2] = journal->sequence;
    words[3] = journal_checksum(words);
}

bool diag_journal_decode(const uint32_t words[DIAG_JOURNAL_WORD_COUNT],
                         diag_journal_t *journal)
{
    uint32_t version;
    uint32_t stage;

    if (words == NULL || journal == NULL || words[0] != DIAG_JOURNAL_MAGIC ||
        words[3] != journal_checksum(words)) {
        return false;
    }

    version = words[1] >> 24;
    stage = (words[1] >> 16) & UINT32_C(0xFF);
    if (version != DIAG_PROTOCOL_VERSION || stage >= DIAG_STAGE_COUNT) {
        return false;
    }

    journal->stage = (diag_stage_id_t)stage;
    journal->phase = (uint16_t)words[1];
    journal->sequence = words[2];
    return true;
}

void diag_journal_clear(uint32_t words[DIAG_JOURNAL_WORD_COUNT])
{
    size_t index;

    if (words == NULL) {
        return;
    }
    for (index = 0u; index < DIAG_JOURNAL_WORD_COUNT; ++index) {
        words[index] = 0u;
    }
}
