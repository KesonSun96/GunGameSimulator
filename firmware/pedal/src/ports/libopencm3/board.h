#pragma once
#include <libopencm3/usb/usbd.h>
#include "gg_hal.h"

/* Board-specific clock tree, USB peripheral, pins, time and pull-up circuit. */
void board_init(void);
uint64_t board_now_us(void);
uint32_t board_pressed(void);
void board_indicator(bool on);
void board_usb_attach(bool enable);
extern const usbd_driver *const board_usb_driver;
