#pragma once
#include "gg_transport.h"

/* Prepare before driver install; start the sole USB task after install. */
void gg_usb_transport_prepare(bool has_mouse);
void gg_usb_transport_start(void);
bool gg_usb_transport_submit(const gg_hid_report_t *report);
bool gg_usb_transport_poll(gg_transport_event_t *event);
uint32_t gg_usb_transport_overflows(void);
void gg_usb_transport_reconnect(void);
