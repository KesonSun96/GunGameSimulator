/* Full application runs with a fake HAL and no ESP-IDF/RTOS headers. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gg_app.h"

typedef struct {
    uint64_t now;
    uint32_t mask, mux_pins, sequence, session, overflows;
    bool init_ok, input_ok, imu_ok, no_sample, recovering, fail_report, bad_sample;
    int init_calls, reads, recovery_starts, reconnects, reports, logs, indicator_calls;
    gg_transport_event_t events[64];
    unsigned head, count;
    gg_hid_report_t last;
    gg_app_stats_t logged;
} fake_t;

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("APP FAIL %d: %s\n", __LINE__, #x); } } while (0)

static void push(fake_t *f, gg_transport_event_t e)
{
    if (f->count == 64) { f->overflows++; return; }
    f->events[(f->head + f->count++) % 64] = e;
}
static bool init(void *ctx)
{
    fake_t *f = ctx;
    f->init_calls++;
    if (f->init_ok) push(f, (gg_transport_event_t){.type = GG_TRANSPORT_CONNECT, .session = ++f->session});
    return f->init_ok;
}
static uint64_t now(void *ctx) { return ((fake_t *)ctx)->now; }
static bool inputs(void *ctx, uint32_t *mask)
{
    fake_t *f = ctx; *mask = f->mask;
    return f->input_ok;
}
/* Alternate hardware wiring: two logical directions on arbitrary raw bits. */
static bool mux_inputs(void *ctx, uint32_t *mask)
{
    fake_t *f = ctx; *mask = 0;
    if (f->mux_pins & (1u << 3)) *mask |= GG_INPUT_BIT(GG_INPUT_W);
    if (f->mux_pins & (1u << 21)) *mask |= GG_INPUT_BIT(GG_INPUT_A);
    return f->input_ok;
}
static bool poll(void *ctx, gg_transport_event_t *event)
{
    fake_t *f = ctx;
    if (!f->count) return false;
    *event = f->events[f->head];
    f->head = (f->head + 1) % 64; f->count--;
    return true;
}
static uint32_t overflow(void *ctx) { return ((fake_t *)ctx)->overflows; }
static bool submit(void *ctx, const gg_hid_report_t *report)
{
    fake_t *f = ctx;
    f->reports++; f->last = *report;
    push(f, (gg_transport_event_t){
        .type = f->fail_report ? GG_TRANSPORT_FAILED : GG_TRANSPORT_COMPLETE,
        .session = report->session, .submit_id = report->submit_id, .itf = report->itf,
    });
    f->fail_report = false;
    return true;
}
static void reconnect(void *ctx)
{
    fake_t *f = ctx; f->reconnects++;
    push(f, (gg_transport_event_t){.type = GG_TRANSPORT_DISCONNECT});
}
static bool imu_init(void *ctx) { return ((fake_t *)ctx)->imu_ok; }
static gg_sample_status_t imu_read(void *ctx, gg_imu_sample_t *sample)
{
    fake_t *f = ctx; f->reads++;
    if (!f->imu_ok) return GG_SAMPLE_IO_ERROR;
    if (f->no_sample) return GG_SAMPLE_NONE;
    *sample = (gg_imu_sample_t){.accel_g = {0, 0, 1}, .timestamp_us = f->now,
                              .sequence = ++f->sequence, .valid = true};
    if (f->bad_sample) sample->gyro_dps[2] = NAN;
    return GG_SAMPLE_NEW;
}
static void recover_start(void *ctx, uint64_t time)
{
    (void)time;
    fake_t *f = ctx; f->recovery_starts++; f->recovering = true;
}
static bool recover_poll(void *ctx, uint64_t time, bool *ok)
{
    (void)time;
    fake_t *f = ctx;
    if (!f->recovering) return false;
    f->recovering = false; f->imu_ok = true; f->no_sample = false; *ok = true;
    return true;
}
static void indicator(void *ctx, bool on)
{ (void)on; ((fake_t *)ctx)->indicator_calls++; }
static void log_stats(void *ctx, const gg_app_stats_t *s)
{ fake_t *f = ctx; f->logs++; f->logged = *s; }

static gg_hal_t make_hal(fake_t *f)
{
    *f = (fake_t){.now = 1000000, .init_ok = true, .input_ok = true, .imu_ok = true};
    const gg_hal_t hal = {
        .abi_version = GG_HAL_ABI_VERSION, .ctx = f,
        .init = init, .now_us = now, .read_inputs = inputs, .poll_event = poll,
        .event_overflows = overflow, .submit_report = submit, .reconnect = reconnect,
        .init_imu = imu_init, .read_imu = imu_read,
        .start_imu_recovery = recover_start, .poll_imu_recovery = recover_poll,
        .set_indicator = indicator, .log_stats = log_stats,
    };
    return hal;
}
static void step(gg_app_t *app, fake_t *f, unsigned count)
{
    while (count--) { f->now += 1000; gg_app_tick(app); }
}

static void test_validation(void)
{
    gg_app_t app;
    fake_t f;
    gg_hal_t hal = make_hal(&f);
    gg_app_config_t cfg = gg_app_default_config(GG_DEVICE_GUN);
    hal.abi_version++;
    CHECK(!gg_app_init(&app, &hal, &cfg));
    CHECK(f.init_calls == 0);
    hal.abi_version = GG_HAL_ABI_VERSION;
    cfg.gun.gains[1] = NAN;
    CHECK(!gg_app_init(&app, &hal, &cfg));
    cfg = gg_app_default_config(GG_DEVICE_GUN);
    hal.read_imu = NULL;
    CHECK(!gg_app_init(&app, &hal, &cfg));
    CHECK(f.init_calls == 0);
    cfg = gg_app_default_config(GG_DEVICE_PEDAL);
    /* Pedal needs no IMU, no scheduler, no logger and no LED. */
    hal.set_indicator = NULL; hal.log_stats = NULL;
    CHECK(gg_app_init(&app, &hal, &cfg));
    step(&app, &f, 10);
    CHECK(f.reads == 0 && app.stats.connected);
    hal = make_hal(&f); f.init_ok = false;
    CHECK(!gg_app_init(&app, &hal, &cfg));
    CHECK(!app.initialized);
    gg_app_tick(&app);
    CHECK(f.reads == 0);
}

static void test_alternate_hardware_and_failures(void)
{
    gg_app_t a, b;
    fake_t fa, fb;
    gg_hal_t ha = make_hal(&fa), hb = make_hal(&fb);
    hb.read_inputs = mux_inputs;
    const gg_app_config_t cfg = gg_app_default_config(GG_DEVICE_PEDAL);
    CHECK(gg_app_init(&a, &ha, &cfg));
    CHECK(gg_app_init(&b, &hb, &cfg));
    step(&a, &fa, 3); step(&b, &fb, 3);
    fa.mask = GG_INPUT_BIT(GG_INPUT_W) | GG_INPUT_BIT(GG_INPUT_A);
    fb.mux_pins = (1u << 3) | (1u << 21);
    fa.fail_report = true;
    step(&a, &fa, 10); step(&b, &fb, 10);
    CHECK(a.hid.failed_reports == 1);
    CHECK(fa.last.itf == GG_HID_ITF_KEYBOARD && fb.last.itf == GG_HID_ITF_KEYBOARD);
    CHECK(!memcmp(fa.last.data, fb.last.data, 8));
    CHECK(fa.last.data[2] == GG_KEY_W && fa.last.data[3] == GG_KEY_A);
    fa.input_ok = false;
    step(&a, &fa, 2);
    CHECK(fa.last.data[2] == 0 && a.stats.input_errors == 2);
    CHECK(fb.last.data[2] == GG_KEY_W); /* Instances cannot share input state. */
    fa.overflows++;
    step(&a, &fa, 1);
    CHECK(fa.reconnects == 1 && !a.hid.connected);
    step(&a, &fa, 1);
    push(&fa, (gg_transport_event_t){.type = GG_TRANSPORT_CONNECT, .session = ++fa.session});
    step(&a, &fa, 3);
    CHECK(a.hid.connected && fa.last.data[2] == 0);
}

static void test_gun_boot_recovery_and_time(void)
{
    gg_app_t app;
    fake_t f;
    gg_hal_t hal = make_hal(&f);
    f.mask = GG_INPUT_BIT(GG_INPUT_TRIGGER);
    const gg_app_config_t cfg = gg_app_default_config(GG_DEVICE_GUN);
    CHECK(gg_app_init(&app, &hal, &cfg));
    step(&app, &f, 2002);
    CHECK(app.controller.state == GG_STATE_READY);
    CHECK(app.hid.latest.mouse_buttons == 1);
    CHECK(app.stats.valid_samples == 2002);
    f.bad_sample = true;
    step(&app, &f, 1);
    CHECK(app.stats.invalid_samples == 1);
    CHECK(app.stats.valid_samples == 2002);
    f.bad_sample = false;
    f.mask |= GG_INPUT_BIT(GG_INPUT_PROFILE);
    step(&app, &f, 6);
    const int profile = app.controller.profile_index;
    f.input_ok = false;
    step(&app, &f, 1);
    CHECK(app.controller.profile_index == profile);
    CHECK(app.hid.latest.mouse_buttons == 0);
    f.input_ok = true; f.mask = 0;
    step(&app, &f, 6);
    f.no_sample = true;
    step(&app, &f, 21);
    CHECK(app.controller.state == GG_STATE_IMU_FAULT);
    f.mask = 0;
    step(&app, &f, 5);
    CHECK(app.hid.latest.mouse_buttons == 0);
    step(&app, &f, 3000);
    CHECK(f.recovery_starts == 1);
    CHECK(app.controller.state == GG_STATE_READY);
    CHECK(f.logs == 1 && f.logged.device == GG_DEVICE_GUN);
    f.now += 10000;
    gg_app_tick(&app);
    CHECK(app.stats.overruns == 1);
    CHECK(f.indicator_calls > 0);

    hal = make_hal(&f); f.imu_ok = false;
    CHECK(gg_app_init(&app, &hal, &cfg));
    CHECK(app.controller.state == GG_STATE_IMU_FAULT);
    step(&app, &f, 3002);
    CHECK(f.recovery_starts == 1 && app.controller.state == GG_STATE_READY);
}

int main(void)
{
    test_validation();
    test_alternate_hardware_and_failures();
    test_gun_boot_recovery_and_time();
    printf("Application HAL: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
