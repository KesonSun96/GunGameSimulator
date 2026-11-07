#pragma once

#include "gg_hid_output.h"

/* Single USB-task ownership. Never inspect this state from the app task. */
typedef struct {
    bool connected;
    uint32_t session;
    struct {
        bool active;
        gg_hid_report_t report;
        uint64_t started_us;
    } slots[GG_HID_ITF_COUNT];
} gg_usb_link_t;

void gg_usb_link_connect(gg_usb_link_t *link);
void gg_usb_link_disconnect(gg_usb_link_t *link);
void gg_usb_link_suspend(gg_usb_link_t *link);
void gg_usb_link_resume(gg_usb_link_t *link);
bool gg_usb_link_begin(gg_usb_link_t *link, const gg_hid_report_t *report, uint64_t now_us);
bool gg_usb_link_finish(gg_usb_link_t *link, uint8_t itf, gg_hid_report_t *report);
bool gg_usb_link_expired(const gg_usb_link_t *link, uint64_t now_us);
