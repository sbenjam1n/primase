/* accent.c — Periodic velocity modulation */

#include "../telomere_transform.h"
#include "../telomere_pattern_api.h"

static void transform_accent(t_telomere *x, int argc, t_atom *argv) {
    int n = pattern_num_events(x);
    if (n == 0) return;

    int period = (int)atom_getfloatarg(0, argc, argv);
    t_float amount = atom_getfloatarg(1, argc, argv);

    if (period < 1) period = 1;

    for (int i = 0; i < n; i++) {
        if ((i % period) == 0) {
            t_float vel = pattern_get_velocity(x, i) + amount;
            if (vel > 1.0f) vel = 1.0f;
            if (vel < 0.0f) vel = 0.0f;
            pattern_set_velocity(x, i, vel);
        }
    }
    /* No position changes, no sort needed */
}

void accent_register(void) {
    telomere_register_transform(
        gensym("accent"),
        transform_accent,
        "Periodic velocity modulation (period amount)",
        2, 2
    );
}
