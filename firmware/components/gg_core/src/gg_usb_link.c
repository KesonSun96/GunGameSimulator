#include "gg_usb_link.h"

#include <string.h>

void gg_usb_link_disconnect(gg_usb_link_t *link)
{
    link->connected = false;
    memset(link->slots, 0, sizeof(link->slots));
}

void gg_usb_link_connect(gg_usb_link_t *link)
{
    /* Mount follows an endpoint reset; old transfers have been cancelled. */
    gg_usb_link_disconnect(link);
    link->session++;
    link->connected = true;
}

void gg_usb_link_suspend(gg_usb_link_t *link)
{
    link->connected = false;
    /* Suspend does not reset endpoints: retain old tokens until completion. */
}

void gg_usb_link_resume(gg_usb_link_t *link)
{
    link->session++;
    link->connected = true;
}

bool gg_usb_link_begin(gg_usb_link_t *link, const gg_hid_report_t *report, uint64_t now_us)
{
    if (report == NULL || report->itf >= GG_HID_ITF_COUNT ||
        !link->connected || report->session != link->session ||
        link->slots[report->itf].active) {
        return false;
    }
    link->slots[report->itf].active = true;
    link->slots[report->itf].report = *report;
    link->slots[report->itf].started_us = now_us;
    return true;
}

bool gg_usb_link_finish(gg_usb_link_t *link, uint8_t itf, gg_hid_report_t *report)
{
    if (itf >= GG_HID_ITF_COUNT || !link->slots[itf].active) {
        return false;
    }
    *report = link->slots[itf].report;
    link->slots[itf].active = false;
    return true;
}

bool gg_usb_link_expired(const gg_usb_link_t *link, uint64_t now_us)
{
    for (int i = 0; i < GG_HID_ITF_COUNT; i++) {
        if (link->slots[i].active && now_us - link->slots[i].started_us > GG_HID_INFLIGHT_TIMEOUT_US) {
            return true;
        }
    }
    return false;
}
