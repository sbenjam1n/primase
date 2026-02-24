/* drift.c — Evolving random walk on event positions */

#include "../telomere_transform.h"
#include "../telomere_pattern_api.h"
#include <stdlib.h>

static void transform_drift(t_telomere *x, int argc, t_atom *argv) {
    int n = pattern_num_events(x);
    if (n == 0) return;

    t_float amount = atom_getfloatarg(0, argc, argv);
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;

    for (int i = 0; i < n; i++) {
        t_float pos = pattern_get_event(x, i);
        float r = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        pos += r * amount;
        /* Wrap into [0, 1) */
        while (pos >= 1.0f) pos -= 1.0f;
        while (pos < 0.0f)  pos += 1.0f;
        pattern_set_event(x, i, pos);
    }

    pattern_sort(x);
}

void drift_register(void) {
    telomere_register_transform(
        gensym("drift"),
        transform_drift,
        "Evolving random walk on positions (amount)",
        1, 1
    );
}
