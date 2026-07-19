#ifndef W5500_DIAG_USB_H
#define W5500_DIAG_USB_H

#include <stdbool.h>
#include <stddef.h>

void diag_usb_init(void);
void diag_usb_task(void);
bool diag_usb_connected(void);
bool diag_usb_write_line(const char *line);
bool diag_usb_poll_line(char *line, size_t capacity);

#endif
