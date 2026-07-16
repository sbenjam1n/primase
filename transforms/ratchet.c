/* ratchet.c — Subdivide a single event into a rapid burst */

#include "../primase_transform.h"
#include "../primase_pattern_api.h"

static void transform_ratchet(t_primase *x, int argc, t_atom *argv) {
    int n = pattern_num_events(x);
    if (n == 0) return;

    int index = (int)atom_getfloatarg(0, argc, argv);
    int count = (int)atom_getfloatarg(1, argc, argv);

    if (index < 0 || index >= n) return;
    if (count < 2) count = 2;
    if (count > 8) count = 8;

    t_float this_pos = pattern_get_event(x, index);
    t_float next_pos = (index + 1 < n) ? pattern_get_event(x, index + 1) : 1.0f;
    t_float span = next_pos - this_pos;
    if (span <= 0.0f) span = 1.0f / 16.0f;  /* fallback minimum span */

    t_float orig_vel = pattern_get_velocity(x, index);

    /* Save all events after the target */
    t_float after_pos[PRIMASE_MAX_EVENTS];
    t_float after_vel[PRIMASE_MAX_EVENTS];
    int after_count = n - index - 1;
    for (int i = 0; i < after_count; i++) {
        after_pos[i] = pattern_get_event(x, index + 1 + i);
        after_vel[i] = pattern_get_velocity(x, index + 1 + i);
    }

    /* Resize to fit: events before target + count ratcheted + events after */
    int new_total = index + count + after_count;
    if (new_total > PRIMASE_MAX_EVENTS) {
        count = PRIMASE_MAX_EVENTS - index - after_count;
        if (count < 2) return;
        new_total = index + count + after_count;
    }
    pattern_resize(x, new_total);

    /* Write ratcheted events */
    t_float sub_step = span / (t_float)count;
    for (int i = 0; i < count; i++) {
        t_float pos = this_pos + sub_step * (t_float)i;
        if (pos >= 1.0f) pos -= 1.0f;
        pattern_set_event(x, index + i, pos);
        pattern_set_velocity(x, index + i, orig_vel);
    }

    /* Write back events after the target */
    for (int i = 0; i < after_count; i++) {
        pattern_set_event(x, index + count + i, after_pos[i]);
        pattern_set_velocity(x, index + count + i, after_vel[i]);
    }

    pattern_sort(x);
}

void ratchet_register(void) {
    primase_register_transform(
        gensym("ratchet"),
        transform_ratchet,
        "Subdivide event at index into count rapid hits",
        2, 2
    );
}
