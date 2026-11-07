#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GG_HID_QUEUE_DEPTH 32
#define GG_HID_MOTION_LIMIT 256
#define GG_HID_MOTION_TIMEOUT_US 20000u
#define GG_HID_INFLIGHT_TIMEOUT_US 100000u
#define GG_HID_REPORT_MAX 8

typedef enum {
    GG_HID_ITF_MOUSE = 0,
    GG_HID_ITF_KEYBOARD = 1,
    GG_HID_ITF_COUNT = 2,
} gg_hid_itf_t;

typedef struct {
    uint8_t itf;
    uint8_t data[GG_HID_REPORT_MAX];
    uint8_t len;
    uint32_t session;
    uint32_t submit_id;
    bool motion;
} gg_hid_report_t;

typedef bool (*gg_hid_submit_fn)(void *ctx, const gg_hid_report_t *report);
typedef void (*gg_hid_abort_fn)(void *ctx);

typedef struct {
    uint8_t mouse_buttons;
    uint8_t kbd_modifier;
    uint8_t kbd_keys[6];
} gg_hid_snapshot_t;

typedef struct {
    gg_hid_snapshot_t items[GG_HID_QUEUE_DEPTH];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint32_t high_water;
} gg_hid_queue_t;

typedef struct {
    bool active;
    bool pop_on_complete;
    gg_hid_report_t report;
    uint64_t submit_us;
} gg_hid_inflight_t;

typedef struct {
    bool mouse_enabled;
    bool keyboard_enabled;
    bool connected;
    bool suspended;
    bool recovering;
    uint32_t session;
    uint32_t next_submit_id;

    gg_hid_snapshot_t latest;
    gg_hid_snapshot_t shadow;
    gg_hid_queue_t queues[GG_HID_ITF_COUNT];
    gg_hid_inflight_t inflight[GG_HID_ITF_COUNT];

    int32_t motion_x;
    int32_t motion_y;
    uint64_t motion_start_us;
    bool motion_active;

    gg_hid_submit_fn submit;
    gg_hid_abort_fn abort;
    void *ctx;

    uint32_t overflow_count;
    uint32_t dropped_motion;
    uint32_t motion_timeouts;
    uint32_t completed_reports;
    uint32_t rejected_submits;
    uint32_t failed_reports;
    uint32_t inflight_timeouts;
    uint32_t queue_recovery_count;
} gg_hid_output_t;

void gg_hid_output_init(gg_hid_output_t *out, bool mouse_enabled, bool keyboard_enabled,
                        gg_hid_submit_fn submit, gg_hid_abort_fn abort, void *ctx);

void gg_hid_output_set_mouse_buttons(gg_hid_output_t *out, uint8_t buttons, uint64_t now_us);
void gg_hid_output_set_keyboard(gg_hid_output_t *out, uint8_t modifier,
                                const uint8_t keys[6], uint64_t now_us);
void gg_hid_output_add_motion(gg_hid_output_t *out, int32_t dx, int32_t dy, uint64_t now_us);
void gg_hid_output_clear_motion(gg_hid_output_t *out);

void gg_hid_output_tick(gg_hid_output_t *out, uint64_t now_us);

void gg_hid_output_on_connect(gg_hid_output_t *out, uint32_t session, uint64_t now_us);
void gg_hid_output_on_disconnect(gg_hid_output_t *out);
void gg_hid_output_on_suspend(gg_hid_output_t *out);
void gg_hid_output_on_resume(gg_hid_output_t *out, uint32_t session, uint64_t now_us);
void gg_hid_output_on_submit_complete(gg_hid_output_t *out, uint32_t session, uint8_t itf,
                                      uint32_t submit_id, uint64_t now_us);
void gg_hid_output_on_submit_failed(gg_hid_output_t *out, uint32_t session, uint8_t itf,
                                    uint32_t submit_id, uint64_t now_us);
