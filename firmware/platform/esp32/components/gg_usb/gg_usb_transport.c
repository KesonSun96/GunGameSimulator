#include "gg_usb_transport.h"

#include <stdatomic.h>
#ifdef GG_USB_TRANSPORT_HOST_TEST
#include "usb_transport_test_port.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "tusb.h"
#endif
#include "gg_usb_link.h"

#if !CONFIG_TINYUSB_NO_DEFAULT_TASK || CONFIG_TINYUSB_INIT_IN_DEFAULT_TASK
#error "gg_usb requires NO_DEFAULT_TASK=y and INIT_IN_DEFAULT_TASK=n"
#endif

static QueueHandle_t s_commands;
static QueueHandle_t s_events;
static TaskHandle_t s_task;
static atomic_uint s_overflows;
static gg_usb_link_t s_link;
static bool s_has_mouse;
static uint64_t s_reconnect_at;

static void publish(gg_transport_event_type_t type, const gg_hid_report_t *report)
{
    const gg_transport_event_t event = {
        .type = type, .session = report ? report->session : s_link.session,
        .itf = report ? report->itf : 0, .submit_id = report ? report->submit_id : 0,
    };
    if (xQueueSend(s_events, &event, 0) != pdTRUE) {
        atomic_fetch_add_explicit(&s_overflows, 1, memory_order_relaxed);
    }
}

static void reconnect(uint64_t now_us)
{
    tud_disconnect();
    gg_usb_link_disconnect(&s_link);
    /* Software detach need not generate an unmount callback. */
    publish(GG_TRANSPORT_DISCONNECT, NULL);
    s_reconnect_at = now_us + 100000u;
}

static void submit(const gg_hid_report_t *report, uint64_t now_us)
{
    const uint8_t physical = s_has_mouse ? report->itf : 0;
    if (s_reconnect_at || !tud_hid_n_ready(physical) ||
        !gg_usb_link_begin(&s_link, report, now_us)) {
        publish(GG_TRANSPORT_FAILED, report);
        return;
    }
    if (!tud_hid_n_report(physical, 0, report->data, report->len)) {
        gg_hid_report_t rejected;
        (void)gg_usb_link_finish(&s_link, report->itf, &rejected);
        publish(GG_TRANSPORT_FAILED, &rejected);
    }
}

static void usb_task(void *arg)
{
    (void)arg;
    while (true) {
        /* Stack calls and callbacks share this task. Idle wait <= 1 ms. */
        tud_task_ext(1, false);
        const uint64_t now_us = (uint64_t)esp_timer_get_time();
        uint32_t flags = 0;
        (void)xTaskNotifyWait(0, UINT32_MAX, &flags, 0);
        if ((flags & 1u) || gg_usb_link_expired(&s_link, now_us)) {
            reconnect(now_us);
        }
        if (s_reconnect_at && now_us >= s_reconnect_at) {
            s_reconnect_at = 0;
            tud_connect();
        }
        gg_hid_report_t report;
        for (int i = 0; i < 4 && xQueueReceive(s_commands, &report, 0) == pdTRUE; i++) {
            submit(&report, now_us);
        }
    }
}

void gg_usb_transport_prepare(bool has_mouse)
{
    s_has_mouse = has_mouse;
    s_commands = xQueueCreate(4, sizeof(gg_hid_report_t));
    s_events = xQueueCreate(32, sizeof(gg_transport_event_t));
    configASSERT(s_commands && s_events);
}

void gg_usb_transport_start(void)
{
    const BaseType_t ok = xTaskCreatePinnedToCore(usb_task, "gg_usb", 4096, NULL, 5, &s_task, 0);
    configASSERT(ok == pdPASS);
}

bool gg_usb_transport_submit(const gg_hid_report_t *report)
{
    if (!report || report->itf >= GG_HID_ITF_COUNT ||
        (!s_has_mouse && report->itf != GG_HID_ITF_KEYBOARD) ||
        report->len != (report->itf == GG_HID_ITF_MOUSE ? 5 : 8)) {
        return false;
    }
    /* Queued acceptance; stack rejection is delivered as FAILED. */
    return xQueueSend(s_commands, report, 0) == pdTRUE;
}

bool gg_usb_transport_poll(gg_transport_event_t *event)
{
    return xQueueReceive(s_events, event, 0) == pdTRUE;
}

uint32_t gg_usb_transport_overflows(void)
{
    return atomic_load_explicit(&s_overflows, memory_order_relaxed);
}

void gg_usb_transport_reconnect(void)
{
    (void)xTaskNotify(s_task, 1u, eSetBits);
}

void tud_mount_cb(void)
{
    gg_usb_link_connect(&s_link);
    publish(GG_TRANSPORT_CONNECT, NULL);
}

void tud_umount_cb(void)
{
    gg_usb_link_disconnect(&s_link);
    publish(GG_TRANSPORT_DISCONNECT, NULL);
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    gg_usb_link_suspend(&s_link);
    publish(GG_TRANSPORT_SUSPEND, NULL);
}

void tud_resume_cb(void)
{
    gg_usb_link_resume(&s_link);
    publish(GG_TRANSPORT_RESUME, NULL);
}

static void finish(uint8_t physical, bool success)
{
    if (physical >= (s_has_mouse ? 2 : 1)) {
        return;
    }
    const uint8_t logical = s_has_mouse ? physical : GG_HID_ITF_KEYBOARD;
    gg_hid_report_t report;
    if (gg_usb_link_finish(&s_link, logical, &report)) {
        publish(success ? GG_TRANSPORT_COMPLETE : GG_TRANSPORT_FAILED, &report);
    }
}

void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report, uint16_t len)
{
    (void)report;
    (void)len;
    finish(instance, true);
}

void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t type,
                              const uint8_t *report, uint16_t len)
{
    (void)report;
    (void)len;
    if (type == HID_REPORT_TYPE_INPUT) {
        finish(instance, false);
    }
}
