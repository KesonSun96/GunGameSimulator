#pragma once

#include "gg_hal.h"
#include "gg_debounce.h"
#include "gg_gun_controller.h"

typedef enum { GG_DEVICE_GUN, GG_DEVICE_PEDAL } gg_device_t;
typedef struct {
    gg_device_t device;
    gg_gun_config_t gun;
    uint32_t debounce_us[GG_INPUT_COUNT];
} gg_app_config_t;

typedef struct gg_app_stats {
    gg_device_t device;
    gg_state_t state;
    gg_cal_reason_t calibration_reason;
    int profile;
    bool connected;
    uint32_t valid_samples, invalid_samples, io_errors, input_errors;
    uint32_t max_sample_interval_us, overruns, max_exec_us;
    uint32_t reconnects, queue_high_water, overflows, dropped_motion, completed_reports;
} gg_app_stats_t;

/* Caller-owned storage, one instance per device. No singleton or allocator. */
typedef struct {
    gg_hal_t hal;
    gg_app_config_t config;
    gg_hid_output_t hid;
    gg_gun_controller_t controller;
    gg_button_t buttons[GG_INPUT_COUNT];
    gg_app_stats_t stats;
    bool initialized, recovering, indicator_on;
    uint32_t last_event_overflows;
    uint64_t last_tick_us, last_sample_us, last_log_us, last_indicator_us;
} gg_app_t;

gg_app_config_t gg_app_default_config(gg_device_t device);
bool gg_app_init(gg_app_t *app, const gg_hal_t *hal, const gg_app_config_t *config);
void gg_app_tick(gg_app_t *app); /* External schedulers can call this at ~1 kHz. */
void gg_app_run(gg_app_t *app); /* Optional blocking runner using HAL wait_next. */
