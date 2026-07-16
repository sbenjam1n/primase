/* degrade.c — Probabilistically remove events (destructive version) */

#include "../primase_transform.h"
#include "../primase_pattern_api.h"
#include <stdlib.h>

static void transform_degrade(t_primase *x, int argc, t_atom *argv) {
    int n = pattern_num_events(x);
    if (n == 0) return;

    t_float prob = atom_getfloatarg(0, argc, argv);
    if (prob < 0.0f) prob = 0.0f;
    if (prob > 1.0f) prob = 1.0f;

    t_float survivors[PRIMASE_MAX_EVENTS];
    t_float surv_vel[PRIMASE_MAX_EVENTS];
    int surv_count = 0;

    for (int i = 0; i < n; i++) {
        float r = (float)rand() / (float)RAND_MAX;
        if (r >= prob) {
            survivors[surv_count] = pattern_get_event(x, i);
            surv_vel[surv_count]  = pattern_get_velocity(x, i);
            surv_count++;
        }
    }

    if (surv_count == 0 && n > 0) {
        survivors[0] = pattern_get_event(x, 0);
        surv_vel[0]  = pattern_get_velocity(x, 0);
        surv_count = 1;
    }

    pattern_replace(x, survivors, surv_vel, surv_count);
}

void degrade_register(void) {
    primase_register_transform(
        gensym("degrade"),
        transform_degrade,
        "Probabilistically remove events (keeps at least one)",
        1, 1
    );
}
