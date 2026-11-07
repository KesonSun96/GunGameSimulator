#pragma once

#include "gg_common.h"
#include "gg_transport.h"

#define GG_HAL_ABI_VERSION 1u

/* Logical controls; adapters map wiring/polarity to 1 = physically pressed. */
typedef enum {
    GG_INPUT_TRIGGER, GG_INPUT_ADS, GG_INPUT_RELOAD, GG_INPUT_WEAPON1,
    GG_INPUT_WEAPON2, GG_INPUT_CROUCH, GG_INPUT_PROFILE, GG_INPUT_RECENTER,
    GG_INPUT_W, GG_INPUT_S, GG_INPUT_A, GG_INPUT_D, GG_INPUT_COUNT,
} gg_input_t;
#define GG_INPUT_BIT(input) (UINT32_C(1) << (input))

struct gg_app_stats;

/* All operations run in application context except an adapter's own workers.
 * ctx and its resources must outlive the app. No SDK types cross this ABI.
 * init may block at startup; tick-path operations must be bounded/nonblocking.
 */
typedef struct {
    uint32_t abi_version;
    void *ctx;
    bool (*init)(void *ctx); /* Board inputs, indicator and transport. */
    uint64_t (*now_us)(void *ctx); /* Monotonic uint64 microseconds; IMU uses SAME clock. */
    void (*wait_next)(void *ctx, uint32_t period_us); /* No catch-up bursts after overruns. */
    bool (*read_inputs)(void *ctx, uint32_t *pressed_mask); /* Raw, not debounced. */
    bool (*poll_event)(void *ctx, gg_transport_event_t *event); /* FIFO, nonblocking. */
    uint32_t (*event_overflows)(void *ctx); /* Monotonic wrapping counter. */
    bool (*submit_report)(void *ctx, const gg_hid_report_t *report); /* Must copy before return. */
    void (*reconnect)(void *ctx); /* Request only; report disconnect/connect via events. */

    /* Gun only. Samples are body axes: +X muzzle, +Y left, +Z up; dps and g.
     * NEW requires a fresh valid sample, increasing sequence/timestamp.
     * init_imu failure is recoverable and must leave input/transport working.
     */
    bool (*init_imu)(void *ctx);
    gg_sample_status_t (*read_imu)(void *ctx, gg_imu_sample_t *sample);
    void (*start_imu_recovery)(void *ctx, uint64_t now_us);
    bool (*poll_imu_recovery)(void *ctx, uint64_t now_us, bool *ok);

    /* Optional: platforms may have no LED and no logging backend. */
    void (*set_indicator)(void *ctx, bool on);
    void (*log_stats)(void *ctx, const struct gg_app_stats *stats);
} gg_hal_t;
