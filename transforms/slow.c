/* slow.c — Stretch pattern by factor N (keep only 1/N of events per cycle) */

#include "../primase_transform.h"
#include "../primase_pattern_api.h"

static void transform_slow(t_primase *x, int argc, t_atom *argv) {
    int n = pattern_num_events(x);
    if (n == 0) return;

    int factor = (int)atom_getfloatarg(0, argc, argv);
    if (factor < 2) factor = 2;

    /* Multiply all positions by factor.  Events that land at or beyond 1.0
     * fall into a future cycle and are discarded.  An event originally at
     * position p moves to p*factor; only events with p < 1/factor survive.
     *
     * This is the exact inverse of fast N: fast 2 doubles the density,
     * slow 2 halves it.  The surviving events are spread further apart in
     * time, so the pattern plays perceptually slower. */
    t_float survivors[PRIMASE_MAX_EVENTS];
    t_float surv_vel[PRIMASE_MAX_EVENTS];
    int surv_count = 0;

    t_float scale = (t_float)factor;
    for (int i = 0; i < n; i++) {
        t_float val = pattern_get_event(x, i) * scale;
        if (val < 1.0f) {
            survivors[surv_count] = val;
            surv_vel[surv_count]  = pattern_get_velocity(x, i);
            surv_count++;
        }
    }

    /* Edge case: every event fell outside the cycle — keep the first at 0 */
    if (surv_count == 0) {
        survivors[0] = 0.0f;
        surv_vel[0]  = pattern_get_velocity(x, 0);
        surv_count   = 1;
    }

    /* Input was sorted and we scaled monotonically; output is already sorted */
    pattern_replace(x, survivors, surv_vel, surv_count);
}

void slow_register(void) {
    primase_register_transform(
        gensym("slow"),
        transform_slow,
        "Stretch pattern by factor N (keep only 1/N events per cycle)",
        1, 1
    );
}
