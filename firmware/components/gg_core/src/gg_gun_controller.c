#include "gg_gun_controller.h"

#include <string.h>

static const float k_profile_gains[3] = {0.6f, 1.0f, 1.2f};

static void reset_motion(gg_gun_controller_t *ctl)
{
    gg_aim_reset_motion(&ctl->aim);
    gg_hid_output_clear_motion(ctl->hid);
    ctl->have_sample = false;
}

static void start_calibration(gg_gun_controller_t *ctl, uint64_t now_us, bool require_success)
{
    if (ctl->state == GG_STATE_CALIBRATING || ctl->state == GG_STATE_IMU_FAULT) {
        return;
    }
    ctl->state = GG_STATE_CALIBRATING;
    ctl->require_cal_success = require_success;
    ctl->last_cal_reason = GG_CAL_REASON_NONE;
    gg_calibration_start(&ctl->calibration, now_us);
    reset_motion(ctl);
}

static void mirror_buttons(gg_gun_controller_t *ctl, const gg_gun_buttons_t *buttons, uint64_t now_us)
{
    uint8_t mouse_buttons = 0;
    if (buttons->trigger) {
        mouse_buttons |= 0x01;
    }
    if (buttons->ads) {
        mouse_buttons |= 0x02;
    }
    gg_hid_output_set_mouse_buttons(ctl->hid, mouse_buttons, now_us);

    uint8_t modifier = buttons->crouch ? GG_KEY_MOD_LEFT_CTRL : 0;
    uint8_t keys[6] = {0};
    int n = 0;
    if (buttons->reload) {
        keys[n++] = GG_KEY_R;
    }
    if (buttons->weapon1 && n < 6) {
        keys[n++] = GG_KEY_1;
    }
    if (buttons->weapon2 && n < 6) {
        keys[n++] = GG_KEY_2;
    }
    gg_hid_output_set_keyboard(ctl->hid, modifier, keys, now_us);
}

static void handle_gestures(gg_gun_controller_t *ctl, const gg_gun_buttons_t *buttons, uint64_t now_us)
{
    const bool both = buttons->recenter && buttons->profile;

    if (both) {
        if (!ctl->combo_active) {
            ctl->combo_active = true;
            ctl->combo_start_us = now_us;
        }
        ctl->combo_consumed = true;
        if (!ctl->combo_triggered &&
            (now_us - ctl->combo_start_us) >= gg_ms_to_us(1000) &&
            ctl->state != GG_STATE_CALIBRATING &&
            ctl->state != GG_STATE_IMU_FAULT) {
            start_calibration(ctl, now_us, false);
            ctl->combo_triggered = true;
        }
    } else {
        if (ctl->combo_active) {
            ctl->combo_active = false;
        }
        if (buttons->profile_released && !ctl->combo_consumed &&
            ctl->state != GG_STATE_CALIBRATING && ctl->state != GG_STATE_IMU_FAULT) {
            ctl->profile_index = (ctl->profile_index + 1) % 3;
            gg_aim_set_profile_gain(&ctl->aim, ctl->cfg.gains[ctl->profile_index]);
        }
        if (!buttons->recenter && !buttons->profile) {
            ctl->combo_consumed = false;
            ctl->combo_triggered = false;
        }
    }
}

static void poll_calibration(gg_gun_controller_t *ctl, const gg_gun_buttons_t *buttons, uint64_t now_us)
{
    if (ctl->state != GG_STATE_CALIBRATING) {
        return;
    }

    float candidate[3] = {0};
    const gg_cal_result_t result = gg_calibration_poll(&ctl->calibration, now_us, candidate);
    if (result == GG_CAL_RUNNING) {
        return;
    }

    ctl->last_cal_reason = gg_calibration_reason(&ctl->calibration);
    if (result == GG_CAL_OK) {
        ctl->has_bias = true;
        memcpy(ctl->bias_dps, candidate, sizeof(ctl->bias_dps));
        gg_aim_set_bias(&ctl->aim, candidate);
        ctl->require_cal_success = false;
        reset_motion(ctl);
        ctl->state = buttons->recenter ? GG_STATE_CLUTCH : GG_STATE_READY;
        return;
    }

    reset_motion(ctl);
    if (ctl->has_bias && !ctl->require_cal_success) {
        ctl->state = buttons->recenter ? GG_STATE_CLUTCH : GG_STATE_READY;
    } else {
        ctl->state = GG_STATE_NEEDS_CALIBRATION;
    }
}

static void process_sample(gg_gun_controller_t *ctl, uint64_t now_us,
                           gg_sample_status_t status, const gg_imu_sample_t *sample)
{
    if (status != GG_SAMPLE_NEW || sample == NULL || !sample->valid) {
        return;
    }
    ctl->last_new_sample_us = now_us;

    if (ctl->state == GG_STATE_CALIBRATING) {
        gg_calibration_feed(&ctl->calibration, sample, now_us);
        return;
    }
    if (ctl->state != GG_STATE_READY) {
        return;
    }

    if (!ctl->have_sample) {
        ctl->have_sample = true;
        ctl->last_sample_us = sample->timestamp_us;
        int16_t dx = 0;
        int16_t dy = 0;
        (void)gg_aim_update(&ctl->aim, sample, 0.0f, &dx, &dy);
        return;
    }

    if (sample->timestamp_us <= ctl->last_sample_us) {
        reset_motion(ctl);
        ctl->last_sample_us = sample->timestamp_us;
        ctl->have_sample = true;
        return;
    }

    const uint64_t dt_us = sample->timestamp_us - ctl->last_sample_us;
    ctl->last_sample_us = sample->timestamp_us;
    if (dt_us > GG_CAL_MAX_INTERVAL_US) {
        reset_motion(ctl);
        ctl->have_sample = true;
        return;
    }

    int16_t dx = 0;
    int16_t dy = 0;
    if (gg_aim_update(&ctl->aim, sample, (float)dt_us / 1000000.0f, &dx, &dy)) {
        gg_hid_output_add_motion(ctl->hid, dx, dy, now_us);
    }
}

void gg_gun_controller_init(gg_gun_controller_t *ctl, const gg_gun_config_t *cfg,
                            gg_hid_output_t *hid)
{
    memset(ctl, 0, sizeof(*ctl));
    ctl->cfg = *cfg;
    if (ctl->cfg.gains[0] == 0.0f) {
        memcpy(ctl->cfg.gains, k_profile_gains, sizeof(k_profile_gains));
    }
    if (ctl->cfg.ads_gain <= 0.0f) {
        ctl->cfg.ads_gain = 1.0f;
    }
    ctl->hid = hid;
    ctl->state = GG_STATE_BOOT;
    gg_aim_init(&ctl->aim);
    gg_aim_set_profile_gain(&ctl->aim, ctl->cfg.gains[0]);
    ctl->profile_index = 0;
}

void gg_gun_controller_start(gg_gun_controller_t *ctl, bool imu_ok, uint64_t now_us)
{
    ctl->last_new_sample_us = now_us;
    ctl->last_recovery_request_us = now_us;
    if (imu_ok) {
        start_calibration(ctl, now_us, true);
    } else {
        ctl->state = GG_STATE_IMU_FAULT;
        ctl->imu_fault_entries++;
        reset_motion(ctl);
    }
}

bool gg_gun_controller_recovery_due(gg_gun_controller_t *ctl, uint64_t now_us)
{
    if (ctl->state != GG_STATE_IMU_FAULT || ctl->recovery_pending) {
        return false;
    }
    if ((now_us - ctl->last_recovery_request_us) < gg_ms_to_us(1000)) {
        return false;
    }
    ctl->last_recovery_request_us = now_us;
    ctl->recovery_pending = true;
    return true;
}

void gg_gun_controller_on_recovery_result(gg_gun_controller_t *ctl, bool ok, uint64_t now_us)
{
    ctl->recovery_pending = false;
    if (!ok || ctl->state != GG_STATE_IMU_FAULT) {
        return;
    }
    ctl->state = GG_STATE_CALIBRATING;
    ctl->require_cal_success = true;
    ctl->last_cal_reason = GG_CAL_REASON_NONE;
    ctl->last_new_sample_us = now_us;
    gg_calibration_start(&ctl->calibration, now_us);
    reset_motion(ctl);
}

void gg_gun_controller_on_usb_change(gg_gun_controller_t *ctl)
{
    reset_motion(ctl);
}

void gg_gun_controller_tick(gg_gun_controller_t *ctl, uint64_t now_us,
                            const gg_gun_buttons_t *buttons,
                            gg_sample_status_t sample_status,
                            const gg_imu_sample_t *sample)
{
    mirror_buttons(ctl, buttons, now_us);
    gg_aim_set_ads(&ctl->aim, buttons->ads, ctl->cfg.ads_gain);

    handle_gestures(ctl, buttons, now_us);

    if (sample_status == GG_SAMPLE_NEW && sample != NULL && sample->valid) {
        ctl->last_new_sample_us = now_us;
    }

    if (ctl->state != GG_STATE_IMU_FAULT &&
        (now_us - ctl->last_new_sample_us) > gg_ms_to_us(20)) {
        ctl->state = GG_STATE_IMU_FAULT;
        ctl->imu_fault_entries++;
        ctl->recovery_pending = false;
        ctl->last_recovery_request_us = now_us;
        reset_motion(ctl);
    }

    if (ctl->state == GG_STATE_CALIBRATING) {
        process_sample(ctl, now_us, sample_status, sample);
        poll_calibration(ctl, buttons, now_us);
    } else if (ctl->state == GG_STATE_READY) {
        if (buttons->recenter) {
            ctl->state = GG_STATE_CLUTCH;
            reset_motion(ctl);
        } else {
            process_sample(ctl, now_us, sample_status, sample);
        }
    } else if (ctl->state == GG_STATE_CLUTCH) {
        if (!buttons->recenter) {
            ctl->state = GG_STATE_READY;
            reset_motion(ctl);
        }
    } else if (ctl->state == GG_STATE_NEEDS_CALIBRATION) {
        reset_motion(ctl);
    } else if (ctl->state == GG_STATE_IMU_FAULT) {
        reset_motion(ctl);
    }

    gg_hid_output_tick(ctl->hid, now_us);
}

gg_state_t gg_gun_controller_state(const gg_gun_controller_t *ctl)
{
    return ctl->state;
}

const char *gg_state_str(gg_state_t state)
{
    switch (state) {
    case GG_STATE_BOOT:
        return "BOOT";
    case GG_STATE_CALIBRATING:
        return "CALIBRATING";
    case GG_STATE_NEEDS_CALIBRATION:
        return "NEEDS_CALIBRATION";
    case GG_STATE_READY:
        return "READY";
    case GG_STATE_CLUTCH:
        return "CLUTCH";
    case GG_STATE_IMU_FAULT:
        return "IMU_FAULT";
    default:
        return "UNKNOWN";
    }
}
