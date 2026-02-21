/* telomere_pattern_api.c — Pattern manipulation API implementation */

#include "telomere_pattern_api.h"
#include <string.h>

/* --- Read access --- */

int pattern_num_events(t_telomere *x) {
    return x->num_events;
}

t_float pattern_get_event(t_telomere *x, int index) {
    if (index < 0 || index >= x->num_events) return 0.0f;
    return x->pattern[index];
}

t_float pattern_get_velocity(t_telomere *x, int index) {
    if (index < 0 || index >= x->num_events) return 1.0f;
    return x->velocity[index];
}

t_float *pattern_get_buffer(t_telomere *x) {
    return x->pattern;
}

/* --- Write access --- */

void pattern_set_event(t_telomere *x, int index, t_float value) {
    if (index < 0 || index >= x->num_events) return;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    x->pattern[index] = value;
}

void pattern_set_velocity(t_telomere *x, int index, t_float vel) {
    if (index < 0 || index >= x->num_events) return;
    if (vel < 0.0f) vel = 0.0f;
    if (vel > 1.0f) vel = 1.0f;
    x->velocity[index] = vel;
}

static void pattern_ensure_capacity(t_telomere *x, int needed) {
    if (needed <= x->pattern_alloc) return;
    int new_alloc = x->pattern_alloc;
    while (new_alloc < needed) {
        new_alloc = (new_alloc < 16) ? 16 : new_alloc * 2;
    }
    if (new_alloc > TELOMERE_MAX_EVENTS) new_alloc = TELOMERE_MAX_EVENTS;
    x->pattern  = (t_float *)resizebytes(x->pattern,
        x->pattern_alloc * sizeof(t_float),
        new_alloc * sizeof(t_float));
    x->velocity = (t_float *)resizebytes(x->velocity,
        x->pattern_alloc * sizeof(t_float),
        new_alloc * sizeof(t_float));
    x->pattern_alloc = new_alloc;
}

void pattern_append_event(t_telomere *x, t_float value, t_float vel) {
    if (x->num_events >= TELOMERE_MAX_EVENTS) return;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    if (vel   < 0.0f) vel   = 0.0f;
    if (vel   > 1.0f) vel   = 1.0f;
    pattern_ensure_capacity(x, x->num_events + 1);
    x->pattern [x->num_events] = value;
    x->velocity[x->num_events] = vel;
    x->num_events++;
}

void pattern_resize(t_telomere *x, int new_size) {
    if (new_size < 0) new_size = 0;
    if (new_size > TELOMERE_MAX_EVENTS) new_size = TELOMERE_MAX_EVENTS;
    pattern_ensure_capacity(x, new_size);
    if (new_size > x->num_events) {
        memset(x->pattern + x->num_events, 0,
               (new_size - x->num_events) * sizeof(t_float));
        /* default velocity 1.0 for newly added slots */
        for (int i = x->num_events; i < new_size; i++)
            x->velocity[i] = 1.0f;
    }
    x->num_events = new_size;
}

void pattern_clear(t_telomere *x) {
    x->num_events = 0;
}

/* Insertion sort — carries velocity alongside position */
void pattern_sort(t_telomere *x) {
    int n = x->num_events;
    t_float *p = x->pattern;
    t_float *v = x->velocity;
    for (int i = 1; i < n; i++) {
        t_float kp = p[i];
        t_float kv = v[i];
        int j = i - 1;
        while (j >= 0 && p[j] > kp) {
            p[j + 1] = p[j];
            v[j + 1] = v[j];
            j--;
        }
        p[j + 1] = kp;
        v[j + 1] = kv;
    }
}

/* --- Bulk operations --- */

void pattern_replace(t_telomere *x, t_float *new_pos, t_float *new_vel, int count) {
    if (count < 0) count = 0;
    if (count > TELOMERE_MAX_EVENTS) count = TELOMERE_MAX_EVENTS;
    pattern_ensure_capacity(x, count);
    memcpy(x->pattern, new_pos, count * sizeof(t_float));
    if (new_vel) {
        memcpy(x->velocity, new_vel, count * sizeof(t_float));
    } else {
        for (int i = 0; i < count; i++) x->velocity[i] = 1.0f;
    }
    x->num_events = count;
}

void pattern_copy_to(t_telomere *x, t_float *pos_dest, t_float *vel_dest, int *count) {
    memcpy(pos_dest, x->pattern, x->num_events * sizeof(t_float));
    if (vel_dest)
        memcpy(vel_dest, x->velocity, x->num_events * sizeof(t_float));
    *count = x->num_events;
}

/* --- State queries --- */

t_float pattern_get_quantize_pct(t_telomere *x) {
    return x->quantize_pct;
}

int pattern_get_grid(t_telomere *x) {
    return x->grid;
}

t_float pattern_get_tempo(t_telomere *x) {
    return x->tempo;
}
