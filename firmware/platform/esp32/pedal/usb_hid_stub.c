/* Preserve the internal USB PHY for Serial/JTAG in the debug build. */
#include "usb_hid_kbd.h"

#include <stddef.h>

#include "esp_log.h"

void usb_hid_init(void)
{
    ESP_LOGW("usb_stub", "JTAG debug build: HID disabled; no game input");
}

void usb_hid_service(void) {}

bool usb_hid_poll_event(gg_transport_event_t *event)
{
    (void)event;
    return false;
}

uint32_t usb_hid_event_overflow_count(void)
{
    return 0;
}

bool usb_hid_submit_keyboard(const gg_hid_report_t *report)
{
    (void)report;
    return false;
}

void usb_hid_request_reconnect(void) {}
