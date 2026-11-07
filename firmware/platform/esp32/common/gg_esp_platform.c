#include "gg_esp_platform.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

static gg_app_t s_app;

uint64_t gg_esp_now(void *ctx)
{
    (void)ctx;
    return (uint64_t)esp_timer_get_time();
}

void gg_esp_wait(void *ctx, uint32_t period_us)
{
    gg_esp_context_t *state = ctx;
    if (!state->schedule_started) {
        state->last_wake = xTaskGetTickCount();
        state->schedule_started = true;
    }
    TickType_t ticks = pdMS_TO_TICKS((period_us + 999u) / 1000u);
    if (!ticks) ticks = 1;
    const TickType_t now = xTaskGetTickCount();
    if (now - state->last_wake > ticks) state->last_wake = now;
    vTaskDelayUntil(&state->last_wake, ticks);
}

bool gg_esp_inputs_init(gg_esp_context_t *ctx, const gg_esp_pin_t *pins, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        const gpio_config_t cfg = {
            .pin_bit_mask = UINT64_C(1) << pins[i].gpio,
            .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&cfg) != ESP_OK) return false;
    }
    const gpio_config_t led = {
        .pin_bit_mask = UINT64_C(1) << ctx->indicator_pin,
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&led) != ESP_OK) return false;
    return gpio_set_level(ctx->indicator_pin, 0) == ESP_OK;
}

bool gg_esp_inputs_read(const gg_esp_pin_t *pins, unsigned count, uint32_t *mask)
{
    *mask = 0;
    for (unsigned i = 0; i < count; i++) {
        if (!gpio_get_level(pins[i].gpio)) *mask |= GG_INPUT_BIT(pins[i].input);
    }
    return true;
}

void gg_esp_indicator(void *ctx, bool on)
{
    gg_esp_context_t *state = ctx;
    gpio_set_level(state->indicator_pin, on ? 1 : 0);
}

void gg_esp_log(void *ctx, const gg_app_stats_t *s)
{
    (void)ctx;
    ESP_LOGI("gg", "dev=%s state=%s cal=%s usb=%d prof=%d valid=%u invalid=%u io=%u "
             "input_err=%u maxdt=%u overrun=%u exec=%u q=%u ovf=%u drop=%u done=%u recon=%u",
             s->device == GG_DEVICE_GUN ? "gun" : "pedal", gg_state_str(s->state),
             gg_calibration_reason_str(s->calibration_reason), s->connected, s->profile,
             (unsigned)s->valid_samples, (unsigned)s->invalid_samples, (unsigned)s->io_errors,
             (unsigned)s->input_errors, (unsigned)s->max_sample_interval_us,
             (unsigned)s->overruns, (unsigned)s->max_exec_us, (unsigned)s->queue_high_water,
             (unsigned)s->overflows, (unsigned)s->dropped_motion,
             (unsigned)s->completed_reports, (unsigned)s->reconnects);
}

static void application_task(void *ctx)
{
    gg_app_run(ctx);
    vTaskDelete(NULL);
}

void gg_esp_platform_start(gg_device_t device)
{
    const gg_hal_t hal = gg_esp_board_hal();
    const gg_app_config_t cfg = gg_app_default_config(device);
    ESP_ERROR_CHECK(gg_app_init(&s_app, &hal, &cfg) ? ESP_OK : ESP_FAIL);
    const BaseType_t ok = xTaskCreatePinnedToCore(application_task, "gg_app", 4096,
                                                &s_app, 12, NULL, 1);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
