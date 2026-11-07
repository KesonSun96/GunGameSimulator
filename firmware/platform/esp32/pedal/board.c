#include "gg_esp_platform.h"
#include "board_config.h"
#include "usb_hid_kbd.h"

static gg_esp_context_t s_context = {.indicator_pin = PIN_LED_STATUS};
static const gg_esp_pin_t s_pins[] = {
    {GG_INPUT_W, PIN_PEDAL_W}, {GG_INPUT_S, PIN_PEDAL_S},
    {GG_INPUT_A, PIN_PEDAL_A}, {GG_INPUT_D, PIN_PEDAL_D},
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
    return usb_hid_submit_keyboard(report);
}
gg_hal_t gg_esp_board_hal(void)
{
    const gg_hal_t hal = {
        .abi_version = GG_HAL_ABI_VERSION, .ctx = &s_context,
        .init = init, .now_us = gg_esp_now, .wait_next = gg_esp_wait, .read_inputs = inputs,
        .poll_event = poll, .event_overflows = overflows, .submit_report = submit, .reconnect = reconnect,
        .set_indicator = gg_esp_indicator, .log_stats = gg_esp_log,
    };
    return hal;
}
