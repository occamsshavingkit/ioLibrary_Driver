#include "diag_usb_identity.h"

#include "tusb.h"

#include <stddef.h>
#include <stdint.h>

enum {
    INTERFACE_CDC_CONTROL = 0,
    INTERFACE_CDC_DATA,
    INTERFACE_COUNT
};

enum {
    STRING_LANGUAGE = 0,
    STRING_MANUFACTURER,
    STRING_PRODUCT,
    STRING_SERIAL,
    STRING_CDC_INTERFACE
};

#define CONFIGURATION_TOTAL_LENGTH (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = DIAG_USB_VID,
    .idProduct = DIAG_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STRING_MANUFACTURER,
    .iProduct = STRING_PRODUCT,
    .iSerialNumber = STRING_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, INTERFACE_COUNT, 0, CONFIGURATION_TOTAL_LENGTH,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(INTERFACE_CDC_CONTROL, STRING_CDC_INTERFACE, 0x81, 8,
                       0x02, 0x82, 64),
};

static const char *const string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "occamsshavingkit",
    DIAG_USB_PRODUCT,
    "RP2040-DIAG",
    "Diagnostic console",
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return configuration_descriptor;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language_id)
{
    static uint16_t descriptor[32];
    const char *text;
    size_t length;
    size_t position;

    (void)language_id;
    if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0])) {
        return NULL;
    }

    text = string_descriptors[index];
    if (index == STRING_LANGUAGE) {
        descriptor[1] = (uint16_t)((uint8_t)text[0] | ((uint16_t)(uint8_t)text[1] << 8));
        length = 1u;
    } else {
        for (length = 0u; text[length] != '\0' && length < 31u; ++length) {
        }
        for (position = 0u; position < length; ++position) {
            descriptor[1u + position] = (uint8_t)text[position];
        }
    }
    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * length + 2u));
    return descriptor;
}
