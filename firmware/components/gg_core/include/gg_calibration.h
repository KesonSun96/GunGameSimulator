#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gg_common.h"

#define GG_CAL_WINDOW_US 2000000u
#define GG_CAL_MIN_SAMPLES 1800u
#define GG_CAL_MAX_INTERVAL_US 5000u
#define GG_CAL_GYRO_MEAN_LIMIT_DPS 3.0f
#define GG_CAL_GYRO_STD_LIMIT_DPS 0.3f
#define GG_CAL_ACCEL_MEAN_MIN_G 0.9f
#define GG_CAL_ACCEL_MEAN_MAX_G 1.1f
#define GG_CAL_ACCEL_STD_LIMIT_G 0.02f

typedef enum {
    GG_CAL_RUNNING = 0,
    GG_CAL_OK,
    GG_CAL_FAIL,
} gg_cal_result_t;

typedef enum {
    GG_CAL_REASON_NONE = 0,
    GG_CAL_REASON_INSUFFICIENT_SAMPLES,
    GG_CAL_REASON_INTERVAL,
    GG_CAL_REASON_GYRO_MEAN,
    GG_CAL_REASON_GYRO_STD,
    GG_CAL_REASON_ACCEL_MEAN,
    GG_CAL_REASON_ACCEL_STD,
} gg_cal_reason_t;

typedef struct {
    bool active;
    uint64_t start_us;
    uint64_t end_us;
    uint64_t last_sample_us;
    uint64_t max_interval_us;
    uint32_t samples;
    uint32_t interval_violations;

    double gyro_mean[3];
    double gyro_m2[3];
    double accel_mean;
    double accel_m2;
    uint32_t accel_samples;
    gg_cal_reason_t reason;
} gg_calibration_t;

void gg_calibration_start(gg_calibration_t *cal, uint64_t now_us);
bool gg_calibration_active(const gg_calibration_t *cal);

/* Only valid NEW samples may be fed. */
void gg_calibration_feed(gg_calibration_t *cal, const gg_imu_sample_t *sample, uint64_t now_us);

/*
 * Evaluates the window when 2 s have elapsed. On GG_CAL_OK the candidate bias
 * is written to out_bias; on failure out_bias is untouched.
 */
gg_cal_result_t gg_calibration_poll(gg_calibration_t *cal, uint64_t now_us, float out_bias[3]);

gg_cal_reason_t gg_calibration_reason(const gg_calibration_t *cal);
const char *gg_calibration_reason_str(gg_cal_reason_t reason);
