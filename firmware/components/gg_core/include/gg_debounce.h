#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Time-driven debounce with no hardware dependency. The caller supplies
 * monotonic microseconds, so the same code is used on the device and in the
 * host tests.
 */
typedef struct {
    uint64_t debounce_us;
    uint64_t raw_change_us;
    uint64_t press_us;
    bool raw_last;
    bool stable;
    bool press_edge;
    bool release_edge;
} gg_button_t;

void gg_button_init(gg_button_t *button, bool raw_initial, uint64_t debounce_us, uint64_t now_us);
void gg_button_update(gg_button_t *button, bool raw, uint64_t now_us);

bool gg_button_pressed(const gg_button_t *button);
bool gg_button_just_pressed(const gg_button_t *button);
bool gg_button_just_released(const gg_button_t *button);
uint64_t gg_button_held_us(const gg_button_t *button, uint64_t now_us);
