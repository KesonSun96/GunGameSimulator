#include "gg_calibration.h"

#include <math.h>
#include <string.h>

static void welford_add(double *mean, double *m2, uint32_t n, double value)
{
    const double delta = value - *mean;
    *mean += delta / (double)n;
    *m2 += delta * (value - *mean);
}

void gg_calibration_start(gg_calibration_t *cal, uint64_t now_us)
{
    memset(cal, 0, sizeof(*cal));
    cal->active = true;
    cal->start_us = now_us;
    cal->end_us = now_us + GG_CAL_WINDOW_US;
    cal->reason = GG_CAL_REASON_NONE;
}

bool gg_calibration_active(const gg_calibration_t *cal)
{
    return cal->active;
}

void gg_calibration_feed(gg_calibration_t *cal, const gg_imu_sample_t *sample, uint64_t now_us)
{
    if (!cal->active || sample == NULL || !sample->valid) {
        return;
    }
    if (sample->timestamp_us < cal->start_us || sample->timestamp_us > cal->end_us) {
        return;
    }
    if (cal->last_sample_us != 0) {
        if (sample->timestamp_us <= cal->last_sample_us) {
            return;
        }
        const uint64_t interval = sample->timestamp_us - cal->last_sample_us;
        if (interval > cal->max_interval_us) {
            cal->max_interval_us = interval;
        }
        if (interval > GG_CAL_MAX_INTERVAL_US) {
            cal->interval_violations++;
        }
    }

    cal->last_sample_us = sample->timestamp_us;
    cal->samples++;

    for (int axis = 0; axis < 3; axis++) {
        welford_add(&cal->gyro_mean[axis], &cal->gyro_m2[axis], cal->samples,
                    sample->gyro_dps[axis]);
    }

    const double accel_norm = sqrt((double)sample->accel_g[0] * sample->accel_g[0] +
                                   (double)sample->accel_g[1] * sample->accel_g[1] +
                                   (double)sample->accel_g[2] * sample->accel_g[2]);
    cal->accel_samples++;
    welford_add(&cal->accel_mean, &cal->accel_m2, cal->accel_samples, accel_norm);

    (void)now_us;
}

static double stddev(uint32_t n, double m2)
{
    if (n < 2) {
        return 0.0;
    }
    return sqrt(m2 / (double)(n - 1));
}

gg_cal_result_t gg_calibration_poll(gg_calibration_t *cal, uint64_t now_us, float out_bias[3])
{
    if (!cal->active) {
        return GG_CAL_FAIL;
    }
    if (now_us < cal->end_us) {
        return GG_CAL_RUNNING;
    }

    cal->active = false;
    cal->reason = GG_CAL_REASON_NONE;

    if (cal->samples < GG_CAL_MIN_SAMPLES) {
        cal->reason = GG_CAL_REASON_INSUFFICIENT_SAMPLES;
        return GG_CAL_FAIL;
    }
    if (cal->interval_violations > 0 ||
        cal->last_sample_us == 0 ||
        cal->end_us - cal->last_sample_us > GG_CAL_MAX_INTERVAL_US) {
        cal->reason = GG_CAL_REASON_INTERVAL;
        return GG_CAL_FAIL;
    }

    for (int axis = 0; axis < 3; axis++) {
        if (fabs(cal->gyro_mean[axis]) > GG_CAL_GYRO_MEAN_LIMIT_DPS) {
            cal->reason = GG_CAL_REASON_GYRO_MEAN;
            return GG_CAL_FAIL;
        }
    }
    for (int axis = 0; axis < 3; axis++) {
        if (stddev(cal->samples, cal->gyro_m2[axis]) > GG_CAL_GYRO_STD_LIMIT_DPS) {
            cal->reason = GG_CAL_REASON_GYRO_STD;
            return GG_CAL_FAIL;
        }
    }
    if (cal->accel_mean < GG_CAL_ACCEL_MEAN_MIN_G || cal->accel_mean > GG_CAL_ACCEL_MEAN_MAX_G) {
        cal->reason = GG_CAL_REASON_ACCEL_MEAN;
        return GG_CAL_FAIL;
    }
    if (stddev(cal->accel_samples, cal->accel_m2) > GG_CAL_ACCEL_STD_LIMIT_G) {
        cal->reason = GG_CAL_REASON_ACCEL_STD;
        return GG_CAL_FAIL;
    }

    for (int axis = 0; axis < 3; axis++) {
        out_bias[axis] = (float)cal->gyro_mean[axis];
    }
    return GG_CAL_OK;
}

gg_cal_reason_t gg_calibration_reason(const gg_calibration_t *cal)
{
    return cal->reason;
}

const char *gg_calibration_reason_str(gg_cal_reason_t reason)
{
    switch (reason) {
    case GG_CAL_REASON_NONE:
        return "none";
    case GG_CAL_REASON_INSUFFICIENT_SAMPLES:
        return "insufficient_samples";
    case GG_CAL_REASON_INTERVAL:
        return "sample_interval";
    case GG_CAL_REASON_GYRO_MEAN:
        return "gyro_mean";
    case GG_CAL_REASON_GYRO_STD:
        return "gyro_std";
    case GG_CAL_REASON_ACCEL_MEAN:
        return "accel_mean";
    case GG_CAL_REASON_ACCEL_STD:
        return "accel_std";
    default:
        return "unknown";
    }
}
