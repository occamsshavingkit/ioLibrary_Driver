#include "diag_usb.h"

#include "diag_protocol.h"

#include "pico/time.h"
#include "tusb.h"

#include <stdint.h>
#include <string.h>

#define DIAG_USB_WRITE_TIMEOUT_US UINT64_C(250000)

static char receive_buffer[DIAG_LINE_MAX + 1u];
static size_t receive_length;
static bool receive_overflow;

void diag_usb_init(void)
{
    tusb_init();
}

void diag_usb_task(void)
{
    tud_task();
}

bool diag_usb_connected(void)
{
    return tud_cdc_connected();
}

bool diag_usb_write_line(const char *line)
{
    absolute_time_t deadline;
    size_t length;
    size_t offset = 0u;

    if (line == NULL) {
        return false;
    }
    for (length = 0u; line[length] != '\0' && length <= DIAG_LINE_MAX;
         ++length) {
    }
    if (length == 0u || length > DIAG_LINE_MAX) {
        return false;
    }

    deadline = make_timeout_time_us(DIAG_USB_WRITE_TIMEOUT_US);
    while (offset < length) {
        uint32_t available;
        size_t remaining;
        uint32_t chunk;

        tud_task();
        if (!tud_cdc_connected()) {
            return false;
        }
        available = tud_cdc_write_available();
        remaining = length - offset;
        chunk = available < remaining ? available : (uint32_t)remaining;
        if (chunk > 0u) {
            offset += tud_cdc_write(line + offset, chunk);
            tud_cdc_write_flush();
        }
        if (offset < length && time_reached(deadline)) {
            return false;
        }
    }

    tud_cdc_write_flush();
    return true;
}

bool diag_usb_poll_line(char *line, size_t capacity)
{
    while (tud_cdc_available() > 0u) {
        uint8_t byte;

        if (tud_cdc_read(&byte, 1u) != 1u) {
            break;
        }
        if (byte == (uint8_t)'\n') {
            size_t line_length = receive_length;

            if (line_length > 0u && receive_buffer[line_length - 1u] == '\r') {
                --line_length;
            }
            if (!receive_overflow && line != NULL && capacity > line_length) {
                memcpy(line, receive_buffer, line_length);
                line[line_length] = '\0';
                receive_length = 0u;
                receive_overflow = false;
                return true;
            }
            receive_length = 0u;
            receive_overflow = false;
            continue;
        }
        if (!receive_overflow) {
            if (receive_length < DIAG_LINE_MAX) {
                receive_buffer[receive_length] = (char)byte;
                ++receive_length;
            } else {
                receive_overflow = true;
            }
        }
    }

    return false;
}
