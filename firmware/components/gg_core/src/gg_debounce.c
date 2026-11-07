#include "gg_debounce.h"

void gg_button_init(gg_button_t *button, bool raw_initial, uint64_t debounce_us, uint64_t now_us)
{
    button->debounce_us = debounce_us;
    button->raw_change_us = now_us;
    button->press_us = now_us;
    button->raw_last = raw_initial;
    button->stable = raw_initial;
    button->press_edge = false;
    button->release_edge = false;
}

void gg_button_update(gg_button_t *button, bool raw, uint64_t now_us)
{
    button->press_edge = false;
    button->release_edge = false;

    if (raw != button->raw_last) {
        button->raw_last = raw;
        button->raw_change_us = now_us;
    }

    if (raw != button->stable && (now_us - button->raw_change_us) >= button->debounce_us) {
        button->stable = raw;
        if (!button->stable) {
            button->press_edge = true;
            button->press_us = now_us;
        } else {
            button->release_edge = true;
        }
    }
}

bool gg_button_pressed(const gg_button_t *button)
{
    return !button->stable;
}

bool gg_button_just_pressed(const gg_button_t *button)
{
    return button->press_edge;
}

bool gg_button_just_released(const gg_button_t *button)
{
    return button->release_edge;
}

uint64_t gg_button_held_us(const gg_button_t *button, uint64_t now_us)
{
    if (!gg_button_pressed(button)) {
        return 0;
    }
    return now_us - button->press_us;
}
