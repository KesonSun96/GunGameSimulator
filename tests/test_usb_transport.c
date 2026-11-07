/* Executes production bridge code with deterministic RTOS/USB event ordering. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "usb_transport_test_port.h"
#define GG_USB_TRANSPORT_HOST_TEST
#include "../firmware/platform/esp32/components/gg_usb/gg_usb_transport.c"

struct test_queue {
    unsigned size, depth, head, count;
    uint8_t data[32][sizeof(gg_hid_report_t)];
};
static struct test_queue queues[2];
static unsigned next_queue;
static bool ready = true, accepted = true;
static uint8_t last_physical;
static int sends, disconnects;
static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

QueueHandle_t xQueueCreate(unsigned depth, unsigned size)
{
    assert(next_queue < 2 && depth <= 32 && size <= sizeof(gg_hid_report_t));
    QueueHandle_t q = &queues[next_queue++];
    q->depth = depth; q->size = size;
    return q;
}
int xQueueSend(QueueHandle_t q, const void *value, unsigned timeout)
{
    (void)timeout;
    if (q->count == q->depth) return 0;
    memcpy(q->data[(q->head + q->count++) % q->depth], value, q->size);
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *value, unsigned timeout)
{
    (void)timeout;
    if (!q->count) return 0;
    memcpy(value, q->data[q->head], q->size);
    q->head = (q->head + 1) % q->depth; q->count--;
    return pdTRUE;
}
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
                           void *arg, unsigned priority, TaskHandle_t *task, int core)
{
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)core;
    *task = (void *)1;
    return pdPASS;
}
int xTaskNotifyWait(uint32_t a, uint32_t b, uint32_t *flags, unsigned timeout)
{ (void)a; (void)b; (void)timeout; *flags = 0; return 0; }
int xTaskNotify(TaskHandle_t task, uint32_t flags, eNotifyAction action)
{ (void)task; (void)flags; (void)action; return pdTRUE; }
int64_t esp_timer_get_time(void) { return 1000; }
bool tud_disconnect(void) { disconnects++; return true; }
bool tud_connect(void) { return true; }
bool tud_hid_n_ready(uint8_t instance) { (void)instance; return ready; }
bool tud_hid_n_report(uint8_t instance, uint8_t id, const void *data, uint16_t len)
{ (void)id; (void)data; (void)len; last_physical = instance; sends++; return accepted; }
void tud_task_ext(uint32_t timeout, bool isr) { (void)timeout; (void)isr; }

static gg_transport_event_t event(void)
{
    gg_transport_event_t e = {0};
    CHECK(gg_usb_transport_poll(&e));
    return e;
}
static void deliver(const gg_hid_report_t *report)
{
    gg_hid_report_t copy;
    CHECK(gg_usb_transport_submit(report));
    CHECK(xQueueReceive(s_commands, &copy, 0));
    submit(&copy, 1000);
}

int main(void)
{
    gg_usb_transport_prepare(false); /* Pedal: physical 0, logical keyboard 1. */
    gg_usb_transport_start();
    tud_mount_cb();
    gg_transport_event_t e = event();
    CHECK(e.type == GG_TRANSPORT_CONNECT);
    gg_hid_report_t report = {.itf = GG_HID_ITF_KEYBOARD, .len = 8,
                             .session = e.session, .submit_id = 7};
    deliver(&report);
    CHECK(last_physical == 0 && sends == 1);
    tud_suspend_cb(false);
    CHECK(event().type == GG_TRANSPORT_SUSPEND);
    tud_resume_cb();
    e = event();
    CHECK(e.type == GG_TRANSPORT_RESUME && e.session != report.session);
    tud_hid_report_complete_cb(0, NULL, 8);
    e = event();
    CHECK(e.type == GG_TRANSPORT_COMPLETE && e.itf == GG_HID_ITF_KEYBOARD);
    CHECK(e.session == report.session && e.submit_id == 7);
    /* A queued command from the old session must never reach TinyUSB. */
    deliver(&report);
    CHECK(event().type == GG_TRANSPORT_FAILED && sends == 1);
    report.session = s_link.session; report.submit_id++;
    deliver(&report);
    tud_hid_report_failed_cb(0, HID_REPORT_TYPE_OUTPUT, NULL, 0);
    CHECK(s_events->count == 0);
    tud_hid_report_failed_cb(0, HID_REPORT_TYPE_INPUT, NULL, 0);
    e = event();
    CHECK(e.type == GG_TRANSPORT_FAILED && e.submit_id == report.submit_id);
    CHECK(e.itf == GG_HID_ITF_KEYBOARD && e.session == report.session);
    accepted = false; report.submit_id++;
    deliver(&report);
    CHECK(event().type == GG_TRANSPORT_FAILED);
    CHECK(!s_link.slots[GG_HID_ITF_KEYBOARD].active);
    ready = false;
    deliver(&report);
    CHECK(event().type == GG_TRANSPORT_FAILED);
    reconnect(1000);
    CHECK(disconnects == 1 && !s_link.connected);
    CHECK(event().type == GG_TRANSPORT_DISCONNECT);
    CHECK(s_reconnect_at == 101000);
    /* Deterministic notification overflow feeds the app recovery counter. */
    for (int i = 0; i < 33; i++) publish(GG_TRANSPORT_FAILED, &report);
    CHECK(gg_usb_transport_overflows() == 1);

    /* Gun: both physical interfaces retain independent submission tokens. */
    memset(queues, 0, sizeof(queues)); next_queue = 0;
    memset(&s_link, 0, sizeof(s_link)); s_reconnect_at = 0;
    ready = accepted = true;
    gg_usb_transport_prepare(true);
    tud_mount_cb();
    e = event();
    gg_hid_report_t mouse = {.itf = GG_HID_ITF_MOUSE, .len = 5,
                            .session = e.session, .submit_id = 91};
    report.session = e.session; report.submit_id = 92;
    deliver(&mouse);
    CHECK(last_physical == 0);
    deliver(&report);
    CHECK(last_physical == 1);
    tud_hid_report_complete_cb(1, NULL, 8);
    e = event();
    CHECK(e.itf == GG_HID_ITF_KEYBOARD && e.submit_id == 92);
    tud_hid_report_failed_cb(0, HID_REPORT_TYPE_INPUT, NULL, 0);
    e = event();
    CHECK(e.itf == GG_HID_ITF_MOUSE && e.submit_id == 91 && e.type == GG_TRANSPORT_FAILED);
    tud_hid_report_complete_cb(0, NULL, 5);
    CHECK(s_events->count == 0);
    CHECK(!gg_usb_transport_submit(NULL));
    mouse.len = 8;
    CHECK(!gg_usb_transport_submit(&mouse));
    mouse.len = 5;
    for (int i = 0; i < 4; i++) CHECK(gg_usb_transport_submit(&mouse));
    CHECK(!gg_usb_transport_submit(&mouse));
    printf("USB transport: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
