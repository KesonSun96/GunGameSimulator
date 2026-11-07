#include "../../ports/libopencm3/board.h"
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

const usbd_driver *const board_usb_driver = &st_usbfs_v1_usb_driver;
static volatile uint32_t milliseconds;

void sys_tick_handler(void)
{
    milliseconds++;
}

uint64_t board_now_us(void)
{
    /* Single foreground reader extends the atomic ISR counter across wraps. */
    static uint32_t last;
    static uint64_t elapsed;
    const uint32_t current = milliseconds;
    elapsed += (uint32_t)(current - last);
    last = current;
    return elapsed * 1000;
}

void board_init(void)
{
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_USB);
    const uint16_t switches = GPIO0 | GPIO1 | GPIO10 | GPIO11;
    gpio_set(GPIOB, switches);
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, switches);
    gpio_set(GPIOC, GPIO13);
    gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload(72000 - 1);
    systick_clear();
    systick_interrupt_enable();
    systick_counter_enable();
}

uint32_t board_pressed(void)
{
    const uint32_t pressed = ~gpio_port_read(GPIOB);
    uint32_t mask = 0;
    if (pressed & GPIO0) mask |= GG_INPUT_BIT(GG_INPUT_W);
    if (pressed & GPIO1) mask |= GG_INPUT_BIT(GG_INPUT_S);
    if (pressed & GPIO10) mask |= GG_INPUT_BIT(GG_INPUT_A);
    if (pressed & GPIO11) mask |= GG_INPUT_BIT(GG_INPUT_D);
    return mask;
}

void board_indicator(bool on)
{
    if (on) gpio_clear(GPIOC, GPIO13);
    else gpio_set(GPIOC, GPIO13);
}

void board_usb_attach(bool enable)
{
    /* Blue Pill with fixed external 1.5k D+ pull-up. Hold D+ low during
     * detach with USB reset, then release PA12 to its USB function. */
    if (enable) {
        gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO11 | GPIO12);
    } else {
        rcc_periph_reset_pulse(RST_USB);
        gpio_clear(GPIOA, GPIO12);
        gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
    }
}

