#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gg_common.h"

#define GG_AIM_LPF_HZ 55.0f
#define GG_AIM_COUNTS_PER_DEGREE 3.0f

typedef struct {
    float bias_dps[3];
    bool has_bias;

    float lpf_yaw;
    float lpf_pitch;
    bool filter_init;

    float residual_x;
    float residual_y;

    float deadzone_dps;
    float counts_per_degree;
    float profile_gain;
    float ads_gain;
    bool ads;
} gg_aim_t;

void gg_aim_init(gg_aim_t *aim);
void gg_aim_set_bias(gg_aim_t *aim, const float bias_dps[3]);
void gg_aim_set_profile_gain(gg_aim_t *aim, float gain);
void gg_aim_set_ads(gg_aim_t *aim, bool ads, float gain);
void gg_aim_reset_motion(gg_aim_t *aim);

/*
 * ds is the real time between consecutive valid NEW samples. The first call
 * after a reset only initializes the filter and produces no movement.
 */
bool gg_aim_update(gg_aim_t *aim, const gg_imu_sample_t *sample, float dt_s,
                   int16_t *dx, int16_t *dy);
