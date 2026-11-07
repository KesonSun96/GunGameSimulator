#include "gg_hid_output.h"

#include <string.h>

static void queue_clear(gg_hid_queue_t *q)
{
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

static bool queue_push(gg_hid_queue_t *q, const gg_hid_snapshot_t *snapshot)
{
    if (q->count >= GG_HID_QUEUE_DEPTH) {
        return false;
    }
    q->items[q->tail] = *snapshot;
    q->tail = (uint8_t)((q->tail + 1u) % GG_HID_QUEUE_DEPTH);
    q->count++;
    if (q->count > q->high_water) {
        q->high_water = q->count;
    }
    return true;
}

static const gg_hid_snapshot_t *queue_head(const gg_hid_queue_t *q)
{
    return &q->items[q->head];
}

static void queue_pop(gg_hid_queue_t *q)
{
    if (q->count > 0) {
        q->head = (uint8_t)((q->head + 1u) % GG_HID_QUEUE_DEPTH);
        q->count--;
    }
}

static bool keys_equal(const uint8_t a[6], const uint8_t b[6])
{
    return memcmp(a, b, 6) == 0;
}

static void build_mouse_report(gg_hid_report_t *report, uint8_t buttons, int32_t dx, int32_t dy)
{
    memset(report, 0, sizeof(*report));
    report->itf = GG_HID_ITF_MOUSE;
    report->len = 5;
    report->data[0] = buttons;
    report->data[1] = (uint8_t)(int8_t)dx;
    report->data[2] = (uint8_t)(int8_t)dy;
    report->data[3] = 0;
    report->data[4] = 0;
}

static void build_keyboard_report(gg_hid_report_t *report, uint8_t modifier,
                                  const uint8_t keys[6])
{
    memset(report, 0, sizeof(*report));
    report->itf = GG_HID_ITF_KEYBOARD;
    report->len = 8;
    report->data[0] = modifier;
    report->data[1] = 0;
    memcpy(&report->data[2], keys, 6);
}

static void enqueue(gg_hid_output_t *out, uint8_t itf, const gg_hid_snapshot_t *snapshot)
{
    if (!queue_push(&out->queues[itf], snapshot)) {
        out->overflow_count++;
        queue_clear(&out->queues[GG_HID_ITF_MOUSE]);
        queue_clear(&out->queues[GG_HID_ITF_KEYBOARD]);
        gg_hid_output_clear_motion(out);
        out->recovering = true;
        for (int i = 0; i < GG_HID_ITF_COUNT; i++) {
            if (out->inflight[i].active) {
                out->inflight[i].pop_on_complete = false;
            }
        }
    }
}

static void try_finish_recovery(gg_hid_output_t *out, uint64_t now_us)
{
    (void)now_us;
    if (!out->recovering || !out->connected || out->suspended) {
        return;
    }
    for (int i = 0; i < GG_HID_ITF_COUNT; i++) {
        if (out->inflight[i].active) {
            return;
        }
    }

    const gg_hid_snapshot_t release = {0};
    if (out->mouse_enabled) {
        queue_push(&out->queues[GG_HID_ITF_MOUSE], &release);
        if (out->latest.mouse_buttons != 0) {
            gg_hid_snapshot_t latest = release;
            latest.mouse_buttons = out->latest.mouse_buttons;
            queue_push(&out->queues[GG_HID_ITF_MOUSE], &latest);
        }
    }
    if (out->keyboard_enabled) {
        queue_push(&out->queues[GG_HID_ITF_KEYBOARD], &release);
        if (out->latest.kbd_modifier != 0 || !keys_equal(out->latest.kbd_keys, release.kbd_keys)) {
            gg_hid_snapshot_t latest = release;
            latest.kbd_modifier = out->latest.kbd_modifier;
            memcpy(latest.kbd_keys, out->latest.kbd_keys, 6);
            queue_push(&out->queues[GG_HID_ITF_KEYBOARD], &latest);
        }
    }
    out->recovering = false;
    out->queue_recovery_count++;
}

static void clear_session(gg_hid_output_t *out)
{
    for (int i = 0; i < GG_HID_ITF_COUNT; i++) {
        queue_clear(&out->queues[i]);
        memset(&out->inflight[i], 0, sizeof(out->inflight[i]));
    }
    gg_hid_output_clear_motion(out);
    out->recovering = false;
}

static void abort_session(gg_hid_output_t *out)
{
    out->connected = false;
    out->suspended = false;
    out->session++;
    clear_session(out);
    if (out->abort != NULL) {
        out->abort(out->ctx);
    }
}

void gg_hid_output_init(gg_hid_output_t *out, bool mouse_enabled, bool keyboard_enabled,
                        gg_hid_submit_fn submit, gg_hid_abort_fn abort, void *ctx)
{
    memset(out, 0, sizeof(*out));
    out->mouse_enabled = mouse_enabled;
    out->keyboard_enabled = keyboard_enabled;
    out->submit = submit;
    out->abort = abort;
    out->ctx = ctx;
}

void gg_hid_output_set_mouse_buttons(gg_hid_output_t *out, uint8_t buttons, uint64_t now_us)
{
    (void)now_us;
    out->latest.mouse_buttons = buttons;
    if (out->shadow.mouse_buttons == buttons) {
        return;
    }
    out->shadow.mouse_buttons = buttons;
    if (out->connected && !out->recovering && out->mouse_enabled) {
        gg_hid_snapshot_t snap = {0};
        snap.mouse_buttons = buttons;
        enqueue(out, GG_HID_ITF_MOUSE, &snap);
    }
}

void gg_hid_output_set_keyboard(gg_hid_output_t *out, uint8_t modifier,
                                const uint8_t keys[6], uint64_t now_us)
{
    (void)now_us;
    out->latest.kbd_modifier = modifier;
    memcpy(out->latest.kbd_keys, keys, 6);
    if (out->shadow.kbd_modifier == modifier && keys_equal(out->shadow.kbd_keys, keys)) {
        return;
    }
    out->shadow.kbd_modifier = modifier;
    memcpy(out->shadow.kbd_keys, keys, 6);
    if (out->connected && !out->recovering && out->keyboard_enabled) {
        gg_hid_snapshot_t snap = {0};
        snap.kbd_modifier = modifier;
        memcpy(snap.kbd_keys, keys, 6);
        enqueue(out, GG_HID_ITF_KEYBOARD, &snap);
    }
}

void gg_hid_output_add_motion(gg_hid_output_t *out, int32_t dx, int32_t dy, uint64_t now_us)
{
    if (!out->connected || out->suspended || out->recovering) {
        return;
    }
    if (dx == 0 && dy == 0) {
        return;
    }
    if (!out->motion_active) {
        out->motion_active = true;
        out->motion_start_us = now_us;
        out->motion_x = 0;
        out->motion_y = 0;
    }
    out->motion_x += dx;
    out->motion_y += dy;
    if (out->motion_x > GG_HID_MOTION_LIMIT) {
        out->motion_x = GG_HID_MOTION_LIMIT;
        out->dropped_motion++;
    } else if (out->motion_x < -GG_HID_MOTION_LIMIT) {
        out->motion_x = -GG_HID_MOTION_LIMIT;
        out->dropped_motion++;
    }
    if (out->motion_y > GG_HID_MOTION_LIMIT) {
        out->motion_y = GG_HID_MOTION_LIMIT;
        out->dropped_motion++;
    } else if (out->motion_y < -GG_HID_MOTION_LIMIT) {
        out->motion_y = -GG_HID_MOTION_LIMIT;
        out->dropped_motion++;
    }
}

void gg_hid_output_clear_motion(gg_hid_output_t *out)
{
    out->motion_x = 0;
    out->motion_y = 0;
    out->motion_active = false;
    out->motion_start_us = 0;
}

static bool submit_report(gg_hid_output_t *out, gg_hid_report_t *report)
{
    report->session = out->session;
    report->submit_id = ++out->next_submit_id;
    if (!out->submit(out->ctx, report)) {
        out->rejected_submits++;
        return false;
    }

    gg_hid_inflight_t *inflight = &out->inflight[report->itf];
    memset(inflight, 0, sizeof(*inflight));
    inflight->active = true;
    inflight->pop_on_complete = !report->motion;
    inflight->report = *report;
    return true;
}

void gg_hid_output_tick(gg_hid_output_t *out, uint64_t now_us)
{
    if (out->motion_active && (now_us - out->motion_start_us) > GG_HID_MOTION_TIMEOUT_US) {
        gg_hid_output_clear_motion(out);
        out->motion_timeouts++;
    }

    for (int i = 0; i < GG_HID_ITF_COUNT; i++) {
        if (out->inflight[i].active &&
            (now_us - out->inflight[i].submit_us) > GG_HID_INFLIGHT_TIMEOUT_US) {
            if (out->inflight[i].report.motion) {
                out->dropped_motion++;
            }
            out->inflight_timeouts++;
            abort_session(out);
            return;
        }
    }

    try_finish_recovery(out, now_us);

    if (!out->connected || out->suspended || out->recovering) {
        return;
    }

    if (out->mouse_enabled && !out->inflight[GG_HID_ITF_MOUSE].active) {
        if (out->queues[GG_HID_ITF_MOUSE].count > 0) {
            gg_hid_report_t report;
            build_mouse_report(&report, queue_head(&out->queues[GG_HID_ITF_MOUSE])->mouse_buttons, 0, 0);
            if (submit_report(out, &report)) {
                out->inflight[GG_HID_ITF_MOUSE].submit_us = now_us;
            }
        } else if (out->motion_active && (out->motion_x != 0 || out->motion_y != 0)) {
            int32_t dx = out->motion_x;
            int32_t dy = out->motion_y;
            if (dx > 127) {
                dx = 127;
            } else if (dx < -127) {
                dx = -127;
            }
            if (dy > 127) {
                dy = 127;
            } else if (dy < -127) {
                dy = -127;
            }
            gg_hid_report_t report;
            build_mouse_report(&report, out->latest.mouse_buttons, dx, dy);
            report.motion = true;
            if (submit_report(out, &report)) {
                out->inflight[GG_HID_ITF_MOUSE].submit_us = now_us;
                out->motion_x -= dx;
                out->motion_y -= dy;
                if (out->motion_x == 0 && out->motion_y == 0) {
                    gg_hid_output_clear_motion(out);
                }
            }
        }
    }

    if (out->keyboard_enabled && !out->inflight[GG_HID_ITF_KEYBOARD].active &&
        out->queues[GG_HID_ITF_KEYBOARD].count > 0) {
        const gg_hid_snapshot_t *snap = queue_head(&out->queues[GG_HID_ITF_KEYBOARD]);
        gg_hid_report_t report;
        build_keyboard_report(&report, snap->kbd_modifier, snap->kbd_keys);
        if (submit_report(out, &report)) {
            out->inflight[GG_HID_ITF_KEYBOARD].submit_us = now_us;
        }
    }
}

void gg_hid_output_on_connect(gg_hid_output_t *out, uint32_t session, uint64_t now_us)
{
    out->session = session;
    out->connected = true;
    out->suspended = false;
    clear_session(out);
    out->recovering = true;
    out->shadow = out->latest;
    try_finish_recovery(out, now_us);
}

void gg_hid_output_on_disconnect(gg_hid_output_t *out)
{
    out->connected = false;
    out->suspended = false;
    out->session++;
    clear_session(out);
    out->shadow = out->latest;
}

void gg_hid_output_on_suspend(gg_hid_output_t *out)
{
    out->connected = false;
    out->suspended = true;
    out->session++;
    clear_session(out);
    out->shadow = out->latest;
}

void gg_hid_output_on_resume(gg_hid_output_t *out, uint32_t session, uint64_t now_us)
{
    out->session = session;
    out->connected = true;
    out->suspended = false;
    clear_session(out);
    out->recovering = true;
    out->shadow = out->latest;
    try_finish_recovery(out, now_us);
}

void gg_hid_output_on_submit_complete(gg_hid_output_t *out, uint32_t session, uint8_t itf,
                                      uint32_t submit_id, uint64_t now_us)
{
    (void)now_us;
    if (itf >= GG_HID_ITF_COUNT) {
        return;
    }
    gg_hid_inflight_t *inflight = &out->inflight[itf];
    if (!inflight->active || session != inflight->report.session ||
        submit_id != inflight->report.submit_id) {
        return;
    }

    const bool pop = inflight->pop_on_complete;
    memset(inflight, 0, sizeof(*inflight));
    if (pop) {
        queue_pop(&out->queues[itf]);
    }
    out->completed_reports++;
    try_finish_recovery(out, now_us);
}

void gg_hid_output_on_submit_failed(gg_hid_output_t *out, uint32_t session, uint8_t itf,
                                    uint32_t submit_id, uint64_t now_us)
{
    (void)now_us;
    if (itf >= GG_HID_ITF_COUNT) {
        return;
    }
    gg_hid_inflight_t *inflight = &out->inflight[itf];
    if (!inflight->active || session != inflight->report.session ||
        submit_id != inflight->report.submit_id) {
        return;
    }
    if (inflight->report.motion) {
        out->dropped_motion++;
    } else {
        out->failed_reports++;
    }
    memset(inflight, 0, sizeof(*inflight));
    try_finish_recovery(out, now_us);
}
