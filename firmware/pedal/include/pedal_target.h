#pragma once
#include "gg_hal.h"

/* Only symbol a new MCU port must supply to the pedal entry point. */
gg_hal_t pedal_target_hal(void);
