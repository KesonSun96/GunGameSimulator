#include "gg_esp_platform.h"
#include "board_config.h"
#include "icm42688.h"
#include "usb_hid.h"

static icm42688_t s_imu;
static gg_esp_context_t s_context = {.indicator_pin = PIN_LED_STATUS, .device = &s_imu};
static const gg_esp_pin_t s_pins[] = {
    {GG_INPUT_TRIGGER, PIN_BTN_TRIGGER}, {GG_INPUT_ADS, PIN_BTN_ADS},
    {GG_INPUT_RELOAD, PIN_BTN_RELOAD}, {GG_INPUT_WEAPON1, PIN_BTN_WEAPON1},
    {GG_INPUT_WEAPON2, PIN_BTN_WEAPON2}, {GG_INPUT_CROUCH, PIN_BTN_CROUCH},
    {GG_INPUT_PROFILE, PIN_BTN_PROFILE}, {GG_INPUT_RECENTER, PIN_BTN_RECENTER},
};

static bool init(void *ctx)
{
    if (!gg_esp_inputs_init(ctx, s_pins, sizeof(s_pins) / sizeof(s_pins[0]))) return false;
    usb_hid_init();
    return true;
}
static bool inputs(void *ctx, uint32_t *mask)
{
    (void)ctx;
    return gg_esp_inputs_read(s_pins, sizeof(s_pins) / sizeof(s_pins[0]), mask);
}
static bool poll(void *ctx, gg_transport_event_t *event)
{
    (void)ctx;
    return usb_hid_poll_event(event);
}
static uint32_t overflows(void *ctx) { (void)ctx; return usb_hid_event_overflow_count(); }
static void reconnect(void *ctx) { (void)ctx; usb_hid_request_reconnect(); }
static bool submit(void *ctx, const gg_hid_report_t *report)
{
    (void)ctx;
    return report->itf == GG_HID_ITF_MOUSE ? usb_hid_submit_mouse(report) : usb_hid_submit_keyboard(report);
}
static bool imu_init(void *ctx)
{
    return icm42688_init(((gg_esp_context_t *)ctx)->device) == ESP_OK;
}
static gg_sample_status_t imu_read(void *ctx, gg_imu_sample_t *sample)
{
    return icm42688_read_sample(((gg_esp_context_t *)ctx)->device, sample);
}
static void recovery_start(void *ctx, uint64_t now)
{
    icm42688_recovery_start(((gg_esp_context_t *)ctx)->device, now);
}
static bool recovery_poll(void *ctx, uint64_t now, bool *ok)
{
    return icm42688_recovery_poll(((gg_esp_context_t *)ctx)->device, now, ok);
}

gg_hal_t gg_esp_board_hal(void)
{
    const gg_hal_t hal = {
        .abi_version = GG_HAL_ABI_VERSION, .ctx = &s_context,
        .init = init, .now_us = gg_esp_now, .wait_next = gg_esp_wait, .read_inputs = inputs,
        .poll_event = poll, .event_overflows = overflows, .submit_report = submit, .reconnect = reconnect,
        .init_imu = imu_init, .read_imu = imu_read,
        .start_imu_recovery = recovery_start, .poll_imu_recovery = recovery_poll,
        .set_indicator = gg_esp_indicator, .log_stats = gg_esp_log,
    };
    return hal;
}
