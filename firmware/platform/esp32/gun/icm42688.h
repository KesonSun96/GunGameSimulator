#pragma once

#include "driver/spi_master.h"
#include "esp_err.h"

#include "gg_common.h"

typedef enum {
    ICM_RECOVERY_IDLE = 0,
    ICM_RECOVERY_RESET_WAIT,
    ICM_RECOVERY_OFF_WAIT,
    ICM_RECOVERY_STARTUP_WAIT,
    ICM_RECOVERY_DONE,
    ICM_RECOVERY_FAILED,
} icm_recovery_stage_t;

typedef struct {
    spi_device_handle_t spi;
    float gyro_lsb_per_dps;
    float accel_lsb_per_g;
    uint32_t sequence;
    bool initialized;
    icm_recovery_stage_t recovery_stage;
    uint64_t recovery_deadline_us;
} icm42688_t;

esp_err_t icm42688_init(icm42688_t *dev);

/*
 * Non-blocking recovery for the IMU_FAULT path. start() only kicks off the
 * sequence; poll() advances it and reports completion through *ok.
 */
void icm42688_recovery_start(icm42688_t *dev, uint64_t now_us);
bool icm42688_recovery_poll(icm42688_t *dev, uint64_t now_us, bool *ok);

/*
 * Polls the data-ready bit and reads one complete snapshot. The timestamp is
 * the monotonic time at which readiness was observed, not the sensor sample
 * time. No sample is produced when no new data is ready.
 */
gg_sample_status_t icm42688_read_sample(icm42688_t *dev, gg_imu_sample_t *sample);
