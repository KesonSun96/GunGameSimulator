/*
 * GunGame Simulator - gun controller board configuration.
 *
 * Target board: ESP32-S3-DevKitC-1 N16R8 (16 MB flash, 8 MB octal PSRAM)
 *
 * Safe GPIO reminder for ESP32-S3 N16R8:
 *   - GPIO19/20 : native USB D-/D+
 *   - GPIO26..32: SPI flash
 *   - GPIO33..37: octal PSRAM (do not use)
 *   - GPIO0/3/45/46: strapping pins, avoid for buttons
 */
#pragma once

/* ---------------- USB identity ---------------- */
#define USB_DEVICE_VID 0x303A
#define USB_DEVICE_PID 0x8266
#define USB_DEVICE_MANUFACTURER "GunGameSim"
#define USB_DEVICE_PRODUCT "GunGame Gun Controller"
#define USB_DEVICE_SERIAL "GG-GUN-0001"

/* ---------------- IMU (ICM-42688-P, SPI2) ---------------- */
#define IMU_SPI_HOST SPI2_HOST
#define IMU_PIN_SCK 12
#define IMU_PIN_MOSI 11
#define IMU_PIN_MISO 13
#define IMU_PIN_CS 10
#define IMU_SPI_CLOCK_HZ (10 * 1000 * 1000)

/* Gyro full scale: 0 = +/-2000 dps, 1 = +/-1000, 2 = +/-500, 3 = +/-250 */
#define IMU_GYRO_FS 0
/* Accel full scale: 0 = +/-16 g, 1 = +/-8 g, 2 = +/-4 g, 3 = +/-2 g */
#define IMU_ACCEL_FS 1
/* ODR code: 0x05 = 2 kHz, 0x06 = 1 kHz (V1 uses 1 kHz) */
#define IMU_ODR_CODE 0x06

/* ---------------- Buttons (active low to GND) ---------------- */
#define PIN_BTN_TRIGGER 4
#define PIN_BTN_ADS 5
#define PIN_BTN_RELOAD 6
#define PIN_BTN_WEAPON1 7
#define PIN_BTN_WEAPON2 15
#define PIN_BTN_CROUCH 16
#define PIN_BTN_PROFILE 17
#define PIN_BTN_RECENTER 18
#define PIN_LED_STATUS 21

