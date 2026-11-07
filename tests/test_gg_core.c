/*
 * Host-side tests for the pure C modules in gg_core. They drive the real
 * production sources with a controlled clock and a mock USB transport.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gg_common.h"
#include "gg_aim.h"
#include "gg_calibration.h"
#include "gg_common.h"
#include "gg_debounce.h"
#include "gg_gun_controller.h"
#include "gg_hid_output.h"
#include "pedal_keys.h"
#include "gg_usb_link.h"
#include "icm42688_decode.h"

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                            \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                      \
    do {                                                                       \
        g_checks++;                                                            \
        const double a_ = (double)(actual);                                    \
        const double e_ = (double)(expected);                                  \
        if (fabs(a_ - e_) > (tol)) {                                           \
            printf("FAIL %s:%d: %s = %.6f, expected %.6f\n", __FILE__,         \
                   __LINE__, #actual, a_, e_);                                 \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------ mock USB */

#define MOCK_MAX_RECORDS 16384

typedef struct {
    gg_hid_report_t records[MOCK_MAX_RECORDS];
    int count;
    bool accept;
    int aborts;
} mock_usb_t;

static bool mock_submit(void *ctx, const gg_hid_report_t *report)
{
    mock_usb_t *usb = ctx;
    if (usb->count < MOCK_MAX_RECORDS) {
        usb->records[usb->count++] = *report;
    }
    return usb->accept;
}

static void mock_abort(void *ctx)
{
    mock_usb_t *usb = ctx;
    usb->aborts++;
}

static void mock_init(mock_usb_t *usb)
{
    memset(usb, 0, sizeof(*usb));
    usb->accept = true;
}

static void complete_available_mock(gg_hid_output_t *out, mock_usb_t *usb, int *completed,
                                    uint64_t now_us)
{
    while (*completed < usb->count) {
        const gg_hid_report_t *report = &usb->records[(*completed)++];
        gg_hid_output_on_submit_complete(out, report->session, report->itf, report->submit_id,
                                         now_us);
    }
}

static int motion_report_count_mock(const mock_usb_t *usb, int from)
{
    int count = 0;
    for (int i = from; i < usb->count; i++) {
        if (usb->records[i].motion) {
            count++;
        }
    }
    return count;
}

static const gg_hid_report_t *last_report_for_itf(const mock_usb_t *usb, uint8_t itf)
{
    for (int i = usb->count - 1; i >= 0; i--) {
        if (usb->records[i].itf == itf) {
            return &usb->records[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ samples */

static uint32_t g_sequence;

static gg_imu_sample_t make_sample(uint64_t timestamp_us, float gx, float gy, float gz)
{
    gg_imu_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    sample.gyro_dps[0] = gx;
    sample.gyro_dps[1] = gy;
    sample.gyro_dps[2] = gz;
    sample.accel_g[2] = 1.0f;
    sample.timestamp_us = timestamp_us;
    sample.sequence = ++g_sequence;
    sample.valid = true;
    return sample;
}

static gg_gun_buttons_t no_buttons(void)
{
    gg_gun_buttons_t buttons;
    memset(&buttons, 0, sizeof(buttons));
    return buttons;
}

/* ------------------------------------------------------------------ rig */

typedef struct {
    mock_usb_t usb;
    gg_hid_output_t hid;
    gg_gun_controller_t ctl;
    int completed;
} rig_t;

static void complete_available(rig_t *rig, uint64_t now_us)
{
    while (rig->completed < rig->usb.count) {
        const gg_hid_report_t *report = &rig->usb.records[rig->completed++];
        gg_hid_output_on_submit_complete(&rig->hid, report->session, report->itf,
                                         report->submit_id, now_us);
    }
}

static void rig_init(rig_t *rig)
{
    memset(rig, 0, sizeof(*rig));
    mock_init(&rig->usb);
    gg_hid_output_init(&rig->hid, true, true, mock_submit, mock_abort, &rig->usb);
    gg_hid_output_on_connect(&rig->hid, 1, 0);
    const gg_gun_config_t cfg = {
        .gains = {0.6f, 1.0f, 1.2f},
        .ads_gain = 1.0f,
    };
    gg_gun_controller_init(&rig->ctl, &cfg, &rig->hid);
}

static void rig_tick(rig_t *rig, uint64_t now_us, const gg_gun_buttons_t *buttons,
                     gg_sample_status_t status, const gg_imu_sample_t *sample)
{
    gg_gun_controller_tick(&rig->ctl, now_us, buttons, status, sample);
    complete_available(rig, now_us);
}

/* Runs the boot calibration window with the given constant gyro input. */
static void run_boot_window(rig_t *rig, float gx, float gy, float gz)
{
    gg_gun_buttons_t buttons = no_buttons();
    for (uint64_t t = 1000; t <= GG_CAL_WINDOW_US; t += 1000) {
        gg_imu_sample_t sample = make_sample(t, gx, gy, gz);
        rig_tick(rig, t, &buttons, GG_SAMPLE_NEW, &sample);
    }
}

static int motion_report_count(const rig_t *rig, int from)
{
    int count = 0;
    for (int i = from; i < rig->usb.count; i++) {
        if (rig->usb.records[i].motion) {
            count++;
        }
    }
    return count;
}

static int32_t motion_sum_x(const rig_t *rig, int from)
{
    int32_t sum = 0;
    for (int i = from; i < rig->usb.count; i++) {
        if (rig->usb.records[i].motion) {
            sum += (int8_t)rig->usb.records[i].data[1];
        }
    }
    return sum;
}

static int button_report_count(const rig_t *rig, int from, uint8_t buttons)
{
    int count = 0;
    for (int i = from; i < rig->usb.count; i++) {
        const gg_hid_report_t *report = &rig->usb.records[i];
        if (!report->motion && report->itf == GG_HID_ITF_MOUSE &&
            report->data[0] == buttons) {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------- calibration */

static void feed_calibration(gg_calibration_t *cal, float gx, float gy, float gz,
                             uint32_t count, uint64_t step_us, uint64_t start_us)
{
    for (uint32_t i = 1; i <= count; i++) {
        gg_imu_sample_t sample = make_sample(start_us + i * step_us, gx, gy, gz);
        gg_calibration_feed(cal, &sample, sample.timestamp_us);
    }
}

static void test_calibration_success(void)
{
    gg_calibration_t cal;
    gg_calibration_start(&cal, 0);
    feed_calibration(&cal, 0.5f, -0.2f, 0.1f, 2000, 1000, 0);

    float bias[3] = {9.0f, 9.0f, 9.0f};
    CHECK(gg_calibration_poll(&cal, GG_CAL_WINDOW_US, bias) == GG_CAL_OK);
    CHECK(cal.samples == 2000);
    CHECK_NEAR(bias[0], 0.5f, 1e-4);
    CHECK_NEAR(bias[1], -0.2f, 1e-4);
    CHECK_NEAR(bias[2], 0.1f, 1e-4);
}

static void test_calibration_failure(gg_cal_reason_t expected, float gx, float gy, float gz,
                                     uint32_t count)
{
    gg_calibration_t cal;
    gg_calibration_start(&cal, 0);
    feed_calibration(&cal, gx, gy, gz, count, 1000, 0);

    float bias[3] = {11.0f, 22.0f, 33.0f};
    const gg_cal_result_t result = gg_calibration_poll(&cal, GG_CAL_WINDOW_US, bias);
    if (result != GG_CAL_FAIL || gg_calibration_reason(&cal) != expected ||
        bias[0] != 11.0f || bias[1] != 22.0f || bias[2] != 33.0f) {
        printf("cal case expected reason %d: result=%d reason=%d samples=%u bias=%.3f\n",
               (int)expected, (int)result, (int)gg_calibration_reason(&cal),
               (unsigned)cal.samples, (double)bias[0]);
    }
    CHECK(result == GG_CAL_FAIL);
    CHECK(gg_calibration_reason(&cal) == expected);
    CHECK(bias[0] == 11.0f && bias[1] == 22.0f && bias[2] == 33.0f);
}

static void test_calibration_accel_mean(void)
{
    gg_calibration_t cal;
    gg_calibration_start(&cal, 0);
    for (uint32_t i = 1; i <= 2000; i++) {
        gg_imu_sample_t sample = make_sample((uint64_t)i * 1000u, 0.0f, 0.0f, 0.0f);
        sample.accel_g[2] = 0.5f;
        gg_calibration_feed(&cal, &sample, sample.timestamp_us);
    }
    float bias[3] = {11.0f, 22.0f, 33.0f};
    CHECK(gg_calibration_poll(&cal, GG_CAL_WINDOW_US, bias) == GG_CAL_FAIL);
    CHECK(gg_calibration_reason(&cal) == GG_CAL_REASON_ACCEL_MEAN);
    CHECK(bias[0] == 11.0f);
}

static void test_calibration_reasons(void)
{
    test_calibration_failure(GG_CAL_REASON_INSUFFICIENT_SAMPLES, 0.0f, 0.0f, 0.0f, 1000);
    test_calibration_failure(GG_CAL_REASON_GYRO_MEAN, 5.0f, 0.0f, 0.0f, 2000);
    test_calibration_accel_mean();
}

static void test_calibration_gyro_std(void)
{
    gg_calibration_t cal;
    gg_calibration_start(&cal, 0);
    for (uint32_t i = 1; i <= 2000; i++) {
        const float gx = (i % 2u) ? 1.0f : -1.0f;
        gg_imu_sample_t sample = make_sample((uint64_t)i * 1000u, gx, 0.0f, 0.0f);
        gg_calibration_feed(&cal, &sample, sample.timestamp_us);
    }
    float bias[3] = {11.0f, 22.0f, 33.0f};
    CHECK(gg_calibration_poll(&cal, GG_CAL_WINDOW_US, bias) == GG_CAL_FAIL);
    CHECK(gg_calibration_reason(&cal) == GG_CAL_REASON_GYRO_STD);
    CHECK(bias[0] == 11.0f);
}

static void test_calibration_accel_std(void)
{
    gg_calibration_t cal;
    gg_calibration_start(&cal, 0);
    for (uint32_t i = 1; i <= 2000; i++) {
        gg_imu_sample_t sample = make_sample((uint64_t)i * 1000u, 0.0f, 0.0f, 0.0f);
        sample.accel_g[2] = (i % 2u) ? 0.9f : 1.1f;
        gg_calibration_feed(&cal, &sample, sample.timestamp_us);
    }
    float bias[3] = {11.0f, 22.0f, 33.0f};
    CHECK(gg_calibration_poll(&cal, GG_CAL_WINDOW_US, bias) == GG_CAL_FAIL);
    CHECK(gg_calibration_reason(&cal) == GG_CAL_REASON_ACCEL_STD);
    CHECK(bias[0] == 11.0f);
}

static void test_calibration_interval(void)
{
    gg_calibration_t cal;
    gg_calibration_start(&cal, 0);
    uint64_t timestamp = 0;
    for (uint32_t i = 1; i <= 1800; i++) {
        timestamp += (i == 900u) ? 6000u : 1000u;
        gg_imu_sample_t sample = make_sample(timestamp, 0.0f, 0.0f, 0.0f);
        gg_calibration_feed(&cal, &sample, timestamp);
    }
    CHECK(cal.samples == 1800);

    float bias[3] = {11.0f, 22.0f, 33.0f};
    CHECK(gg_calibration_poll(&cal, GG_CAL_WINDOW_US, bias) == GG_CAL_FAIL);
    CHECK(gg_calibration_reason(&cal) == GG_CAL_REASON_INTERVAL);
    CHECK(bias[0] == 11.0f);
}

static void test_calibration_duplicates(void)
{
    gg_calibration_t cal;
    gg_calibration_start(&cal, 0);
    gg_imu_sample_t sample = make_sample(1000, 0.0f, 0.0f, 0.0f);
    gg_calibration_feed(&cal, &sample, 1000);
    gg_calibration_feed(&cal, &sample, 1000);
    CHECK(cal.samples == 1);

    sample.timestamp_us = 500;
    gg_calibration_feed(&cal, &sample, 500);
    CHECK(cal.samples == 1);

    sample.timestamp_us = GG_CAL_WINDOW_US + 1;
    gg_calibration_feed(&cal, &sample, sample.timestamp_us);
    CHECK(cal.samples == 1);
}

/* -------------------------------------------------------------------- aim */

static void test_aim_first_sample_and_constant_velocity(void)
{
    gg_aim_t aim;
    gg_aim_init(&aim);
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    gg_aim_set_bias(&aim, zero);

    gg_imu_sample_t sample = make_sample(0, 0.0f, 0.0f, -100.0f);
    int16_t dx = 0;
    int16_t dy = 0;
    CHECK(!gg_aim_update(&aim, &sample, 0.001f, &dx, &dy));
    CHECK(dx == 0 && dy == 0);

    int32_t total = 0;
    for (int i = 0; i < 1000; i++) {
        sample = make_sample((uint64_t)(i + 1) * 1000u, 0.0f, 0.0f, -100.0f);
        CHECK(gg_aim_update(&aim, &sample, 0.001f, &dx, &dy));
        total += dx;
        CHECK(dy == 0);
    }
    CHECK_NEAR(total, 300.0, 1.0);
    CHECK_NEAR(aim.bias_dps[0], 0.0, 0.0);
}

static void test_aim_nonuniform_dt(void)
{
    gg_aim_t aim;
    gg_aim_init(&aim);
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    gg_aim_set_bias(&aim, zero);

    gg_imu_sample_t sample = make_sample(0, 0.0f, 0.0f, -100.0f);
    int16_t dx = 0;
    int16_t dy = 0;
    (void)gg_aim_update(&aim, &sample, 0.001f, &dx, &dy);

    int32_t total = 0;
    uint64_t timestamp = 0;
    for (int i = 0; i < 500; i++) {
        timestamp += 1000;
        sample = make_sample(timestamp, 0.0f, 0.0f, -100.0f);
        CHECK(gg_aim_update(&aim, &sample, 0.001f, &dx, &dy));
        total += dx;

        timestamp += 3000;
        sample = make_sample(timestamp, 0.0f, 0.0f, -100.0f);
        CHECK(gg_aim_update(&aim, &sample, 0.003f, &dx, &dy));
        total += dx;
    }
    CHECK_NEAR(total, 600.0, 1.0);
}

static void test_aim_rejects_zero_dt(void)
{
    gg_aim_t aim;
    gg_aim_init(&aim);
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    gg_aim_set_bias(&aim, zero);

    gg_imu_sample_t sample = make_sample(1000, 0.0f, 0.0f, -100.0f);
    int16_t dx = 0;
    int16_t dy = 0;
    (void)gg_aim_update(&aim, &sample, 0.001f, &dx, &dy);
    CHECK(!gg_aim_update(&aim, &sample, 0.0f, &dx, &dy));
    CHECK(!gg_aim_update(&aim, &sample, -0.001f, &dx, &dy));

    sample.timestamp_us = 2000;
    CHECK(gg_aim_update(&aim, &sample, 0.001f, &dx, &dy));
    CHECK(dy == 0);
}

static void test_aim_low_speed_accumulates(void)
{
    gg_aim_t aim;
    gg_aim_init(&aim);
    const float bias[3] = {0.1f, -0.1f, 0.0f};
    gg_aim_set_bias(&aim, bias);

    gg_imu_sample_t sample = make_sample(0, 0.0f, 0.0f, -0.4f);
    int16_t dx = 0;
    int16_t dy = 0;
    (void)gg_aim_update(&aim, &sample, 0.001f, &dx, &dy);

    int32_t total = 0;
    for (int i = 0; i < 5000; i++) {
        sample = make_sample((uint64_t)(i + 1) * 1000u, 0.0f, 0.0f, -0.4f);
        (void)gg_aim_update(&aim, &sample, 0.001f, &dx, &dy);
        total += dx;
    }
    CHECK(total >= 5 && total <= 6);
    CHECK_NEAR(aim.bias_dps[0], 0.1, 1e-6);
    CHECK_NEAR(aim.bias_dps[1], -0.1, 1e-6);
}

static void test_aim_deadzone(void)
{
    gg_aim_t aim;
    gg_aim_init(&aim);
    aim.deadzone_dps = 1.0f;

    gg_imu_sample_t sample = make_sample(0, 0.0f, 0.0f, -0.5f);
    int16_t dx = 0;
    int16_t dy = 0;
    (void)gg_aim_update(&aim, &sample, 0.001f, &dx, &dy);

    int32_t total = 0;
    for (int i = 0; i < 1000; i++) {
        sample = make_sample((uint64_t)(i + 1) * 1000u, 0.0f, 0.0f, -0.5f);
        (void)gg_aim_update(&aim, &sample, 0.001f, &dx, &dy);
        total += dx;
    }
    CHECK(total == 0);

    total = 0;
    for (int i = 0; i < 1000; i++) {
        sample = make_sample((uint64_t)(i + 1001) * 1000u, 0.0f, 0.0f, -3.0f);
        (void)gg_aim_update(&aim, &sample, 0.001f, &dx, &dy);
        total += dx;
    }
    CHECK(total > 0);
}

/* -------------------------------------------------------------------- HID */

static void hid_init_output(gg_hid_output_t *out, mock_usb_t *usb, bool mouse, bool keyboard)
{
    mock_init(usb);
    gg_hid_output_init(out, mouse, keyboard, mock_submit, mock_abort, usb);
    gg_hid_output_on_connect(out, 1, 0);
}

static void hid_drain(gg_hid_output_t *out, mock_usb_t *usb, int *completed)
{
    for (int pass = 0; pass < 4; pass++) {
        gg_hid_output_tick(out, (uint64_t)pass * 1000u);
        while (*completed < usb->count) {
            const gg_hid_report_t *report = &usb->records[(*completed)++];
            gg_hid_output_on_submit_complete(out, report->session, report->itf,
                                             report->submit_id, 1000);
        }
    }
}

static void test_hid_press_release_order(void)
{
    mock_usb_t usb;
    gg_hid_output_t out;
    hid_init_output(&out, &usb, true, true);
    int completed = 0;
    hid_drain(&out, &usb, &completed);
    const int base = usb.count;

    gg_hid_output_set_mouse_buttons(&out, 0x01, 2000);
    gg_hid_output_set_mouse_buttons(&out, 0x00, 2000);
    gg_hid_output_tick(&out, 2000);
    complete_available_mock(&out, &usb, &completed, 2000);

    CHECK(usb.count == base + 1);
    CHECK(usb.records[base].data[0] == 0x01);

    gg_hid_output_tick(&out, 3000);
    complete_available_mock(&out, &usb, &completed, 3000);
    CHECK(usb.count == base + 2);
    CHECK(usb.records[base + 1].data[0] == 0x00);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 0);
}

/* Helper kept local to HID tests to avoid touching rig completion order. */

static void test_hid_submit_reject_retry(void)
{
    mock_usb_t usb;
    gg_hid_output_t out;
    hid_init_output(&out, &usb, true, true);
    int completed = 0;
    hid_drain(&out, &usb, &completed);
    const int base = usb.count;

    usb.accept = false;
    gg_hid_output_set_mouse_buttons(&out, 0x01, 2000);
    gg_hid_output_set_mouse_buttons(&out, 0x00, 2000);
    gg_hid_output_tick(&out, 2000);
    gg_hid_output_tick(&out, 3000);
    CHECK(out.rejected_submits == 2);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 2);

    usb.accept = true;
    gg_hid_output_tick(&out, 4000);
    complete_available_mock(&out, &usb, &completed, 4000);
    CHECK(usb.records[usb.count - 1].data[0] == 0x01);

    gg_hid_output_tick(&out, 5000);
    complete_available_mock(&out, &usb, &completed, 5000);
    CHECK(usb.records[usb.count - 1].data[0] == 0x00);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 0);
    CHECK(usb.count > base);
}

static void test_hid_motion_not_replayed(void)
{
    mock_usb_t usb;
    gg_hid_output_t out;
    hid_init_output(&out, &usb, true, true);
    int completed = 0;
    hid_drain(&out, &usb, &completed);
    const int base = usb.count;

    gg_hid_output_add_motion(&out, 10, 5, 2000);
    gg_hid_output_tick(&out, 2000);
    CHECK(motion_report_count_mock(&usb, base) == 1);
    const gg_hid_report_t report = usb.records[usb.count - 1];
    CHECK(report.motion);
    CHECK((int8_t)report.data[1] == 10);
    CHECK((int8_t)report.data[2] == 5);

    gg_hid_output_on_submit_failed(&out, report.session, report.itf, report.submit_id, 2500);
    CHECK(out.dropped_motion == 1);
    const int after_failure = usb.count;
    gg_hid_output_tick(&out, 3000);
    CHECK(usb.count == after_failure);
    CHECK(motion_report_count_mock(&usb, base) == 1);
}

static void test_hid_fifo_overflow_recovery(void)
{
    mock_usb_t usb;
    gg_hid_output_t out;
    hid_init_output(&out, &usb, true, true);
    int completed = 0;
    hid_drain(&out, &usb, &completed);
    const uint32_t recoveries = out.queue_recovery_count;

    for (int i = 1; i <= 33; i++) {
        gg_hid_output_set_mouse_buttons(&out, (uint8_t)(i % 2), 3000);
    }
    CHECK(out.overflow_count == 1);
    CHECK(out.recovering);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 0);
    CHECK(out.queues[GG_HID_ITF_KEYBOARD].count == 0);
    CHECK(!out.motion_active);

    gg_hid_output_tick(&out, 4000);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 2);
    CHECK(last_report_for_itf(&usb, GG_HID_ITF_MOUSE)->data[0] == 0x00);
    complete_available_mock(&out, &usb, &completed, 4000);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 1);

    gg_hid_output_tick(&out, 5000);
    CHECK(last_report_for_itf(&usb, GG_HID_ITF_MOUSE)->data[0] == 0x01);
    complete_available_mock(&out, &usb, &completed, 5000);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 0);
    CHECK(out.queue_recovery_count == recoveries + 1);

    const int after_recovery = usb.count;
    gg_hid_output_set_mouse_buttons(&out, 0x00, 6000);
    gg_hid_output_tick(&out, 6000);
    CHECK(usb.count == after_recovery + 1);
    CHECK(last_report_for_itf(&usb, GG_HID_ITF_MOUSE)->data[0] == 0x00);
    complete_available_mock(&out, &usb, &completed, 6000);
}

static void test_hid_inflight_timeout(void)
{
    mock_usb_t usb;
    gg_hid_output_t out;
    hid_init_output(&out, &usb, true, true);
    int completed = 0;
    hid_drain(&out, &usb, &completed);

    gg_hid_output_set_mouse_buttons(&out, 0x01, 1000);
    gg_hid_output_tick(&out, 1000);
    const gg_hid_report_t old_report = usb.records[usb.count - 1];
    CHECK(old_report.data[0] == 0x01);

    gg_hid_output_tick(&out, 1000 + GG_HID_INFLIGHT_TIMEOUT_US + 1);
    CHECK(!out.connected);
    CHECK(usb.aborts == 1);
    CHECK(out.inflight_timeouts == 1);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 0);

    gg_hid_output_on_connect(&out, 7, 111000);
    CHECK(out.connected);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 2);

    gg_hid_output_on_submit_complete(&out, old_report.session, old_report.itf,
                                     old_report.submit_id, 111000);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 2);

    gg_hid_output_tick(&out, 112000);
    const gg_hid_report_t *mouse_release = last_report_for_itf(&usb, GG_HID_ITF_MOUSE);
    CHECK(mouse_release != NULL);
    CHECK(mouse_release->session == 7);
    CHECK(mouse_release->data[0] == 0x00);

    gg_hid_output_on_submit_complete(&out, old_report.session, old_report.itf,
                                     old_report.submit_id, 112000);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 2);

    gg_hid_output_on_submit_complete(&out, mouse_release->session, mouse_release->itf,
                                     mouse_release->submit_id, 113000);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 1);
    gg_hid_output_tick(&out, 113000);
    CHECK(last_report_for_itf(&usb, GG_HID_ITF_MOUSE)->data[0] == 0x01);
}

static void test_hid_disconnect_no_replay(void)
{
    mock_usb_t usb;
    gg_hid_output_t out;
    hid_init_output(&out, &usb, true, true);
    int completed = 0;
    hid_drain(&out, &usb, &completed);

    gg_hid_output_add_motion(&out, 50, -30, 2000);
    gg_hid_output_set_mouse_buttons(&out, 0x01, 2000);
    gg_hid_output_on_disconnect(&out);
    CHECK(!out.motion_active);
    CHECK(out.queues[GG_HID_ITF_MOUSE].count == 0);
    CHECK(out.queues[GG_HID_ITF_KEYBOARD].count == 0);
    const int base = usb.count;

    gg_hid_output_on_connect(&out, 2, 3000);
    gg_hid_output_tick(&out, 3000);
    complete_available_mock(&out, &usb, &completed, 3000);
    CHECK(motion_report_count_mock(&usb, base) == 0);
    CHECK(last_report_for_itf(&usb, GG_HID_ITF_MOUSE)->data[0] == 0x00);
    gg_hid_output_tick(&out, 4000);
    complete_available_mock(&out, &usb, &completed, 4000);
    CHECK(last_report_for_itf(&usb, GG_HID_ITF_MOUSE)->data[0] == 0x01);
    CHECK(motion_report_count_mock(&usb, base) == 0);

    gg_hid_output_on_suspend(&out);
    gg_hid_output_add_motion(&out, 5, 5, 5000);
    CHECK(!out.motion_active);
    gg_hid_output_on_resume(&out, 3, 6000);
    gg_hid_output_tick(&out, 6000);
    complete_available_mock(&out, &usb, &completed, 6000);
    CHECK(motion_report_count_mock(&usb, base) == 0);
}

static void test_hid_keyboard_report(void)
{
    mock_usb_t usb;
    gg_hid_output_t out;
    hid_init_output(&out, &usb, false, true);
    int completed = 0;
    hid_drain(&out, &usb, &completed);

    const uint8_t keys[6] = {GG_KEY_R, GG_KEY_1, 0, 0, 0, 0};
    gg_hid_output_set_keyboard(&out, GG_KEY_MOD_LEFT_CTRL, keys, 2000);
    gg_hid_output_tick(&out, 2000);
    CHECK(usb.records[usb.count - 1].itf == GG_HID_ITF_KEYBOARD);
    CHECK(usb.records[usb.count - 1].len == 8);
    CHECK(usb.records[usb.count - 1].data[0] == GG_KEY_MOD_LEFT_CTRL);
    CHECK(usb.records[usb.count - 1].data[1] == 0);
    CHECK(usb.records[usb.count - 1].data[2] == GG_KEY_R);
    CHECK(usb.records[usb.count - 1].data[3] == GG_KEY_1);
    CHECK(usb.records[usb.count - 1].data[4] == 0);
    complete_available_mock(&out, &usb, &completed, 2000);

    const int before = usb.count;
    gg_hid_output_set_keyboard(&out, GG_KEY_MOD_LEFT_CTRL, keys, 3000);
    gg_hid_output_tick(&out, 3000);
    CHECK(usb.count == before);
    CHECK(out.queues[GG_HID_ITF_KEYBOARD].count == 0);
}

static void test_pedal_combos(void)
{
    uint8_t keys[6];

    pedal_keys_build(true, false, true, false, keys);
    CHECK(keys[0] == GG_KEY_W && keys[1] == GG_KEY_A);
    CHECK(keys[2] == 0 && keys[3] == 0 && keys[4] == 0 && keys[5] == 0);

    pedal_keys_build(true, true, false, false, keys);
    CHECK(keys[0] == GG_KEY_W && keys[1] == GG_KEY_S);

    pedal_keys_build(false, false, true, true, keys);
    CHECK(keys[0] == GG_KEY_A && keys[1] == GG_KEY_D);

    pedal_keys_build(false, false, false, false, keys);
    for (int i = 0; i < 6; i++) {
        CHECK(keys[i] == 0);
    }

    pedal_keys_build(true, true, true, true, keys);
    CHECK(keys[0] == GG_KEY_W && keys[1] == GG_KEY_S && keys[2] == GG_KEY_A &&
          keys[3] == GG_KEY_D);

    mock_usb_t usb;
    gg_hid_output_t out;
    hid_init_output(&out, &usb, false, true);
    int completed = 0;
    hid_drain(&out, &usb, &completed);

    pedal_keys_build(true, true, false, false, keys);
    gg_hid_output_set_keyboard(&out, 0, keys, 2000);
    gg_hid_output_tick(&out, 2000);
    const gg_hid_report_t *report = &usb.records[usb.count - 1];
    CHECK(report->len == 8);
    CHECK(report->data[2] == GG_KEY_W);
    CHECK(report->data[3] == GG_KEY_S);
    CHECK(report->data[4] == 0);
}

/* -------------------------------------------------------------- controller */

static void test_controller_boot_success(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_CALIBRATING);

    run_boot_window(&rig, 0.2f, -0.1f, 0.05f);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);
    CHECK(rig.ctl.has_bias);
    CHECK_NEAR(rig.ctl.bias_dps[0], 0.2, 1e-4);
    CHECK_NEAR(rig.ctl.bias_dps[1], -0.1, 1e-4);
    CHECK_NEAR(rig.ctl.bias_dps[2], 0.05, 1e-4);
}

static void test_controller_boot_failure_no_motion(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    run_boot_window(&rig, 0.0f, 0.0f, 5.0f);

    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_NEEDS_CALIBRATION);
    CHECK(!rig.ctl.has_bias);
    CHECK(rig.ctl.last_cal_reason == GG_CAL_REASON_GYRO_MEAN);

    const int base = rig.usb.count;
    gg_gun_buttons_t buttons = no_buttons();
    for (uint64_t t = GG_CAL_WINDOW_US + 1000; t < GG_CAL_WINDOW_US + 101000; t += 1000) {
        gg_imu_sample_t sample = make_sample(t, 0.0f, 0.0f, -100.0f);
        rig_tick(&rig, t, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(motion_report_count(&rig, base) == 0);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_NEEDS_CALIBRATION);

    buttons.trigger = true;
    rig_tick(&rig, GG_CAL_WINDOW_US + 102000, &buttons, GG_SAMPLE_NONE, NULL);
    CHECK(button_report_count(&rig, base, 0x01) == 1);
}

static void test_controller_manual_cal_failure_keeps_bias(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    run_boot_window(&rig, 0.2f, -0.1f, 0.05f);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);
    const float old_bias[3] = {rig.ctl.bias_dps[0], rig.ctl.bias_dps[1], rig.ctl.bias_dps[2]};

    gg_gun_buttons_t buttons = no_buttons();
    buttons.recenter = true;
    buttons.profile = true;
    uint64_t now = GG_CAL_WINDOW_US;
    for (int i = 0; i < 1001; i++) {
        now += 1000;
        gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, 0.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_CALIBRATING);
    CHECK(gg_calibration_active(&rig.ctl.calibration));

    buttons = no_buttons();
    for (int i = 0; i < 2000; i++) {
        now += 1000;
        gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, 5.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);
    CHECK(rig.ctl.last_cal_reason == GG_CAL_REASON_GYRO_MEAN);
    CHECK_NEAR(rig.ctl.bias_dps[0], old_bias[0], 1e-6);
    CHECK_NEAR(rig.ctl.bias_dps[1], old_bias[1], 1e-6);
    CHECK_NEAR(rig.ctl.bias_dps[2], old_bias[2], 1e-6);
    CHECK_NEAR(rig.ctl.aim.bias_dps[2], old_bias[2], 1e-6);
}

static void test_controller_clutch(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    run_boot_window(&rig, 0.0f, 0.0f, 0.0f);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);

    gg_gun_buttons_t buttons = no_buttons();
    gg_hid_output_add_motion(&rig.hid, 20, 20, GG_CAL_WINDOW_US + 1000);
    buttons.recenter = true;
    rig_tick(&rig, GG_CAL_WINDOW_US + 1000, &buttons, GG_SAMPLE_NONE, NULL);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_CLUTCH);
    CHECK(!rig.hid.motion_active);

    const int base = rig.usb.count;
    uint64_t now = GG_CAL_WINDOW_US + 1000;
    for (int i = 0; i < 100; i++) {
        now += 1000;
        gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, -100.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(motion_report_count(&rig, base) == 0);

    buttons = no_buttons();
    now += 1000;
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NONE, NULL);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);
    CHECK(motion_report_count(&rig, base) == 0);

    const int after_release = rig.usb.count;
    for (int i = 0; i < 50; i++) {
        now += 1000;
        gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, -100.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(motion_report_count(&rig, after_release) > 0);
    CHECK(motion_sum_x(&rig, after_release) < 30);

    gg_gun_controller_on_usb_change(&rig.ctl);
    CHECK(!rig.ctl.have_sample);
    CHECK(!rig.ctl.aim.filter_init);
    CHECK(!rig.hid.motion_active);
}

static void test_controller_gesture_conflict(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    run_boot_window(&rig, 0.0f, 0.0f, 0.0f);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);
    const float old_bias = rig.ctl.bias_dps[0];

    gg_gun_buttons_t buttons = no_buttons();
    buttons.profile = true;
    rig_tick(&rig, GG_CAL_WINDOW_US + 1000, &buttons, GG_SAMPLE_NONE, NULL);
    buttons = no_buttons();
    buttons.profile_released = true;
    rig_tick(&rig, GG_CAL_WINDOW_US + 2000, &buttons, GG_SAMPLE_NONE, NULL);
    CHECK(rig.ctl.profile_index == 1);

    buttons = no_buttons();
    buttons.recenter = true;
    uint64_t now = GG_CAL_WINDOW_US + 3000;
    for (int i = 0; i < 1500; i++) {
        now += 1000;
        gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, 0.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_CLUTCH);
    CHECK(!gg_calibration_active(&rig.ctl.calibration));
    CHECK_NEAR(rig.ctl.bias_dps[0], old_bias, 1e-6);

    buttons = no_buttons();
    now += 1000;
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NONE, NULL);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);

    buttons = no_buttons();
    buttons.recenter = true;
    buttons.profile = true;
    const int profile_before = rig.ctl.profile_index;
    for (int i = 0; i < 1001; i++) {
        now += 1000;
        gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, 0.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_CALIBRATING);
    const uint64_t cal_start = rig.ctl.calibration.start_us;

    for (int i = 0; i < 2000; i++) {
        now += 1000;
        gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, 0.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(rig.ctl.calibration.start_us == cal_start);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_CLUTCH);

    /* Releasing only PROFILE must not rearm calibration while RECENTER stays held. */
    buttons.profile = false;
    buttons.profile_released = true;
    now += 1000;
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NONE, NULL);
    buttons.profile = true;
    buttons.profile_released = false;
    for (int i = 0; i < 1001; i++) {
        now += 1000;
        gg_imu_sample_t sample = make_sample(now, 0, 0, 0);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(rig.ctl.calibration.start_us == cal_start);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_CLUTCH);

    buttons.profile = false;
    buttons.profile_released = true;
    now += 1000;
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NONE, NULL);
    CHECK(rig.ctl.profile_index == profile_before);

    buttons = no_buttons();
    now += 1000;
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NONE, NULL);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);
}

static void test_controller_uniform_motion(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    run_boot_window(&rig, 0.0f, 0.0f, 0.0f);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);

    gg_gun_buttons_t buttons = no_buttons();
    const int base = rig.usb.count;
    uint64_t now = GG_CAL_WINDOW_US;
    gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, -100.0f);
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    for (int i = 0; i < 1000; i++) {
        now += 1000;
        sample = make_sample(now, 0.0f, 0.0f, -100.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK_NEAR(motion_sum_x(&rig, base), 180.0, 1.0);
}

static void test_controller_nonuniform_motion(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    run_boot_window(&rig, 0.0f, 0.0f, 0.0f);

    gg_gun_buttons_t buttons = no_buttons();
    const int base = rig.usb.count;
    uint64_t now = GG_CAL_WINDOW_US;
    gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, -100.0f);
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    for (int i = 0; i < 500; i++) {
        now += 1000;
        sample = make_sample(now, 0.0f, 0.0f, -100.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);

        now += 3000;
        sample = make_sample(now, 0.0f, 0.0f, -100.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK_NEAR(motion_sum_x(&rig, base), 360.0, 1.0);
}

static void test_controller_invalid_interval(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    run_boot_window(&rig, 0.0f, 0.0f, 0.0f);

    gg_gun_buttons_t buttons = no_buttons();
    const int base = rig.usb.count;
    uint64_t now = GG_CAL_WINDOW_US;
    gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, -100.0f);
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);

    now += 1000;
    sample = make_sample(now, 0.0f, 0.0f, -100.0f);
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);

    const int before_gap = rig.usb.count;
    now += 8000; /* 8 ms > 5 ms limit */
    sample = make_sample(now, 0.0f, 0.0f, -100.0f);
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    CHECK(motion_report_count(&rig, before_gap) == 0);

    const int after_gap = rig.usb.count;
    now += 1000;
    sample = make_sample(now, 0.0f, 0.0f, -100.0f);
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    CHECK(motion_report_count(&rig, after_gap) == 0);

    for (int i = 0; i < 100; i++) {
        now += 1000;
        sample = make_sample(now, 0.0f, 0.0f, -100.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK_NEAR(motion_sum_x(&rig, after_gap), 18.0, 1.0);
    (void)base;
}

static void test_controller_no_new_samples(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    run_boot_window(&rig, 0.0f, 0.0f, 0.0f);

    gg_gun_buttons_t buttons = no_buttons();
    const int base = rig.usb.count;
    uint64_t now = GG_CAL_WINDOW_US;
    for (int i = 0; i < 10; i++) {
        now += 1000;
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NONE, NULL);
    }
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);
    CHECK(motion_report_count(&rig, base) == 0);

    now += 1000;
    gg_imu_sample_t sample = make_sample(now, 0.0f, 0.0f, -100.0f);
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);

    for (int i = 0; i < 5; i++) {
        now += 1000;
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(motion_report_count(&rig, base) == 0);

    now += 1000;
    sample = make_sample(now, 0.0f, 0.0f, -100.0f);
    rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    CHECK(motion_report_count(&rig, base) == 0);

    for (int i = 0; i < 100; i++) {
        now += 1000;
        sample = make_sample(now, 0.0f, 0.0f, -100.0f);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(motion_sum_x(&rig, base) >= 17 && motion_sum_x(&rig, base) <= 19);
}

static void test_controller_imu_fault_and_recovery(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    run_boot_window(&rig, 0.0f, 0.0f, 0.0f);

    gg_gun_buttons_t buttons = no_buttons();
    const int base = rig.usb.count;
    const uint64_t fault_at = GG_CAL_WINDOW_US + 22000;
    gg_imu_sample_t sample = make_sample(GG_CAL_WINDOW_US, 0.0f, 0.0f, -100.0f);
    rig_tick(&rig, GG_CAL_WINDOW_US + 1000, &buttons, GG_SAMPLE_NEW, &sample);
    rig_tick(&rig, fault_at, &buttons, GG_SAMPLE_NONE, NULL);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_IMU_FAULT);
    CHECK(rig.ctl.imu_fault_entries == 1);

    buttons.trigger = true;
    rig_tick(&rig, fault_at + 1000, &buttons, GG_SAMPLE_NONE, NULL);
    CHECK(button_report_count(&rig, base, 0x01) >= 1);
    CHECK(motion_report_count(&rig, base) == 0);

    CHECK(!gg_gun_controller_recovery_due(&rig.ctl, fault_at + 999000));
    CHECK(gg_gun_controller_recovery_due(&rig.ctl, fault_at + 1000000));
    gg_gun_controller_on_recovery_result(&rig.ctl, true, fault_at + 1000000);
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_CALIBRATING);
    CHECK(rig.ctl.require_cal_success);

    const uint64_t cal_start = fault_at + 1000000;
    buttons = no_buttons();
    for (uint64_t t = cal_start + 1000; t <= cal_start + GG_CAL_WINDOW_US; t += 1000) {
        sample = make_sample(t, 0.3f, -0.2f, 0.1f);
        rig_tick(&rig, t, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(gg_gun_controller_state(&rig.ctl) == GG_STATE_READY);
    CHECK_NEAR(rig.ctl.bias_dps[0], 0.3, 1e-4);
    CHECK_NEAR(rig.ctl.bias_dps[2], 0.1, 1e-4);
}

/* ---------------------------------------------------------------- debounce */

static void test_debounce(void)
{
    gg_button_t button;
    gg_button_init(&button, true, 5000, 0);
    gg_button_update(&button, false, 1000);
    CHECK(!gg_button_pressed(&button));
    CHECK(!gg_button_just_pressed(&button));

    gg_button_update(&button, false, 6000);
    CHECK(gg_button_pressed(&button));
    CHECK(gg_button_just_pressed(&button));
    CHECK(gg_button_held_us(&button, 9000) == 3000);

    gg_button_update(&button, true, 10000);
    CHECK(gg_button_pressed(&button));
    gg_button_update(&button, true, 16000);
    CHECK(!gg_button_pressed(&button));
    CHECK(gg_button_just_released(&button));
}

/* -------------------------------------------------------------------- main */

static void test_usb_session_tokens(void)
{
    gg_usb_link_t link = {0};
    gg_hid_report_t report = {.itf = GG_HID_ITF_KEYBOARD, .len = 8, .submit_id = 42};
    gg_hid_report_t done;
    CHECK(!gg_usb_link_begin(&link, &report, 1000));
    gg_usb_link_connect(&link);
    report.session = link.session;
    CHECK(gg_usb_link_begin(&link, &report, 1000));
    CHECK(!gg_usb_link_begin(&link, &report, 2000));
    gg_usb_link_suspend(&link);
    CHECK(!gg_usb_link_begin(&link, &report, 3000));
    gg_usb_link_resume(&link);
    const uint32_t old_session = report.session;
    report.session = link.session;
    report.submit_id = 43;
    CHECK(!gg_usb_link_begin(&link, &report, 4000));
    CHECK(gg_usb_link_finish(&link, GG_HID_ITF_KEYBOARD, &done));
    CHECK(done.session == old_session && done.submit_id == 42);
    CHECK(!gg_usb_link_finish(&link, GG_HID_ITF_KEYBOARD, &done));
    CHECK(gg_usb_link_begin(&link, &report, 5000));
    CHECK(!gg_usb_link_expired(&link, 5000 + GG_HID_INFLIGHT_TIMEOUT_US));
    CHECK(gg_usb_link_expired(&link, 5001 + GG_HID_INFLIGHT_TIMEOUT_US));
    gg_usb_link_disconnect(&link);
    CHECK(!gg_usb_link_finish(&link, GG_HID_ITF_KEYBOARD, &done));
    gg_usb_link_connect(&link);
    CHECK(!gg_usb_link_begin(&link, &report, 6000));
}

static void test_sensor_payload_validation(void)
{
    uint8_t bytes[12] = {0};
    int16_t gyro[3], accel[3];
    CHECK(!icm42688_decode(bytes, gyro, accel));
    bytes[4] = 0x10; /* 1g at configured +/-8g full-scale. */
    CHECK(icm42688_decode(bytes, gyro, accel));
    CHECK(accel[2] == 4096 && gyro[0] == 0);
    for (int axis = 0; axis < 6; axis++) {
        bytes[axis * 2] = 0x80;
        bytes[axis * 2 + 1] = 0;
        CHECK(!icm42688_decode(bytes, gyro, accel));
        bytes[axis * 2 + 1] = 1;
        CHECK(!icm42688_decode(bytes, gyro, accel));
        bytes[axis * 2 + 1] = 2;
        CHECK(icm42688_decode(bytes, gyro, accel));
        bytes[axis * 2] = 0;
        bytes[axis * 2 + 1] = 0;
        bytes[4] = 0x10;
    }
}

static void test_combo_suppresses_motion(void)
{
    rig_t rig;
    rig_init(&rig);
    gg_gun_controller_start(&rig.ctl, true, 0);
    run_boot_window(&rig, 0, 0, 0);
    gg_gun_buttons_t buttons = no_buttons();
    buttons.recenter = buttons.profile = true;
    const int base = rig.usb.count;
    uint64_t now = GG_CAL_WINDOW_US;
    for (int i = 0; i < 1001; i++) {
        now += 1000;
        gg_imu_sample_t sample = make_sample(now, 0, 0, -150);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(motion_report_count(&rig, base) == 0);
    CHECK(rig.ctl.state == GG_STATE_CALIBRATING);
    /* A failed manual window with RECENTER held must return to CLUTCH. */
    for (int i = 0; i < 2000; i++) {
        now += 1000;
        gg_imu_sample_t sample = make_sample(now, 0, 0, -150);
        rig_tick(&rig, now, &buttons, GG_SAMPLE_NEW, &sample);
    }
    CHECK(rig.ctl.state == GG_STATE_CLUTCH);
    CHECK(motion_report_count(&rig, base) == 0);
}

int main(void)
{
    test_usb_session_tokens();
    test_sensor_payload_validation();
    test_combo_suppresses_motion();
    test_debounce();

    test_calibration_success();
    test_calibration_gyro_std();
    test_calibration_accel_std();
    test_calibration_interval();
    test_calibration_duplicates();
    test_calibration_reasons();

    test_aim_first_sample_and_constant_velocity();
    test_aim_nonuniform_dt();
    test_aim_rejects_zero_dt();
    test_aim_low_speed_accumulates();
    test_aim_deadzone();

    test_hid_press_release_order();
    test_hid_submit_reject_retry();
    test_hid_motion_not_replayed();
    test_hid_fifo_overflow_recovery();
    test_hid_inflight_timeout();
    test_hid_disconnect_no_replay();
    test_hid_keyboard_report();
    test_pedal_combos();

    test_controller_boot_success();
    test_controller_boot_failure_no_motion();
    test_controller_manual_cal_failure_keeps_bias();
    test_controller_clutch();
    test_controller_gesture_conflict();
    test_controller_uniform_motion();
    test_controller_nonuniform_motion();
    test_controller_invalid_interval();
    test_controller_no_new_samples();
    test_controller_imu_fault_and_recovery();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
