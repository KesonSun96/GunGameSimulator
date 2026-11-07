#pragma once

#include <stdbool.h>
#include <stdint.h>

/* DS-000347 v1.7, INTF_CONFIG0: 16-bit big-endian, HOLD_LAST_DATA=0,
 * FSYNC tagging disabled. -32768/-32767 are outside the valid sample range.
 * A completely zero SPI payload is rejected as a probable wiring fault.
 */
static inline bool icm42688_decode(const uint8_t buf[12], int16_t gyro[3], int16_t accel[3])
{
    bool nonzero = false;
    for (int i = 0; i < 12; i++) {
        nonzero |= buf[i] != 0;
    }
    if (!nonzero) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        accel[i] = (int16_t)(((uint16_t)buf[i * 2] << 8) | buf[i * 2 + 1]);
        gyro[i] = (int16_t)(((uint16_t)buf[6 + i * 2] << 8) | buf[7 + i * 2]);
        if (accel[i] <= -32767 || gyro[i] <= -32767) {
            return false;
        }
    }
    return true;
}
