#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GG_IMU_AXIS_COUNT 3

typedef struct {
    float gyro_dps[GG_IMU_AXIS_COUNT];
    float accel_g[GG_IMU_AXIS_COUNT];
    uint64_t timestamp_us;
    uint32_t sequence;
    bool valid;
} gg_imu_sample_t;

typedef enum {
    GG_SAMPLE_NONE = 0,
    GG_SAMPLE_NEW,
    GG_SAMPLE_IO_ERROR,
    GG_SAMPLE_INVALID,
} gg_sample_status_t;

/* USB HID keyboard usages used by the gun, from the HID Usage Tables. */
#define GG_KEY_R 0x15
#define GG_KEY_1 0x1E
#define GG_KEY_2 0x1F
#define GG_KEY_MOD_LEFT_CTRL 0x01
#define GG_KEY_A 0x04
#define GG_KEY_D 0x07
#define GG_KEY_S 0x16
#define GG_KEY_W 0x1A

static inline uint64_t gg_ms_to_us(uint32_t ms)
{
    return (uint64_t)ms * 1000u;
}
