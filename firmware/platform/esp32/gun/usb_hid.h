#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gg_usb_transport.h"

void usb_hid_init(void);
void usb_hid_service(void);
bool usb_hid_poll_event(gg_transport_event_t *event);
uint32_t usb_hid_event_overflow_count(void);

bool usb_hid_submit_mouse(const gg_hid_report_t *report);
bool usb_hid_submit_keyboard(const gg_hid_report_t *report);
void usb_hid_request_reconnect(void);
