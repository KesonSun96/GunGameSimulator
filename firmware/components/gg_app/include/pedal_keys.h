#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Builds a 6-key HID keyboard report body from the physical pedal states. */
void pedal_keys_build(bool w, bool s, bool a, bool d, uint8_t keys[6]);
