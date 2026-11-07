/*
 * Polling driver for the TDK InvenSense ICM-42688-P over SPI.
 *
 * Data-ready semantics used here: INT_STATUS (0x2D) bit 3 (DATA_RDY_INT) is
 * read-to-clear. Reading the status only when data is ready then reading the
 * 12 data bytes gives one complete accel+gyro snapshot per call. V1 ODR is
 * 1 kHz; the app polls at 1 kHz and only advances time on NEW samples.
 */
#include "icm42688.h"
#include "icm42688_decode.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_config.h"

static const char *TAG = "icm42688";

#define ICM_REG_DEVICE_CONFIG 0x11
#define ICM_REG_ACCEL_DATA_X1 0x1F
#define ICM_REG_GYRO_DATA_X1 0x25
#define ICM_REG_INT_STATUS 0x2D
#define ICM_REG_PWR_MGMT0 0x4E
#define ICM_REG_INTF_CONFIG0 0x4C
#define ICM_REG_FSYNC_CONFIG 0x62
#define ICM_REG_GYRO_CONFIG0 0x4F
#define ICM_REG_ACCEL_CONFIG0 0x50
#define ICM_REG_GYRO_CONFIG1 0x51
#define ICM_REG_GYRO_ACCEL_CONFIG0 0x52
#define ICM_REG_WHO_AM_I 0x75
#define ICM_REG_BANK_SEL 0x76

#define ICM_WHO_AM_I_VALUE 0x47
#define ICM_SPI_READ_BIT 0x80
#define ICM_BANK0 0x00
#define ICM_INT_STATUS_DATA_RDY (1u << 3)

static esp_err_t write_reg(icm42688_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {reg & 0x7F, value};
    spi_transaction_t t = {
        .length = 2 * 8,
        .tx_buffer = tx,
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

static esp_err_t read_regs(icm42688_t *dev, uint8_t reg, uint8_t *out, size_t len)
{
    uint8_t tx[1 + 16] = {0};
    uint8_t rx[1 + 16] = {0};
    if (len > 16) {
        return ESP_ERR_INVALID_SIZE;
    }

    tx[0] = reg | ICM_SPI_READ_BIT;
    spi_transaction_t t = {
        .length = (len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_polling_transmit(dev->spi, &t);
    if (err == ESP_OK) {
        memcpy(out, rx + 1, len);
    }
    return err;
}

static esp_err_t read_who_am_i(icm42688_t *dev)
{
    uint8_t who = 0;
    esp_err_t err = read_regs(dev, ICM_REG_WHO_AM_I, &who, 1);
    if (err != ESP_OK) {
        return err;
    }
    return who == ICM_WHO_AM_I_VALUE ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t config_off(icm42688_t *dev)
{
    esp_err_t err = write_reg(dev, ICM_REG_BANK_SEL, ICM_BANK0);
    if (err != ESP_OK) {
        return err;
    }
    return write_reg(dev, ICM_REG_PWR_MGMT0, 0x00);
}

static esp_err_t config_run(icm42688_t *dev)
{
    const uint8_t gyro_cfg = (uint8_t)((IMU_GYRO_FS << 5) | (IMU_ODR_CODE & 0x0F));
    const uint8_t accel_cfg = (uint8_t)((IMU_ACCEL_FS << 5) | (IMU_ODR_CODE & 0x0F));
    esp_err_t err = write_reg(dev, ICM_REG_GYRO_CONFIG0, gyro_cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = write_reg(dev, ICM_REG_ACCEL_CONFIG0, accel_cfg);
    if (err != ESP_OK) {
        return err;
    }
    /* Explicit data format for icm42688_decode: big-endian, no hold-last
     * mode, no FSYNC tagging. Keep SPI/I2C interface selection at reset. */
    err = write_reg(dev, ICM_REG_INTF_CONFIG0, 0x30);
    if (err != ESP_OK) {
        return err;
    }
    err = write_reg(dev, ICM_REG_FSYNC_CONFIG, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = write_reg(dev, ICM_REG_GYRO_CONFIG1, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = write_reg(dev, ICM_REG_GYRO_ACCEL_CONFIG0, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = write_reg(dev, ICM_REG_PWR_MGMT0, 0x0F);
    if (err != ESP_OK) {
        return err;
    }

    dev->gyro_lsb_per_dps = (IMU_GYRO_FS == 0) ? 16.4f
                          : (IMU_GYRO_FS == 1) ? 32.8f
                          : (IMU_GYRO_FS == 2) ? 65.5f
                                               : 131.0f;
    dev->accel_lsb_per_g = (IMU_ACCEL_FS == 0) ? 2048.0f
                         : (IMU_ACCEL_FS == 1) ? 4096.0f
                         : (IMU_ACCEL_FS == 2) ? 8192.0f
                                               : 16384.0f;
    return ESP_OK;
}

static esp_err_t configure(icm42688_t *dev)
{
    esp_err_t err = config_off(dev);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
    err = config_run(dev);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static esp_err_t init_spi(icm42688_t *dev)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = IMU_PIN_MOSI,
        .miso_io_num = IMU_PIN_MISO,
        .sclk_io_num = IMU_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    esp_err_t err = spi_bus_initialize(IMU_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = IMU_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = IMU_PIN_CS,
        .queue_size = 1,
    };
    err = spi_bus_add_device(IMU_SPI_HOST, &dev_cfg, &dev->spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t icm42688_init(icm42688_t *dev)
{
    memset(dev, 0, sizeof(*dev));

    esp_err_t err = init_spi(dev);
    if (err != ESP_OK) {
        return err;
    }
    err = read_who_am_i(dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I mismatch, err=%s", esp_err_to_name(err));
        return err;
    }

    err = write_reg(dev, ICM_REG_DEVICE_CONFIG, 0x01);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    err = read_who_am_i(dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "device did not come back after reset");
        return err;
    }

    err = configure(dev);
    if (err == ESP_OK) {
        dev->initialized = true;
        ESP_LOGI(TAG, "ready (gyro_fs=%d, accel_fs=%d, odr=0x%02X)",
                 IMU_GYRO_FS, IMU_ACCEL_FS, IMU_ODR_CODE);
    }
    return err;
}

void icm42688_recovery_start(icm42688_t *dev, uint64_t now_us)
{
    if (dev == NULL || dev->spi == NULL) {
        return;
    }
    dev->initialized = false;
    dev->recovery_stage = ICM_RECOVERY_RESET_WAIT;
    dev->recovery_deadline_us = now_us + 10000u;

    if (read_who_am_i(dev) != ESP_OK ||
        write_reg(dev, ICM_REG_DEVICE_CONFIG, 0x01) != ESP_OK) {
        dev->recovery_stage = ICM_RECOVERY_FAILED;
    }
}

bool icm42688_recovery_poll(icm42688_t *dev, uint64_t now_us, bool *ok)
{
    if (dev == NULL || ok == NULL) {
        return true;
    }
    *ok = false;

    switch (dev->recovery_stage) {
    case ICM_RECOVERY_RESET_WAIT:
        if (now_us < dev->recovery_deadline_us) {
            return false;
        }
        if (read_who_am_i(dev) != ESP_OK || config_off(dev) != ESP_OK) {
            dev->recovery_stage = ICM_RECOVERY_FAILED;
            return true;
        }
        dev->recovery_stage = ICM_RECOVERY_OFF_WAIT;
        dev->recovery_deadline_us = now_us + 1000u;
        return false;

    case ICM_RECOVERY_OFF_WAIT:
        if (now_us < dev->recovery_deadline_us) {
            return false;
        }
        if (config_run(dev) != ESP_OK) {
            dev->recovery_stage = ICM_RECOVERY_FAILED;
            return true;
        }
        dev->recovery_stage = ICM_RECOVERY_STARTUP_WAIT;
        dev->recovery_deadline_us = now_us + 50000u;
        return false;

    case ICM_RECOVERY_STARTUP_WAIT:
        if (now_us < dev->recovery_deadline_us) {
            return false;
        }
        dev->initialized = true;
        dev->recovery_stage = ICM_RECOVERY_DONE;
        *ok = true;
        return true;

    case ICM_RECOVERY_DONE:
        *ok = true;
        return true;

    case ICM_RECOVERY_FAILED:
        return true;

    case ICM_RECOVERY_IDLE:
    default:
        return true;
    }
}

gg_sample_status_t icm42688_read_sample(icm42688_t *dev, gg_imu_sample_t *sample)
{
    if (dev == NULL || sample == NULL || !dev->initialized) {
        return GG_SAMPLE_IO_ERROR;
    }
    memset(sample, 0, sizeof(*sample));

    uint8_t status = 0;
    esp_err_t err = read_regs(dev, ICM_REG_INT_STATUS, &status, 1);
    if (err != ESP_OK) {
        return GG_SAMPLE_IO_ERROR;
    }
    if ((status & ICM_INT_STATUS_DATA_RDY) == 0) {
        return GG_SAMPLE_NONE;
    }

    uint8_t buf[12] = {0};
    err = read_regs(dev, ICM_REG_ACCEL_DATA_X1, buf, sizeof(buf));
    if (err != ESP_OK) {
        return GG_SAMPLE_IO_ERROR;
    }

    int16_t accel_raw[3];
    int16_t gyro_raw[3];

    sample->timestamp_us = (uint64_t)esp_timer_get_time();
    sample->sequence = ++dev->sequence;
    if (!icm42688_decode(buf, gyro_raw, accel_raw)) {
        sample->valid = false;
        return GG_SAMPLE_INVALID;
    }

    for (int i = 0; i < 3; i++) {
        sample->gyro_dps[i] = (float)gyro_raw[i] / dev->gyro_lsb_per_dps;
        sample->accel_g[i] = (float)accel_raw[i] / dev->accel_lsb_per_g;
    }
    sample->valid = true;
    return GG_SAMPLE_NEW;
}
