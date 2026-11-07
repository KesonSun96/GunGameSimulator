#include "gg_aim.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float soft_deadzone(float value, float deadzone)
{
    if (deadzone <= 0.0f) {
        return value;
    }
    if (fabsf(value) <= deadzone) {
        return 0.0f;
    }
    return copysignf(fabsf(value) - deadzone, value);
}

void gg_aim_init(gg_aim_t *aim)
{
    memset(aim, 0, sizeof(*aim));
    aim->deadzone_dps = 0.0f;
    aim->counts_per_degree = GG_AIM_COUNTS_PER_DEGREE;
    aim->profile_gain = 1.0f;
    aim->ads_gain = 1.0f;
}

void gg_aim_set_bias(gg_aim_t *aim, const float bias_dps[3])
{
    memcpy(aim->bias_dps, bias_dps, sizeof(aim->bias_dps));
    aim->has_bias = true;
    gg_aim_reset_motion(aim);
}

void gg_aim_set_profile_gain(gg_aim_t *aim, float gain)
{
    aim->profile_gain = gain;
}

void gg_aim_set_ads(gg_aim_t *aim, bool ads, float gain)
{
    aim->ads = ads;
    aim->ads_gain = gain;
}

void gg_aim_reset_motion(gg_aim_t *aim)
{
    aim->filter_init = false;
    aim->lpf_yaw = 0.0f;
    aim->lpf_pitch = 0.0f;
    aim->residual_x = 0.0f;
    aim->residual_y = 0.0f;
}

bool gg_aim_update(gg_aim_t *aim, const gg_imu_sample_t *sample, float dt_s,
                   int16_t *dx, int16_t *dy)
{
    if (sample == NULL || !sample->valid) {
        return false;
    }

    const float gyro_y = sample->gyro_dps[1] - aim->bias_dps[1];
    const float gyro_z = sample->gyro_dps[2] - aim->bias_dps[2];

    /* Fixed V1 mount: +X muzzle, +Y left, +Z up. */
    const float yaw_right = -gyro_z;
    const float pitch_up = -gyro_y;

    if (!aim->filter_init) {
        aim->lpf_yaw = yaw_right;
        aim->lpf_pitch = pitch_up;
        aim->filter_init = true;
        *dx = 0;
        *dy = 0;
        return false;
    }

    if (dt_s <= 0.0f) {
        return false;
    }

    const float rc = 1.0f / (2.0f * (float)M_PI * GG_AIM_LPF_HZ);
    const float alpha = dt_s / (rc + dt_s);
    aim->lpf_yaw += alpha * (yaw_right - aim->lpf_yaw);
    aim->lpf_pitch += alpha * (pitch_up - aim->lpf_pitch);

    const float yaw = soft_deadzone(aim->lpf_yaw, aim->deadzone_dps);
    const float pitch = soft_deadzone(aim->lpf_pitch, aim->deadzone_dps);

    float gain = aim->profile_gain;
    if (aim->ads) {
        gain *= aim->ads_gain;
    }
    const float scale = aim->counts_per_degree * gain * dt_s;

    const float mx = yaw * scale + aim->residual_x;
    const float my = -pitch * scale + aim->residual_y;
    const int32_t ix = (int32_t)mx;
    const int32_t iy = (int32_t)my;
    aim->residual_x = mx - (float)ix;
    aim->residual_y = my - (float)iy;

    *dx = (int16_t)(ix > 32767 ? 32767 : (ix < -32768 ? -32768 : ix));
    *dy = (int16_t)(iy > 32767 ? 32767 : (iy < -32768 ? -32768 : iy));
    return true;
}
