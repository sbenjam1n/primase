/* ratio.c — Fractional time scaling for polyrhythm */

#include "../primase_transform.h"
#include "../primase_pattern_api.h"
#include <math.h>

static void transform_ratio(t_primase *x, int argc, t_atom *argv) {
    int n = pattern_num_events(x);
    if (n == 0) return;

    int num = (int)atom_getfloatarg(0, argc, argv);
    int den = (int)atom_getfloatarg(1, argc, argv);
    if (num < 1) num = 1;
    if (den < 1) den = 1;

    float factor = (float)num / (float)den;

    t_float orig[PRIMASE_MAX_EVENTS];
    t_float orig_vel[PRIMASE_MAX_EVENTS];
    int count;
    pattern_copy_to(x, orig, orig_vel, &count);

    if (factor > 1.0f) {
        /* Compress to fit multiple repetitions */
        int reps = (int)ceilf(factor);
        int new_size = count * reps;
        if (new_size > PRIMASE_MAX_EVENTS) new_size = PRIMASE_MAX_EVENTS;
        pattern_resize(x, new_size);

        int idx = 0;
        for (int r = 0; r < reps && idx < new_size; r++) {
            for (int i = 0; i < count && idx < new_size; i++) {
                t_float val = ((t_float)r + orig[i]) / factor;
                if (val >= 0.0f && val < 1.0f) {
                    pattern_set_event(x, idx, val);
                    pattern_set_velocity(x, idx, orig_vel[i]);
                    idx++;
                }
            }
        }
        pattern_resize(x, idx);
    } else if (factor < 1.0f) {
        /* Stretch: multiply positions by 1/factor, discard >= 1.0 */
        float inv = 1.0f / factor;
        int idx = 0;
        for (int i = 0; i < count; i++) {
            t_float val = orig[i] * inv;
            if (val < 1.0f) {
                pattern_set_event(x, idx, val);
                pattern_set_velocity(x, idx, orig_vel[i]);
                idx++;
            }
        }
        pattern_resize(x, idx);
    }
    /* factor == 1.0: no-op */

    pattern_sort(x);
}

void ratio_register(void) {
    primase_register_transform(
        gensym("ratio"),
        transform_ratio,
        "Fractional time scaling N/D for polyrhythm",
        2, 2
    );
}
