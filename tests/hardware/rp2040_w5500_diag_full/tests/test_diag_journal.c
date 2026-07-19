#include "diag_journal.h"

#include <assert.h>
#include <stddef.h>

static uint32_t checksum(const uint32_t words[DIAG_JOURNAL_WORD_COUNT])
{
    return words[0] ^ words[1] ^ words[2] ^ UINT32_C(0xA5A55A5A);
}

int main(void)
{
    uint32_t words[DIAG_JOURNAL_WORD_COUNT] = {0u};
    const diag_journal_t input = {
        .stage = DIAG_STAGE_POINTER_API,
        .phase = 2u,
        .sequence = 41u,
    };
    diag_journal_t output;

    diag_journal_encode(&input, words);
    assert(words[0] == DIAG_JOURNAL_MAGIC);
    assert(words[1] == UINT32_C(0x010A0002));
    assert(words[2] == 41u);
    assert(words[3] == checksum(words));
    assert(diag_journal_decode(words, &output));
    assert(output.stage == DIAG_STAGE_POINTER_API);
    assert(output.phase == 2u);
    assert(output.sequence == 41u);

    words[0] ^= 1u;
    assert(!diag_journal_decode(words, &output));
    diag_journal_encode(&input, words);
    words[3] ^= 1u;
    assert(!diag_journal_decode(words, &output));

    diag_journal_encode(&input, words);
    words[1] = (words[1] & UINT32_C(0x00FFFFFF)) | UINT32_C(0x02000000);
    words[3] = checksum(words);
    assert(!diag_journal_decode(words, &output));

    diag_journal_encode(&input, words);
    words[1] = (words[1] & UINT32_C(0xFF00FFFF)) |
               ((uint32_t)DIAG_STAGE_COUNT << 16);
    words[3] = checksum(words);
    assert(!diag_journal_decode(words, &output));

    diag_journal_clear(words);
    assert(words[0] == 0u);
    assert(words[1] == 0u);
    assert(words[2] == 0u);
    assert(words[3] == 0u);
    assert(!diag_journal_decode(words, &output));

    words[0] = 1u;
    words[1] = 2u;
    words[2] = 3u;
    words[3] = 4u;
    diag_journal_encode(NULL, words);
    assert(words[0] == 1u);
    assert(words[1] == 2u);
    assert(words[2] == 3u);
    assert(words[3] == 4u);
    diag_journal_encode(&input, NULL);
    assert(!diag_journal_decode(NULL, &output));
    assert(!diag_journal_decode(words, NULL));
    diag_journal_clear(NULL);
    return 0;
}
