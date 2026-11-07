#include "gg_app.h"

#include <math.h>
#include <string.h>
#include "pedal_keys.h"

static bool submit(void *ctx, const gg_hid_report_t *report)
{
    gg_app_t *app = ctx;
    return app->hal.submit_report(app->hal.ctx, report);
}

static void reconnect(void *ctx)
{
    gg_app_t *app = ctx;
    app->hal.reconnect(app->hal.ctx);
}

gg_app_config_t gg_app_default_config(gg_device_t device)
{
    gg_app_config_t cfg = {.device = device, .gun = {{0.6f, 1.0f, 1.2f}, 1.0f}};
    for (int i = 0; i < GG_INPUT_COUNT; i++) cfg.debounce_us[i] = 5000;
    cfg.debounce_us[GG_INPUT_TRIGGER] = 1500;
    cfg.debounce_us[GG_INPUT_ADS] = 3000;
    return cfg;
}

static bool valid_config(const gg_hal_t *hal, const gg_app_config_t *cfg)
{
    if (!hal || !cfg || hal->abi_version != GG_HAL_ABI_VERSION ||
        (cfg->device != GG_DEVICE_GUN && cfg->device != GG_DEVICE_PEDAL) ||
        !hal->init || !hal->now_us || !hal->read_inputs || !hal->poll_event ||
        !hal->event_overflows || !hal->submit_report || !hal->reconnect) return false;
    if (cfg->device == GG_DEVICE_GUN) {
        if (!hal->init_imu || !hal->read_imu || !hal->start_imu_recovery || !hal->poll_imu_recovery ||
            !isfinite(cfg->gun.ads_gain) || cfg->gun.ads_gain <= 0) return false;
        for (int i = 0; i < 3; i++) {
            if (!isfinite(cfg->gun.gains[i]) || cfg->gun.gains[i] <= 0) return false;
        }
    }
    for (int i = 0; i < GG_INPUT_COUNT; i++) {
        if (cfg->debounce_us[i] > 1000000u) return false;
    }
    return true;
}

bool gg_app_init(gg_app_t *app, const gg_hal_t *hal, const gg_app_config_t *config)
{
    if (!app || !valid_config(hal, config)) return false;
    memset(app, 0, sizeof(*app));
    app->hal = *hal;
    app->config = *config;
    app->stats.device = config->device;
    if (!app->hal.init(app->hal.ctx)) return false;
    uint32_t mask = 0;
    if (!app->hal.read_inputs(app->hal.ctx, &mask)) {
        app->stats.input_errors++;
        mask = 0;
    }
    const uint64_t now = app->hal.now_us(app->hal.ctx);
    for (int i = 0; i < GG_INPUT_COUNT; i++) {
        gg_button_init(&app->buttons[i], !(mask & GG_INPUT_BIT(i)), config->debounce_us[i], now);
    }
    const bool gun = config->device == GG_DEVICE_GUN;
    gg_hid_output_init(&app->hid, gun, true, submit, reconnect, app);
    if (gun) {
        gg_gun_controller_init(&app->controller, &config->gun, &app->hid);
        const bool ok = app->hal.init_imu(app->hal.ctx);
        gg_gun_controller_start(&app->controller, ok, app->hal.now_us(app->hal.ctx));
    }
    app->last_tick_us = app->last_log_us = app->last_indicator_us = app->hal.now_us(app->hal.ctx);
    app->initialized = true;
    return true;
}

static void transport_events(gg_app_t *app, uint64_t now)
{
    gg_transport_event_t event;
    /* Bound work even if an adapter keeps producing events. */
    for (int i = 0; i < 32 && app->hal.poll_event(app->hal.ctx, &event); i++) {
        bool changed = true;
        switch (event.type) {
        case GG_TRANSPORT_CONNECT:
            app->stats.connected = true;
            app->stats.reconnects++;
            gg_hid_output_on_connect(&app->hid, event.session, now);
            break;
        case GG_TRANSPORT_RESUME:
            app->stats.connected = true;
            app->stats.reconnects++;
            gg_hid_output_on_resume(&app->hid, event.session, now);
            break;
        case GG_TRANSPORT_DISCONNECT:
            app->stats.connected = false;
            gg_hid_output_on_disconnect(&app->hid);
            break;
        case GG_TRANSPORT_SUSPEND:
            app->stats.connected = false;
            gg_hid_output_on_suspend(&app->hid);
            break;
        case GG_TRANSPORT_COMPLETE:
            gg_hid_output_on_submit_complete(&app->hid, event.session, event.itf, event.submit_id, now);
            changed = false;
            break;
        case GG_TRANSPORT_FAILED:
            gg_hid_output_on_submit_failed(&app->hid, event.session, event.itf, event.submit_id, now);
            changed = false;
            break;
        default:
            changed = false;
            break;
        }
        if (changed && app->config.device == GG_DEVICE_GUN) gg_gun_controller_on_usb_change(&app->controller);
    }
    const uint32_t overflow = app->hal.event_overflows(app->hal.ctx);
    if (overflow != app->last_event_overflows) {
        app->last_event_overflows = overflow;
        app->stats.connected = false;
        gg_hid_output_on_disconnect(&app->hid);
        if (app->config.device == GG_DEVICE_GUN) gg_gun_controller_on_usb_change(&app->controller);
        reconnect(app);
    }
}

static void scan_inputs(gg_app_t *app, uint64_t now)
{
    uint32_t mask = 0;
    const bool ok = app->hal.read_inputs(app->hal.ctx, &mask);
    if (!ok) app->stats.input_errors++;
    for (int i = 0; i < GG_INPUT_COUNT; i++) {
        if (!ok) {
            /* Failed input hardware must not leave a held key stuck. */
            gg_button_init(&app->buttons[i], true, app->config.debounce_us[i], now);
            /* Levels release host keys; do not synthesize PROFILE gestures. */
        } else {
            gg_button_update(&app->buttons[i], !(mask & GG_INPUT_BIT(i)), now);
        }
    }
}

static void gun_tick(gg_app_t *app, uint64_t now)
{
    gg_gun_buttons_t buttons = {0};
#define BUTTON(field, id) \
    buttons.field = gg_button_pressed(&app->buttons[id]); \
    buttons.field##_pressed = gg_button_just_pressed(&app->buttons[id]); \
    buttons.field##_released = gg_button_just_released(&app->buttons[id])
    BUTTON(trigger, GG_INPUT_TRIGGER);
    BUTTON(ads, GG_INPUT_ADS);
    BUTTON(reload, GG_INPUT_RELOAD);
    BUTTON(weapon1, GG_INPUT_WEAPON1);
    BUTTON(weapon2, GG_INPUT_WEAPON2);
    BUTTON(crouch, GG_INPUT_CROUCH);
    BUTTON(profile, GG_INPUT_PROFILE);
    BUTTON(recenter, GG_INPUT_RECENTER);
#undef BUTTON
    gg_imu_sample_t sample = {0};
    gg_sample_status_t status = app->hal.read_imu(app->hal.ctx, &sample);
    if (status == GG_SAMPLE_NEW) {
        for (int i = 0; i < GG_IMU_AXIS_COUNT; i++) {
            if (!isfinite(sample.gyro_dps[i]) || !isfinite(sample.accel_g[i])) {
                sample.valid = false;
                status = GG_SAMPLE_INVALID;
                break;
            }
        }
    }
    if (status == GG_SAMPLE_NEW && sample.valid) {
        app->stats.valid_samples++;
        if (app->last_sample_us && sample.timestamp_us > app->last_sample_us) {
            const uint64_t interval = sample.timestamp_us - app->last_sample_us;
            if (interval > app->stats.max_sample_interval_us)
                app->stats.max_sample_interval_us = interval > UINT32_MAX ? UINT32_MAX : (uint32_t)interval;
        }
        app->last_sample_us = sample.timestamp_us;
    } else if (status == GG_SAMPLE_INVALID || status == GG_SAMPLE_NEW) {
        app->stats.invalid_samples++;
    } else if (status == GG_SAMPLE_IO_ERROR) {
        app->stats.io_errors++;
    }
    if (!app->recovering && gg_gun_controller_recovery_due(&app->controller, now)) {
        app->hal.start_imu_recovery(app->hal.ctx, now);
        app->recovering = true;
    }
    if (app->recovering) {
        bool ok = false;
        if (app->hal.poll_imu_recovery(app->hal.ctx, now, &ok)) {
            app->recovering = false;
            app->last_sample_us = 0;
            gg_gun_controller_on_recovery_result(&app->controller, ok, now);
        }
    }
    gg_gun_controller_tick(&app->controller, now, &buttons, status, &sample);
}

static void indicator(gg_app_t *app, uint64_t now)
{
    uint32_t period = app->stats.connected ? 0 : 1000;
    if (app->config.device == GG_DEVICE_GUN) {
        switch (app->controller.state) {
        case GG_STATE_IMU_FAULT: period = 100; break;
        case GG_STATE_NEEDS_CALIBRATION: period = 250; break;
        case GG_STATE_CALIBRATING: period = 500; break;
        default: break;
        }
    }
    const bool old = app->indicator_on;
    if (!period) app->indicator_on = true;
    else if (now - app->last_indicator_us >= gg_ms_to_us(period)) {
        app->last_indicator_us = now;
        app->indicator_on = !app->indicator_on;
    }
    if (old != app->indicator_on && app->hal.set_indicator)
        app->hal.set_indicator(app->hal.ctx, app->indicator_on);
}

void gg_app_tick(gg_app_t *app)
{
    if (!app || !app->initialized) return;
    const uint64_t now = app->hal.now_us(app->hal.ctx);
    if (now - app->last_tick_us > 2000) app->stats.overruns++;
    app->last_tick_us = now;
    transport_events(app, now);
    scan_inputs(app, now);
    if (app->config.device == GG_DEVICE_GUN) gun_tick(app, now);
    else {
        uint8_t keys[6];
        pedal_keys_build(gg_button_pressed(&app->buttons[GG_INPUT_W]),
                         gg_button_pressed(&app->buttons[GG_INPUT_S]),
                         gg_button_pressed(&app->buttons[GG_INPUT_A]),
                         gg_button_pressed(&app->buttons[GG_INPUT_D]), keys);
        gg_hid_output_set_keyboard(&app->hid, 0, keys, now);
        gg_hid_output_tick(&app->hid, now);
    }
    app->stats.connected = app->hid.connected && !app->hid.suspended;
    app->stats.state = app->config.device == GG_DEVICE_GUN ? app->controller.state : GG_STATE_READY;
    app->stats.calibration_reason = app->controller.last_cal_reason;
    app->stats.profile = app->controller.profile_index;
    const uint32_t q0 = app->hid.queues[0].high_water, q1 = app->hid.queues[1].high_water;
    app->stats.queue_high_water = q0 > q1 ? q0 : q1;
    app->stats.overflows = app->hid.overflow_count + app->last_event_overflows;
    app->stats.dropped_motion = app->hid.dropped_motion + app->hid.motion_timeouts;
    app->stats.completed_reports = app->hid.completed_reports;
    indicator(app, now);
    const uint64_t exec = app->hal.now_us(app->hal.ctx) - now;
    if (exec > app->stats.max_exec_us) app->stats.max_exec_us = exec > UINT32_MAX ? UINT32_MAX : (uint32_t)exec;
    if (now - app->last_log_us >= gg_ms_to_us(5000)) {
        app->last_log_us = now;
        if (app->hal.log_stats) app->hal.log_stats(app->hal.ctx, &app->stats);
        app->stats.max_exec_us = app->stats.max_sample_interval_us = 0;
    }
}

void gg_app_run(gg_app_t *app)
{
    if (!app || !app->initialized || !app->hal.wait_next) return;
    while (true) {
        app->hal.wait_next(app->hal.ctx, 1000);
        gg_app_tick(app);
    }
}
