#include "diag_watchdog.h"

#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"

#include <stddef.h>

static void write_scratch(const uint32_t words[DIAG_JOURNAL_WORD_COUNT])
{
    size_t index;

    for (index = 0u; index < DIAG_JOURNAL_WORD_COUNT; ++index) {
        watchdog_hw->scratch[index] = words[index];
    }
}

static void clear_scratch(void)
{
    size_t index;

    for (index = 0u; index < DIAG_JOURNAL_WORD_COUNT; ++index) {
        watchdog_hw->scratch[index] = 0u;
    }
}

bool diag_watchdog_recover(diag_journal_t *journal)
{
    uint32_t words[DIAG_JOURNAL_WORD_COUNT];
    bool recovered;
    size_t index;

    for (index = 0u; index < DIAG_JOURNAL_WORD_COUNT; ++index) {
        words[index] = watchdog_hw->scratch[index];
    }
    recovered = watchdog_enable_caused_reboot() &&
                diag_journal_decode(words, journal);
    watchdog_disable();
    clear_scratch();
    return recovered;
}

void diag_watchdog_begin(diag_stage_id_t stage, uint16_t phase,
                         uint32_t sequence, uint32_t timeout_ms)
{
    const diag_journal_t journal = {
        .stage = stage,
        .phase = phase,
        .sequence = sequence,
    };
    uint32_t words[DIAG_JOURNAL_WORD_COUNT];

    diag_journal_encode(&journal, words);
    write_scratch(words);
    watchdog_enable(timeout_ms, false);
}

void diag_watchdog_complete(void)
{
    watchdog_disable();
    clear_scratch();
}

void diag_watchdog_feed(void)
{
    watchdog_update();
}
