#include "diag_build_info.h"
#include "diag_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    static const char expected[] =
        "DIAG protocol=1 seq=0 stage=boot event=PASS "
        "git=0123456789abcdef0123456789abcdef01234567 dirty=1 "
        "diff=89abcdef0123456789abcdef0123456789abcdef0123456789abcdef01234567 "
        "build=2026-07-19T12:34:56Z\n";
    char line[DIAG_LINE_MAX + 1u];
    int written = diag_format_boot_event(line, sizeof(line));

    assert(sizeof(expected) - 1u == 194u);
    assert(written == (int)(sizeof(expected) - 1u));
    assert(strcmp(line, expected) == 0);
    return 0;
}
