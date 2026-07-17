/* primase.c — Core dispatch and lifecycle for the primase Pd external
 *
 * primase records rhythmic tap-patterns as fractional positions within a
 * cycle (0.0–1.0) and plays them back, applying registered transforms.
 *
 * Architecture:
 *   source[]  — frozen recorded/imported positions; never touched by chain
 *   pattern[] — derived buffer: source → chain transforms applied in order
 *   playback  — reads pattern[]
 *
 * Recording and importing write to source then call primase_chain_eval()
 * to re-derive pattern[].  Direct transform messages (reverse, fast, etc.)
 * still mutate pattern[] directly for backward compatibility.
 */

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "primase.h"

#ifndef t_freemethod
typedef void (*t_freemethod)(void *);
#endif
#include "primase_transform.h"
#include "primase_pattern_api.h"

t_class *primase_class;
static t_class *primase_clock_proxy_class;

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */

static void primase_chain_eval(t_primase *x);
static void primase_set(t_primase *x, t_symbol *s, int argc, t_atom *argv);
static void primase_set_source(t_primase *x, t_symbol *s, int argc, t_atom *argv);

/* ------------------------------------------------------------------ */
/* Helper: compute swing-adjusted effective position for event i      */
/* ------------------------------------------------------------------ */

static double effective_pos(t_primase *x, int index) {
    if (index < 0 || index >= x->num_events) return 0.0;
    double p = (double)x->pattern[index];
    /* Phase 7: apply phase offset */
    p += (double)x->phase_offset;
    while (p >= 1.0) p -= 1.0;
    while (p <  0.0) p += 1.0;
    /* Phase 9: grid-aware swing */
    if (x->swing_amt != 0.0f && x->grid > 0) {
        double step = 1.0 / (double)x->grid;
        int nearest_grid = (int)(p / step + 0.5);
        if (nearest_grid % 2 == 1) {
            p += (double)x->swing_amt;
            while (p >= 1.0) p -= 1.0;
            while (p <  0.0) p += 1.0;
        }
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Playback clock callback                                            */
/* ------------------------------------------------------------------ */

static void primase_tick(t_primase *x) {
    /* Cycle end: play_index was advanced past the last event, or the
     * scheduled cycle-boundary callback fired after the last event. */
    if (x->play_index >= x->num_events) {
        outlet_float(x->out_count, (t_float)x->num_events);

        if (x->armed) {
            /* Cycle-quantized record arm: start recording now */
            x->armed     = 0;
            x->recording = 1;
            source_clear(x);
            x->cycle_start_time = clock_getlogicaltime();
            outlet_float(x->out_status, 1.0f);
            x->playing = 0;
            return;
        }

        if (x->loop) {
            if (x->num_events == 0) {
                x->playing = 0;
                x->play_index = 0;
                return;
            }
            x->play_index = 0;
            x->cycle_start_time = clock_getlogicaltime();
            double delay = effective_pos(x, 0) * x->cycle_length_ms;
            if (delay < 0.1) delay = 0.1;
            clock_delay(x->playback_clock, delay);
        } else {
            x->playing = 0;
            x->play_index = 0;
        }
        return;
    }

    double eff_pos = effective_pos(x, x->play_index);

    /* Phase 5: clamp modulation inputs */
    t_float accent = x->mod_accent;
    if (accent < 0.0f) accent = 0.0f;
    if (accent > 2.0f) accent = 2.0f;

    t_float jitter = x->jitter_amt;
    if (jitter < 0.0f) jitter = 0.0f;
    if (jitter > 1.0f) jitter = 1.0f;

    t_float skip_p = x->skip_prob;
    if (skip_p < 0.0f) skip_p = 0.0f;
    if (skip_p > 1.0f) skip_p = 1.0f;

    t_float swing = x->swing_amt;
    if (swing < -0.5f) swing = -0.5f;
    if (swing > 0.5f) swing = 0.5f;
    x->swing_amt = swing; /* write back clamped value for effective_pos */

    /* Apply per-event skip probability */
    t_float effective_skip = skip_p * x->skip_weight[x->play_index];
    if (effective_skip > 0.0f) {
        float r = (float)rand() / (float)RAND_MAX;
        if (r < effective_skip) {
            x->play_index++;
            if (x->play_index < x->num_events) {
                double next_eff = effective_pos(x, x->play_index);
                double delay = (next_eff - eff_pos) * x->cycle_length_ms;
                if (delay < 0.1) delay = 0.1;
                clock_delay(x->playback_clock, delay);
            } else if (x->loop || x->armed) {
                double remaining = (1.0 - eff_pos) * x->cycle_length_ms;
                if (remaining < 0.1) remaining = 0.1;
                clock_delay(x->playback_clock, remaining);
            } else {
                x->playing = 0;
                x->play_index = 0;
                outlet_float(x->out_count, (t_float)x->num_events);
            }
            return;
        }
    }

    /* Apply jitter to output position only (not scheduling) */
    t_float out_pos = (t_float)eff_pos;
    if (jitter > 0.0f) {
        float r = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        out_pos += r * jitter;
        while (out_pos >= 1.0f) out_pos -= 1.0f;
        while (out_pos <  0.0f) out_pos += 1.0f;
    }

    /* Output event — apply accent to velocity */
    t_float vel = pattern_get_velocity(x, x->play_index) * accent;
    if (vel > 1.0f) vel = 1.0f;
    outlet_float(x->out_velocity, vel);
    outlet_float(x->out_position, out_pos);
    outlet_bang(x->out_bang);

    /* Schedule next event */
    x->play_index++;
    if (x->play_index < x->num_events) {
        double next_eff = effective_pos(x, x->play_index);
        double delay = (next_eff - eff_pos) * x->cycle_length_ms;
        if (delay < 0.1) delay = 0.1;
        clock_delay(x->playback_clock, delay);
    } else {
        /* Last event fired — schedule cycle-end callback if needed */
        if (x->loop || x->armed) {
            double remaining = (1.0 - eff_pos) * x->cycle_length_ms;
            if (remaining < 0.1) remaining = 0.1;
            clock_delay(x->playback_clock, remaining);
        } else {
            x->playing = 0;
            x->play_index = 0;
            outlet_float(x->out_count, (t_float)x->num_events);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Bang — record a tap or trigger playback (inlet 1)                 */
/* ------------------------------------------------------------------ */

static void primase_bang(t_primase *x) {
    /* Phase 4: Overdub — append tap to source during playback */
    if (x->overdub && x->playing) {
        double elapsed = clock_gettimesince(x->cycle_start_time);
        t_float pos = (t_float)(elapsed / x->cycle_length_ms);
        while (pos >= 1.0f) pos -= 1.0f;
        if (pos < 0.0f) pos = 0.0f;

        if (x->quantize_pct > 0.0f && x->grid > 0) {
            t_float step    = 1.0f / (t_float)x->grid;
            t_float nearest = roundf(pos / step) * step;
            pos = pos + (nearest - pos) * x->quantize_pct;
            if (pos >= 1.0f) pos -= 1.0f;
        }

        t_float vel = x->current_velocity;

        /* Phase 4: Transparent — pass through taps to outlets */
        if (x->transparent) {
            outlet_float(x->out_velocity, vel);
            outlet_float(x->out_position, pos);
            outlet_bang(x->out_bang);
        }

        source_append_event(x, pos, vel);
        primase_chain_eval(x);
        outlet_float(x->out_count, (t_float)x->num_events);
        return;
    }

    if (x->recording) {
        double elapsed = clock_gettimesince(x->cycle_start_time);
        t_float pos = (t_float)(elapsed / x->cycle_length_ms);

        while (pos >= 1.0f) pos -= 1.0f;
        if (pos < 0.0f) pos = 0.0f;

        if (x->quantize_pct > 0.0f && x->grid > 0) {
            t_float step    = 1.0f / (t_float)x->grid;
            t_float nearest = roundf(pos / step) * step;
            pos = pos + (nearest - pos) * x->quantize_pct;
            if (pos >= 1.0f) pos -= 1.0f;
        }

        t_float vel = x->current_velocity;

        /* Phase 4: Transparent — pass through taps to outlets */
        if (x->transparent) {
            outlet_float(x->out_velocity, vel);
            outlet_float(x->out_position, pos);
            outlet_bang(x->out_bang);
        }

        source_append_event(x, pos, vel);
        primase_chain_eval(x);
        outlet_float(x->out_count, (t_float)x->num_events);
        return;
    }

    /* Trigger playback */
    if (x->num_events == 0) return;
    x->play_index = 0;
    x->playing    = 1;
    x->cycle_start_time = clock_getlogicaltime();
    double delay = effective_pos(x, 0) * x->cycle_length_ms;
    if (delay < 0.1) delay = 0.1;
    clock_delay(x->playback_clock, delay);
}

/* ------------------------------------------------------------------ */
/* Float — set tempo                                                  */
/* ------------------------------------------------------------------ */

static void primase_float(t_primase *x, t_float f) {
    if (f > 0.0f) {
        x->tempo = f;
        x->cycle_length_ms = (60000.0 / (double)f) * x->beats_per_cycle;
    }
}

/* ------------------------------------------------------------------ */
/* Chain evaluation                                                   */
/* ------------------------------------------------------------------ */

static void primase_chain_eval(t_primase *x) {
    pattern_replace(x, x->source, x->source_vel, x->source_count);

    for (int i = 0; i < x->chain_len; i++) {
        if (x->chain[i].bypassed) continue;
        t_transform_entry *entry = primase_lookup_transform(x->chain[i].name);
        if (!entry) continue;
        t_atom argv[4];
        for (int j = 0; j < x->chain[i].argc; j++)
            SETFLOAT(&argv[j], x->chain[i].argv[j]);
        entry->fn(x, x->chain[i].argc, argv);
    }
}

/* ------------------------------------------------------------------ */
/* Chain management (variable-arg variants, dispatched from anything) */
/* ------------------------------------------------------------------ */

static void do_chain_add(t_primase *x, int argc, t_atom *argv) {
    if (argc < 1) {
        pd_error(x, "primase: chain_add requires a transform name");
        return;
    }
    t_symbol *name = atom_getsymbolarg(0, argc, argv);
    if (!name) {
        pd_error(x, "primase: chain_add: first argument must be a symbol");
        return;
    }
    if (!primase_lookup_transform(name)) {
        pd_error(x, "primase: chain_add: unknown transform '%s'", name->s_name);
        return;
    }
    if (x->chain_len >= PRIMASE_MAX_CHAIN) {
        pd_error(x, "primase: chain is full (max %d entries)", PRIMASE_MAX_CHAIN);
        return;
    }
    t_chain_entry *e = &x->chain[x->chain_len];
    e->name     = name;
    e->bypassed = 0;
    e->argc     = 0;
    int nargs = argc - 1;
    if (nargs > 4) nargs = 4;
    for (int i = 0; i < nargs; i++) {
        e->argv[e->argc] = atom_getfloatarg(i + 1, argc, argv);
        e->argc++;
    }
    x->chain_len++;
    primase_chain_eval(x);
    post("primase: chain[%d] = %s", x->chain_len - 1, name->s_name);
}

static void do_chain_replace(t_primase *x, int argc, t_atom *argv) {
    if (argc < 2) {
        pd_error(x, "primase: chain_replace: requires <index> <transform> [args...]");
        return;
    }
    int idx = (int)atom_getfloatarg(0, argc, argv);
    t_symbol *name = atom_getsymbolarg(1, argc, argv);
    if (!name) {
        pd_error(x, "primase: chain_replace: second argument must be a symbol");
        return;
    }
    if (idx < 0 || idx >= x->chain_len) {
        pd_error(x, "primase: chain_replace: index %d out of range (len=%d)",
                 idx, x->chain_len);
        return;
    }
    if (!primase_lookup_transform(name)) {
        pd_error(x, "primase: chain_replace: unknown transform '%s'", name->s_name);
        return;
    }
    t_chain_entry *e = &x->chain[idx];
    e->name     = name;
    e->bypassed = 0;
    e->argc     = 0;
    int nargs = argc - 2;
    if (nargs > 4) nargs = 4;
    for (int i = 0; i < nargs; i++) {
        e->argv[e->argc] = atom_getfloatarg(i + 2, argc, argv);
        e->argc++;
    }
    primase_chain_eval(x);
}

/* ------------------------------------------------------------------ */
/* Chain management (fixed-arg variants, registered with class_addmethod) */
/* ------------------------------------------------------------------ */

static void primase_chain_remove(t_primase *x, t_float fidx) {
    int idx = (int)fidx;
    if (idx < 0 || idx >= x->chain_len) {
        pd_error(x, "primase: chain_remove: index %d out of range (len=%d)",
                 idx, x->chain_len);
        return;
    }
    for (int i = idx; i < x->chain_len - 1; i++)
        x->chain[i] = x->chain[i + 1];
    x->chain_len--;
    primase_chain_eval(x);
}

static void primase_chain_clear(t_primase *x) {
    x->chain_len = 0;
    primase_chain_eval(x);
}

static void primase_chain_bypass(t_primase *x, t_float fidx, t_float fbypass) {
    int idx = (int)fidx;
    if (idx < 0 || idx >= x->chain_len) {
        pd_error(x, "primase: chain_bypass: index %d out of range (len=%d)",
                 idx, x->chain_len);
        return;
    }
    x->chain[idx].bypassed = (fbypass != 0.0f) ? 1 : 0;
    primase_chain_eval(x);
}

static void primase_chain_dump(t_primase *x) {
    post("primase: chain (%d entr%s):", x->chain_len,
         x->chain_len == 1 ? "y" : "ies");
    for (int i = 0; i < x->chain_len; i++) {
        char args[64] = "";
        for (int j = 0; j < x->chain[i].argc; j++) {
            char buf[16];
            snprintf(buf, sizeof(buf), " %.4g", x->chain[i].argv[j]);
            strncat(args, buf, sizeof(args) - strlen(args) - 1);
        }
        post("  [%d]%s %s%s",
             i,
             x->chain[i].bypassed ? " (bypassed)" : "",
             x->chain[i].name->s_name,
             args);
    }
}

/* ------------------------------------------------------------------ */
/* Message dispatch — chain variable-arg messages then transform reg  */
/* ------------------------------------------------------------------ */

static void primase_anything(t_primase *x, t_symbol *s, int argc, t_atom *argv) {
    const char *n = s->s_name;

    if (strcmp(n, "chain_add")     == 0) { do_chain_add(x, argc, argv);     return; }
    if (strcmp(n, "chain_replace") == 0) { do_chain_replace(x, argc, argv); return; }
    if (strcmp(n, "set")           == 0) { primase_set(x, s, argc, argv);  return; }
    if (strcmp(n, "set_source")    == 0) { primase_set_source(x, s, argc, argv); return; }

    t_transform_entry *entry = primase_lookup_transform(s);
    if (!entry) {
        pd_error(x, "primase: unknown message '%s'", s->s_name);
        return;
    }
    if (argc < entry->min_args ||
        (entry->max_args >= 0 && argc > entry->max_args)) {
        pd_error(x, "primase: '%s' expects %d-%d args, got %d",
                 s->s_name, entry->min_args,
                 entry->max_args < 0 ? 999 : entry->max_args, argc);
        return;
    }
    entry->fn(x, argc, argv);
}

/* ------------------------------------------------------------------ */
/* Built-in messages                                                  */
/* ------------------------------------------------------------------ */

static void primase_record(t_primase *x) {
    if (x->playing) {
        /* Arm: start recording at next cycle boundary */
        x->armed = 1;
        outlet_float(x->out_status, 2.0f);
        return;
    }
    x->armed     = 0;
    x->recording = 1;
    source_clear(x);
    x->cycle_start_time = clock_getlogicaltime();
    outlet_float(x->out_status, 1.0f);
}

static void primase_clear(t_primase *x) {
    source_clear(x);
    primase_chain_eval(x);
    outlet_float(x->out_count, 0.0f);
}

static void primase_quantize(t_primase *x, t_float pct) {
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    x->quantize_pct = pct;
}

static void primase_grid(t_primase *x, t_float g) {
    int gi = (int)g;
    if (gi < 1) gi = 1;
    if (gi > 128) gi = 128;
    x->grid = gi;
}

static void primase_jitter(t_primase *x, t_float amt) {
    if (amt < 0.0f) amt = 0.0f;
    if (amt > 1.0f) amt = 1.0f;
    x->jitter_amt = amt;
}

static void primase_skip(t_primase *x, t_float prob) {
    if (prob < 0.0f) prob = 0.0f;
    if (prob > 1.0f) prob = 1.0f;
    x->skip_prob = prob;
}

static void primase_skipweight(t_primase *x, t_float fidx, t_float fw) {
    int idx = (int)fidx;
    if (idx < 0 || idx >= x->num_events) {
        pd_error(x, "primase: skipweight: index %d out of range (count=%d)",
                 idx, x->num_events);
        return;
    }
    if (fw < 0.0f) fw = 0.0f;
    if (fw > 1.0f) fw = 1.0f;
    x->skip_weight[idx] = fw;
}

static void primase_swing(t_primase *x, t_float amt) {
    if (amt < -0.5f) amt = -0.5f;
    if (amt >  0.5f) amt =  0.5f;
    x->swing_amt = amt;
}

static void primase_loop(t_primase *x, t_float f) {
    x->loop = (f != 0.0f) ? 1 : 0;
}

static void primase_sync(t_primase *x, t_float f) {
    x->sync_mode = (f != 0.0f) ? 1 : 0;
}

static void primase_beats(t_primase *x, t_float b) {
    int bi = (int)b;
    if (bi < 1) bi = 1;
    x->beats_per_cycle = bi;
    x->cycle_length_ms = (60000.0 / (double)x->tempo) * bi;
}

static void primase_dump(t_primase *x) {
    post("primase: %d events (source=%d chain=%d) tempo=%.1f grid=%d q=%.2f",
         x->num_events, x->source_count, x->chain_len,
         x->tempo, x->grid, x->quantize_pct);
    for (int i = 0; i < x->num_events; i++) {
        post("  [%d] pos=%.6f vel=%.3f sw=%.2f",
             i, x->pattern[i], x->velocity[i], x->skip_weight[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Build a filesystem path relative to current canvas directory       */
/* ------------------------------------------------------------------ */

static void make_filepath(char *out, size_t sz, t_symbol *sym) {
    const char *fn = sym->s_name;
    if (fn[0] == '/' || fn[0] == '~') {
        strncpy(out, fn, sz - 1);
        out[sz - 1] = '\0';
        return;
    }
    t_canvas *cv = canvas_getcurrent();
    t_symbol *dir = cv ? canvas_getdir(cv) : NULL;
    if (dir && dir->s_name && dir->s_name[0])
        snprintf(out, sz, "%s/%s", dir->s_name, fn);
    else
        strncpy(out, fn, sz - 1);
    out[sz - 1] = '\0';
}

static void primase_write(t_primase *x, t_symbol *sym) {
    char path[4096];
    make_filepath(path, sizeof(path), sym);
    FILE *f = fopen(path, "w");
    if (!f) { pd_error(x, "primase: write: cannot open '%s'", path); return; }
    for (int i = 0; i < x->num_events; i++)
        fprintf(f, "%.9g %.9g\n", x->pattern[i], x->velocity[i]);
    fclose(f);
    post("primase: wrote %d events to '%s'", x->num_events, path);
}

static void primase_read(t_primase *x, t_symbol *sym) {
    char path[4096];
    make_filepath(path, sizeof(path), sym);
    FILE *f = fopen(path, "r");
    if (!f) { pd_error(x, "primase: read: cannot open '%s'", path); return; }

    source_clear(x);
    float pos, vel;
    int loaded = 0;
    while (fscanf(f, "%f %f", &pos, &vel) == 2) {
        source_append_event(x, (t_float)pos, (t_float)vel);
        loaded++;
    }
    if (loaded == 0) {
        rewind(f);
        while (fscanf(f, "%f", &pos) == 1) {
            source_append_event(x, (t_float)pos, 1.0f);
            loaded++;
        }
    }
    fclose(f);
    post("primase: read %d events from '%s'", loaded, path);

    primase_chain_eval(x);

    if (x->playing && x->num_events > 0) {
        x->play_index = 0;
        x->cycle_start_time = clock_getlogicaltime();
        double metric_scale = (x->metric_num > 0.0f && x->metric_den > 0.0f)
                            ? (double)x->metric_den / (double)x->metric_num
                            : 1.0;
        double delay = effective_pos(x, 0) * x->cycle_length_ms * metric_scale;
        if (delay < 0.1) delay = 0.1;
        clock_unset(x->playback_clock);
        clock_delay(x->playback_clock, delay);
    } else {
        outlet_float(x->out_count, (t_float)x->num_events);
    }
}

static void primase_play(t_primase *x) {
    if (x->num_events == 0) return;
    x->play_index = 0;
    x->playing    = 1;
    x->cycle_start_time = clock_getlogicaltime();
    double delay = effective_pos(x, 0) * x->cycle_length_ms;
    if (delay < 0.1) delay = 0.1;
    clock_delay(x->playback_clock, delay);
}

static void primase_metric(t_primase *x, t_float num, t_float den) {
    if (num <= 0.0f || den <= 0.0f) {
        pd_error(x, "primase: metric ratio must be positive (got %g:%g)", num, den);
        return;
    }
    x->metric_num = num;
    x->metric_den = den;
}

static void primase_stop(t_primase *x) {
    x->armed = 0;
    x->overdub = 0;
    if (x->recording) {
        x->recording = 0;
        outlet_float(x->out_status, 0.0f);
    }
    if (x->playing) {
        clock_unset(x->playback_clock);
        x->playing = 0;
        outlet_float(x->out_status, 0.0f);
    }
}

static void primase_tempo(t_primase *x, t_float f) {
    if (f > 0.0f) {
        x->tempo = f;
        x->cycle_length_ms = (60000.0 / (double)f) * x->beats_per_cycle;
    }
}

static void primase_help_msg(t_primase *x) {
    post("primase -- available transforms:");
    t_transform_entry *cur = primase_get_registry_head();
    while (cur) {
        post("  %-16s  args: %d-%d  %s",
             cur->name->s_name, cur->min_args,
             cur->max_args < 0 ? 999 : cur->max_args, cur->description);
        cur = cur->next;
    }
    post("---");
    post("  record              start recording (arms if currently playing)");
    post("  stop                stop playback or recording; cancel arm");
    post("  play                start playback");
    post("  loop <0|1>          auto-restart at end of cycle");
    post("  sync <0|1>          bang = external clock reset (resets cycle phase)");
    post("  clear               clear pattern");
    post("  quantize <0-1>      quantize strength");
    post("  grid <n>            grid subdivisions per cycle");
    post("  jitter <0-1>        per-event random displacement");
    post("  skip <0-1>          global skip probability");
    post("  skipweight <i> <w>  per-event skip weight multiplier (0-1)");
    post("  swing <-0.5-0.5>    delay odd-indexed events (in 0-1 space)");
    post("  beats <n>           beats per cycle");
    post("  tempo <bpm>         set tempo");
    post("  metric <n> <d>      metric modulation ratio n:d");
    post("  write <file>        export pattern to file");
    post("  read  <file>        import pattern (playback continues)");
    post("  chain_add <t> [..] append transform to chain");
    post("  chain_remove <i>   remove chain entry at index i");
    post("  chain_replace <i> <t> [..]  replace chain entry");
    post("  chain_bypass <i> <0|1>      bypass/restore entry");
    post("  chain_clear        clear entire chain (restores source)");
    post("  chain_dump         print chain to console");
    post("  dump               print pattern to console");
    (void)x;
}

/* ------------------------------------------------------------------ */
/* Phase 1: Clock following messages                                  */
/* ------------------------------------------------------------------ */

static void primase_clockfollow(t_primase *x, t_float f) {
    x->clock_follow = (f != 0.0f) ? 1 : 0;
    if (x->clock_follow) {
        x->last_bang_time = 0.0;
        x->clock_bang_count = 0;
    }
}

static void primase_clockdiv(t_primase *x, t_float f) {
    int d = (int)f;
    if (d < 1) d = 1;
    x->clock_div = d;
    x->clock_bang_count = 0;
}

/* ------------------------------------------------------------------ */
/* Phase 2: State output query handlers                               */
/* ------------------------------------------------------------------ */

static void primase_getpattern(t_primase *x) {
    t_atom argv[PRIMASE_MAX_EVENTS];
    for (int i = 0; i < x->num_events; i++)
        SETFLOAT(&argv[i], x->pattern[i]);
    outlet_anything(x->out_state, gensym("pattern"), x->num_events, argv);
}

static void primase_getvelocity(t_primase *x) {
    t_atom argv[PRIMASE_MAX_EVENTS];
    for (int i = 0; i < x->num_events; i++)
        SETFLOAT(&argv[i], x->velocity[i]);
    outlet_anything(x->out_state, gensym("velocity"), x->num_events, argv);
}

static void primase_getsource(t_primase *x) {
    t_atom argv[PRIMASE_MAX_EVENTS];
    for (int i = 0; i < x->source_count; i++)
        SETFLOAT(&argv[i], x->source[i]);
    outlet_anything(x->out_state, gensym("source"), x->source_count, argv);
}

static void primase_getparams(t_primase *x) {
    t_atom a[1];
    SETFLOAT(&a[0], (t_float)x->playing);
    outlet_anything(x->out_state, gensym("playing"), 1, a);
    SETFLOAT(&a[0], (t_float)x->recording);
    outlet_anything(x->out_state, gensym("recording"), 1, a);
    SETFLOAT(&a[0], x->tempo);
    outlet_anything(x->out_state, gensym("tempo"), 1, a);
    SETFLOAT(&a[0], (t_float)x->loop);
    outlet_anything(x->out_state, gensym("loop"), 1, a);
    SETFLOAT(&a[0], (t_float)x->num_events);
    outlet_anything(x->out_state, gensym("num_events"), 1, a);
    SETFLOAT(&a[0], (t_float)x->cycle_length_ms);
    outlet_anything(x->out_state, gensym("cycle_length_ms"), 1, a);
    SETFLOAT(&a[0], x->jitter_amt);
    outlet_anything(x->out_state, gensym("jitter"), 1, a);
    SETFLOAT(&a[0], x->skip_prob);
    outlet_anything(x->out_state, gensym("skip"), 1, a);
    SETFLOAT(&a[0], x->swing_amt);
    outlet_anything(x->out_state, gensym("swing"), 1, a);
    SETFLOAT(&a[0], (t_float)x->grid);
    outlet_anything(x->out_state, gensym("grid"), 1, a);
    SETFLOAT(&a[0], x->quantize_pct);
    outlet_anything(x->out_state, gensym("quantize"), 1, a);
    SETFLOAT(&a[0], (t_float)x->beats_per_cycle);
    outlet_anything(x->out_state, gensym("beats"), 1, a);
    SETFLOAT(&a[0], (t_float)x->clock_follow);
    outlet_anything(x->out_state, gensym("clockfollow"), 1, a);
}

static void primase_getchain(t_primase *x) {
    for (int i = 0; i < x->chain_len; i++) {
        t_atom argv[6]; /* index + name + up to 4 args */
        int ac = 0;
        SETFLOAT(&argv[ac++], (t_float)i);
        SETSYMBOL(&argv[ac++], x->chain[i].name);
        for (int j = 0; j < x->chain[i].argc; j++)
            SETFLOAT(&argv[ac++], x->chain[i].argv[j]);
        outlet_anything(x->out_state, gensym("chain"), ac, argv);
    }
}

/* ------------------------------------------------------------------ */
/* Phase 3: Pattern I/O                                               */
/* ------------------------------------------------------------------ */

static void primase_set(t_primase *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    if (argc == 0) return;
    if (argc > PRIMASE_MAX_EVENTS) argc = PRIMASE_MAX_EVENTS;
    t_float pos[PRIMASE_MAX_EVENTS];
    for (int i = 0; i < argc; i++)
        pos[i] = atom_getfloatarg(i, argc, argv);
    pattern_replace(x, pos, NULL, argc);
    pattern_sort(x);
    outlet_float(x->out_count, (t_float)x->num_events);
}

static void primase_set_source(t_primase *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    if (argc == 0) return;
    if (argc > PRIMASE_MAX_EVENTS) argc = PRIMASE_MAX_EVENTS;
    t_float pos[PRIMASE_MAX_EVENTS];
    for (int i = 0; i < argc; i++)
        pos[i] = atom_getfloatarg(i, argc, argv);
    source_replace(x, pos, NULL, argc);
    primase_chain_eval(x);
    outlet_float(x->out_count, (t_float)x->num_events);
}

static void primase_tap_at(t_primase *x, t_float pos, t_float vel) {
    if (vel == 0.0f) vel = x->current_velocity;
    source_append_event(x, pos, vel);
    primase_chain_eval(x);
    outlet_float(x->out_count, (t_float)x->num_events);
}

/* ------------------------------------------------------------------ */
/* Phase 4: Overdub + Transparent                                     */
/* ------------------------------------------------------------------ */

static void primase_overdub(t_primase *x) {
    if (!x->playing) {
        pd_error(x, "primase: overdub requires active playback");
        return;
    }
    x->overdub = 1;
    x->recording = 0;  /* mutually exclusive with record */
    outlet_float(x->out_status, 3.0f);  /* 3 = overdub */
}

static void primase_transparent(t_primase *x, t_float f) {
    x->transparent = (f != 0.0f) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Phase 7: Phase offset                                              */
/* ------------------------------------------------------------------ */

static void primase_phase(t_primase *x, t_float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    x->phase_offset = f;
}

/* ------------------------------------------------------------------ */
/* Phase 8: Scene memory                                              */
/* ------------------------------------------------------------------ */

static void primase_store(t_primase *x, t_float fslot) {
    int slot = (int)fslot;
    if (slot < 0 || slot >= PRIMASE_MAX_SCENES) {
        pd_error(x, "primase: store: slot %d out of range (0-%d)",
                 slot, PRIMASE_MAX_SCENES - 1);
        return;
    }
    t_scene *sc = &x->scenes[slot];
    memcpy(sc->source, x->source, x->source_count * sizeof(t_float));
    memcpy(sc->source_vel, x->source_vel, x->source_count * sizeof(t_float));
    sc->source_count = x->source_count;
    memcpy(sc->chain, x->chain, x->chain_len * sizeof(t_chain_entry));
    sc->chain_len = x->chain_len;
    sc->occupied = 1;
    post("primase: stored to scene %d (%d events, %d chain entries)",
         slot, sc->source_count, sc->chain_len);
}

static void primase_recall(t_primase *x, t_float fslot) {
    int slot = (int)fslot;
    if (slot < 0 || slot >= PRIMASE_MAX_SCENES) {
        pd_error(x, "primase: recall: slot %d out of range (0-%d)",
                 slot, PRIMASE_MAX_SCENES - 1);
        return;
    }
    t_scene *sc = &x->scenes[slot];
    if (!sc->occupied) {
        pd_error(x, "primase: recall: scene %d is empty", slot);
        return;
    }
    source_replace(x, sc->source, sc->source_vel, sc->source_count);
    memcpy(x->chain, sc->chain, sc->chain_len * sizeof(t_chain_entry));
    x->chain_len = sc->chain_len;
    primase_chain_eval(x);

    if (x->playing && x->num_events > 0) {
        x->play_index = 0;
        x->cycle_start_time = clock_getlogicaltime();
        double delay = effective_pos(x, 0) * x->cycle_length_ms;
        if (delay < 0.1) delay = 0.1;
        clock_unset(x->playback_clock);
        clock_delay(x->playback_clock, delay);
    }
    outlet_float(x->out_count, (t_float)x->num_events);
    post("primase: recalled scene %d (%d events, %d chain entries)",
         slot, x->source_count, x->chain_len);
}

/* ------------------------------------------------------------------ */
/* Clock proxy bang handler (inlet 2)                                 */
/* ------------------------------------------------------------------ */

static void primase_clock_proxy_bang(t_primase_clock_proxy *p) {
    t_primase *x = p->x;

    /* Clock following — derive tempo from bang intervals */
    if (x->clock_follow && x->playing) {
        double now = clock_getlogicaltime();
        if (x->last_bang_time > 0.0) {
            double interval_units = now - x->last_bang_time;
            double interval_ms = interval_units / 14112.0;
            if (interval_ms > 0.0) {
                x->tempo = (t_float)(60000.0 / (interval_ms * x->clock_div));
                x->cycle_length_ms = (60000.0 / (double)x->tempo) * x->beats_per_cycle;
            }
        }
        x->last_bang_time = now;
        x->clock_bang_count++;
        if (x->clock_bang_count >= x->clock_div) {
            /* Reset cycle at clock_div boundary */
            x->clock_bang_count = 0;
            if (x->num_events == 0) {
                clock_unset(x->playback_clock);
                x->playing = 0;
                x->play_index = 0;
                return;
            }
            clock_unset(x->playback_clock);
            x->play_index = 0;
            x->cycle_start_time = clock_getlogicaltime();
            double delay = effective_pos(x, 0) * x->cycle_length_ms;
            if (delay < 0.1) delay = 0.1;
            clock_delay(x->playback_clock, delay);
        }
        return;
    }

    /* External sync: reset cycle phase to now */
    if (x->sync_mode && x->playing) {
        if (x->num_events == 0) {
            clock_unset(x->playback_clock);
            x->playing = 0;
            x->play_index = 0;
            return;
        }
        clock_unset(x->playback_clock);
        x->play_index = 0;
        x->cycle_start_time = clock_getlogicaltime();
        double delay = effective_pos(x, 0) * x->cycle_length_ms;
        if (delay < 0.1) delay = 0.1;
        clock_delay(x->playback_clock, delay);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Constructor / Destructor                                           */
/* ------------------------------------------------------------------ */

static void *primase_new(t_float tempo) {
    t_primase *x = (t_primase *)pd_new(primase_class);

    /* Derived pattern storage */
    x->pattern_alloc = 32;
    x->pattern     = (t_float *)getbytes(x->pattern_alloc * sizeof(t_float));
    x->velocity    = (t_float *)getbytes(x->pattern_alloc * sizeof(t_float));
    x->skip_weight = (t_float *)getbytes(x->pattern_alloc * sizeof(t_float));
    x->num_events  = 0;

    /* Source pattern storage */
    x->source_alloc = 32;
    x->source     = (t_float *)getbytes(x->source_alloc * sizeof(t_float));
    x->source_vel = (t_float *)getbytes(x->source_alloc * sizeof(t_float));
    x->source_count = 0;

    /* Chain */
    x->chain_len = 0;

    x->current_velocity = 1.0f;

    /* Euclidean */
    x->euclid_pattern = NULL;
    x->euclid_len     = 0;

    /* Quantization */
    x->quantize_pct = 0.0f;
    x->grid         = PRIMASE_DEFAULT_GRID;

    /* Clock */
    x->tempo            = (tempo > 0.0f) ? tempo : PRIMASE_DEFAULT_TEMPO;
    x->beats_per_cycle  = 4;
    x->cycle_length_ms  = (60000.0 / (double)x->tempo) * x->beats_per_cycle;
    x->cycle_start_time = 0.0;

    /* Metric modulation */
    x->metric_num = 1.0f;
    x->metric_den = 1.0f;

    /* Clock following */
    x->clock_follow     = 0;
    x->last_bang_time   = 0.0;
    x->clock_div        = 1;
    x->clock_bang_count = 0;

    /* Playback */
    x->recording  = 0;
    x->armed      = 0;
    x->playing    = 0;
    x->play_index = 0;
    x->loop       = 0;
    x->sync_mode  = 0;

    /* Overdub / transparent */
    x->overdub     = 0;
    x->transparent = 0;

    /* Variation */
    x->jitter_amt = 0.0f;
    x->skip_prob  = 0.0f;
    x->swing_amt  = 0.0f;

    /* Modulation */
    x->mod_accent = 1.0f;

    /* Phase offset */
    x->phase_offset = 0.0f;

    /* Scene memory */
    memset(x->scenes, 0, sizeof(x->scenes));

    /* Outlets: bang, position, velocity, count, status, state */
    x->out_bang     = outlet_new(&x->x_obj, gensym("bang"));
    x->out_position = outlet_new(&x->x_obj, gensym("float"));
    x->out_velocity = outlet_new(&x->x_obj, gensym("float"));
    x->out_count    = outlet_new(&x->x_obj, gensym("float"));
    x->out_status   = outlet_new(&x->x_obj, gensym("float"));
    x->out_state    = outlet_new(&x->x_obj, &s_list);

    x->playback_clock = clock_new(x, (t_method)primase_tick);

    /* Inlet 2: clock proxy (dedicated clock/sync inlet) */
    x->clock_proxy.x = x;
    x->clock_proxy.pd = primase_clock_proxy_class;
    x->clock_inlet = inlet_new(&x->x_obj, &x->clock_proxy.pd, 0, 0);

    /* Inlet 3: velocity */
    floatinlet_new(&x->x_obj, &x->current_velocity);
    /* Inlet 4: accent modulation */
    floatinlet_new(&x->x_obj, &x->mod_accent);
    /* Inlet 5: jitter amount */
    floatinlet_new(&x->x_obj, &x->jitter_amt);
    /* Inlet 6: skip probability */
    floatinlet_new(&x->x_obj, &x->skip_prob);
    /* Inlet 7: swing amount */
    floatinlet_new(&x->x_obj, &x->swing_amt);

    x->f_inlet = 0.0f;

    return x;
}

static void primase_free(t_primase *x) {
    clock_free(x->playback_clock);
    if (x->pattern)
        freebytes(x->pattern,     x->pattern_alloc * sizeof(t_float));
    if (x->velocity)
        freebytes(x->velocity,    x->pattern_alloc * sizeof(t_float));
    if (x->skip_weight)
        freebytes(x->skip_weight, x->pattern_alloc * sizeof(t_float));
    if (x->source)
        freebytes(x->source,      x->source_alloc * sizeof(t_float));
    if (x->source_vel)
        freebytes(x->source_vel,  x->source_alloc * sizeof(t_float));
    if (x->euclid_pattern)
        freebytes(x->euclid_pattern, x->euclid_len * sizeof(int));
}

/* ------------------------------------------------------------------ */
/* Setup                                                              */
/* ------------------------------------------------------------------ */

EXTERN void primase_setup(void) {
    /* Clock proxy class for dedicated clock inlet */
    primase_clock_proxy_class = class_new(gensym("primase_clock_proxy"),
        0, 0, sizeof(t_primase_clock_proxy), CLASS_PD, 0);
    class_addbang(primase_clock_proxy_class,
        (t_method)primase_clock_proxy_bang);

    primase_class = class_new(
        gensym("primase"),
        (t_newmethod)primase_new,
        (t_freemethod)primase_free,
        sizeof(t_primase),
        CLASS_DEFAULT,
        A_DEFFLOAT, 0
    );

    class_addbang   (primase_class, (t_method)primase_bang);
    class_addfloat  (primase_class, (t_method)primase_float);
    class_addanything(primase_class, (t_method)primase_anything);

    class_addmethod(primase_class, (t_method)primase_record,
                    gensym("record"), 0);
    class_addmethod(primase_class, (t_method)primase_stop,
                    gensym("stop"), 0);
    class_addmethod(primase_class, (t_method)primase_play,
                    gensym("play"), 0);
    class_addmethod(primase_class, (t_method)primase_loop,
                    gensym("loop"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_sync,
                    gensym("sync"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_clear,
                    gensym("clear"), 0);
    class_addmethod(primase_class, (t_method)primase_quantize,
                    gensym("quantize"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_grid,
                    gensym("grid"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_jitter,
                    gensym("jitter"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_skip,
                    gensym("skip"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_skipweight,
                    gensym("skipweight"), A_FLOAT, A_FLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_swing,
                    gensym("swing"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_beats,
                    gensym("beats"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_tempo,
                    gensym("tempo"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_metric,
                    gensym("metric"), A_FLOAT, A_FLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_dump,
                    gensym("dump"), 0);
    class_addmethod(primase_class, (t_method)primase_write,
                    gensym("write"), A_SYMBOL, 0);
    class_addmethod(primase_class, (t_method)primase_read,
                    gensym("read"), A_SYMBOL, 0);
    class_addmethod(primase_class, (t_method)primase_help_msg,
                    gensym("help"), 0);

    /* Phase 1: Clock following */
    class_addmethod(primase_class, (t_method)primase_clockfollow,
                    gensym("clockfollow"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_clockdiv,
                    gensym("clockdiv"), A_DEFFLOAT, 0);

    /* Phase 2: State output */
    class_addmethod(primase_class, (t_method)primase_getpattern,
                    gensym("getpattern"), 0);
    class_addmethod(primase_class, (t_method)primase_getvelocity,
                    gensym("getvelocity"), 0);
    class_addmethod(primase_class, (t_method)primase_getsource,
                    gensym("getsource"), 0);
    class_addmethod(primase_class, (t_method)primase_getparams,
                    gensym("getparams"), 0);
    class_addmethod(primase_class, (t_method)primase_getchain,
                    gensym("getchain"), 0);

    /* Phase 3: Pattern I/O (set/set_source via anything; tap_at here) */
    class_addmethod(primase_class, (t_method)primase_tap_at,
                    gensym("tap_at"), A_FLOAT, A_DEFFLOAT, 0);

    /* Phase 4: Overdub + transparent */
    class_addmethod(primase_class, (t_method)primase_overdub,
                    gensym("overdub"), 0);
    class_addmethod(primase_class, (t_method)primase_transparent,
                    gensym("transparent"), A_DEFFLOAT, 0);

    /* Phase 7: Phase offset */
    class_addmethod(primase_class, (t_method)primase_phase,
                    gensym("phase"), A_DEFFLOAT, 0);

    /* Phase 8: Scene memory */
    class_addmethod(primase_class, (t_method)primase_store,
                    gensym("store"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_recall,
                    gensym("recall"), A_DEFFLOAT, 0);

    /* Chain management — fixed-arg variants */
    class_addmethod(primase_class, (t_method)primase_chain_remove,
                    gensym("chain_remove"), A_DEFFLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_chain_clear,
                    gensym("chain_clear"), 0);
    class_addmethod(primase_class, (t_method)primase_chain_bypass,
                    gensym("chain_bypass"), A_FLOAT, A_FLOAT, 0);
    class_addmethod(primase_class, (t_method)primase_chain_dump,
                    gensym("chain_dump"), 0);

    primase_transforms_builtins_setup();

    post("primase: tap-pattern sequencer loaded");
}
