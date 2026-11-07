#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gg_aim.h"
#include "gg_calibration.h"
#include "gg_common.h"
#include "gg_hid_output.h"

typedef enum {
    GG_STATE_BOOT = 0,
    GG_STATE_CALIBRATING,
    GG_STATE_NEEDS_CALIBRATION,
    GG_STATE_READY,
    GG_STATE_CLUTCH,
    GG_STATE_IMU_FAULT,
} gg_state_t;

typedef struct {
    bool trigger;
    bool ads;
    bool reload;
    bool weapon1;
    bool weapon2;
    bool crouch;
    bool profile;
    bool recenter;

    bool trigger_pressed;
    bool ads_pressed;
    bool reload_pressed;
    bool weapon1_pressed;
    bool weapon2_pressed;
    bool crouch_pressed;
    bool profile_pressed;
    bool recenter_pressed;

    bool trigger_released;
    bool ads_released;
    bool reload_released;
    bool weapon1_released;
    bool weapon2_released;
    bool crouch_released;
    bool profile_released;
    bool recenter_released;
} gg_gun_buttons_t;

typedef struct {
    float gains[3];
    float ads_gain;
} gg_gun_config_t;

typedef struct {
    gg_gun_config_t cfg;
    gg_state_t state;
    gg_aim_t aim;
    gg_calibration_t calibration;
    gg_hid_output_t *hid;

    bool has_bias;
    float bias_dps[3];
    int profile_index;

    bool combo_active;
    bool combo_consumed;
    bool combo_triggered;
    uint64_t combo_start_us;

    bool require_cal_success;
    bool have_sample;
    uint64_t last_sample_us;
    uint64_t last_new_sample_us;
    uint64_t last_recovery_request_us;
    bool recovery_pending;

    gg_cal_reason_t last_cal_reason;
    uint32_t imu_fault_entries;
} gg_gun_controller_t;

void gg_gun_controller_init(gg_gun_controller_t *ctl, const gg_gun_config_t *cfg,
                            gg_hid_output_t *hid);
void gg_gun_controller_start(gg_gun_controller_t *ctl, bool imu_ok, uint64_t now_us);

/*
 * Runs one 1 kHz control cycle. The controller always mirrors the physical
 * buttons to HID, then applies the motion state machine.
 */
void gg_gun_controller_tick(gg_gun_controller_t *ctl, uint64_t now_us,
                            const gg_gun_buttons_t *buttons,
                            gg_sample_status_t sample_status,
                            const gg_imu_sample_t *sample);

bool gg_gun_controller_recovery_due(gg_gun_controller_t *ctl, uint64_t now_us);
void gg_gun_controller_on_recovery_result(gg_gun_controller_t *ctl, bool ok, uint64_t now_us);

/* Clears filter history, residuals and pending motion on a USB state change. */
void gg_gun_controller_on_usb_change(gg_gun_controller_t *ctl);

gg_state_t gg_gun_controller_state(const gg_gun_controller_t *ctl);
const char *gg_state_str(gg_state_t state);
