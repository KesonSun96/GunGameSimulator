#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gg_app.h"

typedef struct {
    TickType_t last_wake;
    bool schedule_started;
    int indicator_pin;
    void *device;
} gg_esp_context_t;

typedef struct { gg_input_t input; int gpio; } gg_esp_pin_t;

bool gg_esp_inputs_init(gg_esp_context_t *ctx, const gg_esp_pin_t *pins, unsigned count);
bool gg_esp_inputs_read(const gg_esp_pin_t *pins, unsigned count, uint32_t *mask);
uint64_t gg_esp_now(void *ctx);
void gg_esp_wait(void *ctx, uint32_t period_us);
void gg_esp_indicator(void *ctx, bool on);
void gg_esp_log(void *ctx, const gg_app_stats_t *stats);
gg_hal_t gg_esp_board_hal(void); /* Supplied by selected board adapter. */
void gg_esp_platform_start(gg_device_t device);
