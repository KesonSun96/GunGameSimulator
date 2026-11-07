/*
 * Composite USB HID device: interface 0 = mouse, interface 1 = keyboard.
 * Only USB events and submit/completion are exposed to the application; the
 * TinyUSB callbacks never modify controller state directly.
 */
#include "usb_hid.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "class/hid/hid.h"
#include "class/hid/hid_device.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tinyusb.h"
#include "tusb.h"

#include "board_config.h"

static const char *TAG = "usb_hid";

#define ITF_MOUSE 0
#define ITF_KEYBOARD 1
#define ITF_COUNT 2
#define EPNUM_MOUSE 0x81
#define EPNUM_KEYBOARD 0x82
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN * 2)
#define USB_EVENT_QUEUE_DEPTH 32

static const uint8_t s_mouse_report_desc[] = {TUD_HID_REPORT_DESC_MOUSE()};
static const uint8_t s_keyboard_report_desc[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

static const uint8_t s_configuration_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_HID_DESCRIPTOR(ITF_MOUSE, 4, HID_ITF_PROTOCOL_MOUSE,
                       sizeof(s_mouse_report_desc), EPNUM_MOUSE, 8, 1),
    TUD_HID_DESCRIPTOR(ITF_KEYBOARD, 4, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(s_keyboard_report_desc), EPNUM_KEYBOARD, 8, 1),
};

static const tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = 64,
    .idVendor = USB_DEVICE_VID,
    .idProduct = USB_DEVICE_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const char *s_string_desc[] = {
    (const char[]){0x09, 0x04},
    USB_DEVICE_MANUFACTURER,
    USB_DEVICE_PRODUCT,
    USB_DEVICE_SERIAL,
    "Gun HID interface",
};

void usb_hid_init(void)
{
    gg_usb_transport_prepare(true);

    const tinyusb_config_t cfg = {
        .device_descriptor = &s_device_desc,
        .string_descriptor = s_string_desc,
        .string_descriptor_count = sizeof(s_string_desc) / sizeof(s_string_desc[0]),
        .external_phy = false,
        .configuration_descriptor = s_configuration_desc,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));
    gg_usb_transport_start();
    ESP_LOGI(TAG, "composite HID installed (mouse + keyboard)");
}

bool usb_hid_poll_event(gg_transport_event_t *event) { return gg_usb_transport_poll(event); }
uint32_t usb_hid_event_overflow_count(void) { return gg_usb_transport_overflows(); }
void usb_hid_service(void) {}
void usb_hid_request_reconnect(void) { gg_usb_transport_reconnect(); }
bool usb_hid_submit_mouse(const gg_hid_report_t *report)
{
    return report && report->itf == GG_HID_ITF_MOUSE && gg_usb_transport_submit(report);
}
bool usb_hid_submit_keyboard(const gg_hid_report_t *report)
{
    return report && report->itf == GG_HID_ITF_KEYBOARD && gg_usb_transport_submit(report);
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return (instance == ITF_MOUSE) ? s_mouse_report_desc : s_keyboard_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

