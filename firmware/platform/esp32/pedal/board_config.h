#pragma once

/* Target board: ESP32-S3-DevKitC-1 N16R8. */

#define USB_DEVICE_VID 0x303A
#define USB_DEVICE_PID 0x8267
#define USB_DEVICE_MANUFACTURER "GunGameSim"
#define USB_DEVICE_PRODUCT "GunGame Pedal Controller"
#define USB_DEVICE_SERIAL "GG-PEDAL-0001"

/* Four direction pedals, active low to GND. Safe GPIOs on N16R8. */
#define PIN_PEDAL_W 4
#define PIN_PEDAL_S 5
#define PIN_PEDAL_A 6
#define PIN_PEDAL_D 7
#define PIN_LED_STATUS 21

