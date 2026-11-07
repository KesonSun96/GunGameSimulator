#pragma once

#include "gg_hid_output.h"

/* Logical transport events: never expose endpoint numbers or SDK types. */
typedef enum {
    GG_TRANSPORT_CONNECT, GG_TRANSPORT_DISCONNECT,
    GG_TRANSPORT_SUSPEND, GG_TRANSPORT_RESUME,
    GG_TRANSPORT_COMPLETE, GG_TRANSPORT_FAILED,
} gg_transport_event_type_t;

typedef struct {
    gg_transport_event_type_t type;
    uint32_t session;
    uint8_t itf; /* GG_HID_ITF_*, not the physical USB interface number. */
    uint32_t submit_id;
} gg_transport_event_t;
