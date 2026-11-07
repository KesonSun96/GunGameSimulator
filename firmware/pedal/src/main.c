#include "gg_app.h"
#include "pedal_target.h"

static gg_app_t pedal;

int main(void)
{
    const gg_hal_t hal = pedal_target_hal();
    const gg_app_config_t config = gg_app_default_config(GG_DEVICE_PEDAL);
    if (!gg_app_init(&pedal, &hal, &config)) return 1;
    gg_app_run(&pedal);
    return 0;
}
