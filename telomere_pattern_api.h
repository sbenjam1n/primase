/* telomere_pattern_api.h — Pattern manipulation API for transforms
 *
 * INVARIANT: All transforms MUST leave the pattern in sorted order
 * (positions ascending 0.0–1.0).  Call pattern_sort() after any
 * operation that may reorder positions.  Failure to sort will cause
 * events to fire out of order silently during playback.
 */
#ifndef TELOMERE_PATTERN_API_H
#define TELOMERE_PATTERN_API_H

#include "telomere.h"

/* --- Read access (derived pattern) --- */
int      pattern_num_events(t_telomere *x);
t_float  pattern_get_event(t_telomere *x, int index);
t_float  pattern_get_velocity(t_telomere *x, int index);
t_float  pattern_get_skip_weight(t_telomere *x, int index);
t_float *pattern_get_buffer(t_telomere *x);

/* --- Write access (derived pattern) --- */
void     pattern_set_event(t_telomere *x, int index, t_float value);
void     pattern_set_velocity(t_telomere *x, int index, t_float vel);
void     pattern_set_skip_weight(t_telomere *x, int index, t_float w);
void     pattern_append_event(t_telomere *x, t_float value, t_float vel);
void     pattern_resize(t_telomere *x, int new_size);
void     pattern_clear(t_telomere *x);
void     pattern_sort(t_telomere *x);   /* carries velocity+skip_weight */

/* --- Bulk operations (derived pattern) --- */
void     pattern_replace(t_telomere *x, t_float *new_pos, t_float *new_vel, int count);
void     pattern_copy_to(t_telomere *x, t_float *pos_dest, t_float *vel_dest, int *count);

/* --- Source pattern operations --- */
void     source_clear(t_telomere *x);
void     source_append_event(t_telomere *x, t_float pos, t_float vel);
void     source_replace(t_telomere *x, t_float *pos, t_float *vel, int count);
int      source_num_events(t_telomere *x);

/* --- State queries --- */
t_float  pattern_get_quantize_pct(t_telomere *x);
int      pattern_get_grid(t_telomere *x);
t_float  pattern_get_tempo(t_telomere *x);

#endif /* TELOMERE_PATTERN_API_H */
