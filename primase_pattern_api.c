/* primase_pattern_api.c — Pattern manipulation API implementation */

#include "primase_pattern_api.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static void pattern_ensure_capacity(t_primase *x, int needed) {
    if (needed <= x->pattern_alloc) return;
    int new_alloc = x->pattern_alloc;
    while (new_alloc < needed)
        new_alloc = (new_alloc < 16) ? 16 : new_alloc * 2;
    if (new_alloc > PRIMASE_MAX_EVENTS) new_alloc = PRIMASE_MAX_EVENTS;
    x->pattern     = (t_float *)resizebytes(x->pattern,
        x->pattern_alloc * sizeof(t_float), new_alloc * sizeof(t_float));
    x->velocity    = (t_float *)resizebytes(x->velocity,
        x->pattern_alloc * sizeof(t_float), new_alloc * sizeof(t_float));
    x->skip_weight = (t_float *)resizebytes(x->skip_weight,
        x->pattern_alloc * sizeof(t_float), new_alloc * sizeof(t_float));
    x->pattern_alloc = new_alloc;
}

static void source_ensure_capacity(t_primase *x, int needed) {
    if (needed <= x->source_alloc) return;
    int new_alloc = x->source_alloc;
    while (new_alloc < needed)
        new_alloc = (new_alloc < 16) ? 16 : new_alloc * 2;
    if (new_alloc > PRIMASE_MAX_EVENTS) new_alloc = PRIMASE_MAX_EVENTS;
    x->source     = (t_float *)resizebytes(x->source,
        x->source_alloc * sizeof(t_float), new_alloc * sizeof(t_float));
    x->source_vel = (t_float *)resizebytes(x->source_vel,
        x->source_alloc * sizeof(t_float), new_alloc * sizeof(t_float));
    x->source_alloc = new_alloc;
}

/* ------------------------------------------------------------------ */
/* Read access — derived pattern                                      */
/* ------------------------------------------------------------------ */

int pattern_num_events(t_primase *x) {
    return x->num_events;
}

t_float pattern_get_event(t_primase *x, int index) {
    if (index < 0 || index >= x->num_events) return 0.0f;
    return x->pattern[index];
}

t_float pattern_get_velocity(t_primase *x, int index) {
    if (index < 0 || index >= x->num_events) return 1.0f;
    return x->velocity[index];
}

t_float pattern_get_skip_weight(t_primase *x, int index) {
    if (index < 0 || index >= x->num_events) return 1.0f;
    return x->skip_weight[index];
}

t_float *pattern_get_buffer(t_primase *x) {
    return x->pattern;
}

/* ------------------------------------------------------------------ */
/* Write access — derived pattern                                     */
/* ------------------------------------------------------------------ */

void pattern_set_event(t_primase *x, int index, t_float value) {
    if (index < 0 || index >= x->num_events) return;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    x->pattern[index] = value;
}

void pattern_set_velocity(t_primase *x, int index, t_float vel) {
    if (index < 0 || index >= x->num_events) return;
    if (vel < 0.0f) vel = 0.0f;
    if (vel > 1.0f) vel = 1.0f;
    x->velocity[index] = vel;
}

void pattern_set_skip_weight(t_primase *x, int index, t_float w) {
    if (index < 0 || index >= x->num_events) return;
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    x->skip_weight[index] = w;
}

void pattern_append_event(t_primase *x, t_float value, t_float vel) {
    if (x->num_events >= PRIMASE_MAX_EVENTS) return;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    if (vel   < 0.0f) vel   = 0.0f;
    if (vel   > 1.0f) vel   = 1.0f;
    pattern_ensure_capacity(x, x->num_events + 1);
    x->pattern    [x->num_events] = value;
    x->velocity   [x->num_events] = vel;
    x->skip_weight[x->num_events] = 1.0f;
    x->num_events++;
}

void pattern_resize(t_primase *x, int new_size) {
    if (new_size < 0) new_size = 0;
    if (new_size > PRIMASE_MAX_EVENTS) new_size = PRIMASE_MAX_EVENTS;
    pattern_ensure_capacity(x, new_size);
    if (new_size > x->num_events) {
        memset(x->pattern + x->num_events, 0,
               (new_size - x->num_events) * sizeof(t_float));
        for (int i = x->num_events; i < new_size; i++) {
            x->velocity   [i] = 1.0f;
            x->skip_weight[i] = 1.0f;
        }
    }
    x->num_events = new_size;
    if (x->playing && new_size > 0 && x->play_index >= new_size)
        x->play_index = 0;
}

void pattern_clear(t_primase *x) {
    x->num_events = 0;
    x->play_index = 0;
}

/* Insertion sort — carries velocity and skip_weight alongside position */
void pattern_sort(t_primase *x) {
    int n = x->num_events;
    t_float *p = x->pattern;
    t_float *v = x->velocity;
    t_float *w = x->skip_weight;
    for (int i = 1; i < n; i++) {
        t_float kp = p[i];
        t_float kv = v[i];
        t_float kw = w[i];
        int j = i - 1;
        while (j >= 0 && p[j] > kp) {
            p[j + 1] = p[j];
            v[j + 1] = v[j];
            w[j + 1] = w[j];
            j--;
        }
        p[j + 1] = kp;
        v[j + 1] = kv;
        w[j + 1] = kw;
    }
}

/* ------------------------------------------------------------------ */
/* Bulk operations — derived pattern                                  */
/* ------------------------------------------------------------------ */

void pattern_replace(t_primase *x, t_float *new_pos, t_float *new_vel, int count) {
    if (count < 0) count = 0;
    if (count > PRIMASE_MAX_EVENTS) count = PRIMASE_MAX_EVENTS;
    pattern_ensure_capacity(x, count);
    memcpy(x->pattern, new_pos, count * sizeof(t_float));
    if (new_vel)
        memcpy(x->velocity, new_vel, count * sizeof(t_float));
    else
        for (int i = 0; i < count; i++) x->velocity[i] = 1.0f;
    /* Reset skip_weight to 1.0 for all events (fresh derived state) */
    for (int i = 0; i < count; i++) x->skip_weight[i] = 1.0f;
    x->num_events = count;
    if (x->playing && count > 0 && x->play_index >= count)
        x->play_index = 0;
}

void pattern_copy_to(t_primase *x, t_float *pos_dest, t_float *vel_dest, int *count) {
    memcpy(pos_dest, x->pattern, x->num_events * sizeof(t_float));
    if (vel_dest)
        memcpy(vel_dest, x->velocity, x->num_events * sizeof(t_float));
    *count = x->num_events;
}

/* ------------------------------------------------------------------ */
/* Source pattern operations                                          */
/* ------------------------------------------------------------------ */

void source_clear(t_primase *x) {
    x->source_count = 0;
}

void source_append_event(t_primase *x, t_float pos, t_float vel) {
    if (x->source_count >= PRIMASE_MAX_EVENTS) return;
    if (pos < 0.0f) pos = 0.0f;
    if (pos > 1.0f) pos = 1.0f;
    if (vel < 0.0f) vel = 0.0f;
    if (vel > 1.0f) vel = 1.0f;
    source_ensure_capacity(x, x->source_count + 1);
    x->source    [x->source_count] = pos;
    x->source_vel[x->source_count] = vel;
    x->source_count++;
}

void source_replace(t_primase *x, t_float *pos, t_float *vel, int count) {
    if (count < 0) count = 0;
    if (count > PRIMASE_MAX_EVENTS) count = PRIMASE_MAX_EVENTS;
    source_ensure_capacity(x, count);
    memcpy(x->source, pos, count * sizeof(t_float));
    if (vel)
        memcpy(x->source_vel, vel, count * sizeof(t_float));
    else
        for (int i = 0; i < count; i++) x->source_vel[i] = 1.0f;
    x->source_count = count;
}

int source_num_events(t_primase *x) {
    return x->source_count;
}

/* ------------------------------------------------------------------ */
/* State queries                                                      */
/* ------------------------------------------------------------------ */

t_float pattern_get_quantize_pct(t_primase *x) {
    return x->quantize_pct;
}

int pattern_get_grid(t_primase *x) {
    return x->grid;
}

t_float pattern_get_tempo(t_primase *x) {
    return x->tempo;
}
