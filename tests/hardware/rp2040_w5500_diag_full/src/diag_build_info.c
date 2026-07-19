#include "diag_build_info.h"

#include "diag_protocol.h"

int diag_format_boot_event(char *buffer, size_t capacity)
{
    static const char details[] =
        "git=" DIAG_GIT_SHA
        " dirty=" DIAG_GIT_DIRTY
        " diff=" DIAG_DIFF_SHA256
        " build=" DIAG_BUILD_UTC;

    return diag_format_event(buffer, capacity, 0u, "boot", "PASS", details);
}
