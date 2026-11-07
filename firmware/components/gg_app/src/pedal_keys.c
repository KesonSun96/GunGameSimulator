#include "pedal_keys.h"

#include <string.h>

#include "gg_common.h"

void pedal_keys_build(bool w, bool s, bool a, bool d, uint8_t keys[6])
{
    memset(keys, 0, 6);
    int n = 0;
    if (w) {
        keys[n++] = GG_KEY_W;
    }
    if (s) {
        keys[n++] = GG_KEY_S;
    }
    if (a) {
        keys[n++] = GG_KEY_A;
    }
    if (d && n < 6) {
        keys[n++] = GG_KEY_D;
    }
}
