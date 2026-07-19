#include "diag_usb_identity.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(DIAG_USB_VID == 0x6666u);
    assert(DIAG_USB_PID == 0x4021u);
    assert(strcmp(DIAG_USB_PRODUCT, "RP2040 W5500 Diagnostic") == 0);
    return 0;
}
