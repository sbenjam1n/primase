/* telomere.h — Main struct definition for the telomere Pd external */
#ifndef TELOMERE_H
#define TELOMERE_H

#include "m_pd.h"

#define TELOMERE_MAX_EVENTS   256
#define TELOMERE_DEFAULT_GRID 16
#define TELOMERE_DEFAULT_TEMPO 120.0f
#define TELOMERE_MAX_CHAIN    16
#define TELOMERE_MAX_SCENES   8

/* One entry in the non-destructive transform chain.
 * argc/argv store the arguments so the chain can be re-evaluated from
 * the frozen source pattern at any time. */
typedef struct _chain_entry {
    t_symbol *name;         /* transform name (e.g. "fast", "reverse")     */
    int       argc;         /* number of stored float arguments (0–4)      */
    t_float   argv[4];      /* argument values                             */
    int       bypassed;     /* 1 = skip this entry during chain evaluation */
} t_chain_entry;

/* Scene memory slot for store/recall during improvisation */
typedef struct _scene {
    t_float  source[TELOMERE_MAX_EVENTS];
    t_float  source_vel[TELOMERE_MAX_EVENTS];
    int      source_count;
    t_chain_entry chain[TELOMERE_MAX_CHAIN];
    int      chain_len;
    int      occupied;  /* 0 = empty, 1 = has data */
} t_scene;

typedef struct _telomere {
    t_object  x_obj;

    /* --- Derived pattern (source + chain applied; read by playback) --- */
    t_float  *pattern;          /* dynamically allocated position buffer   */
    t_float  *velocity;         /* parallel velocity buffer (0.0–1.0)      */
    t_float  *skip_weight;      /* per-event skip probability weight (0–1) */
    int       num_events;       /* current number of derived events        */
    int       pattern_alloc;    /* allocated capacity                      */

    /* --- Source pattern (frozen; never mutated by chain transforms) --- */
    t_float  *source;           /* recorded/imported positions             */
    t_float  *source_vel;       /* recorded/imported velocities            */
    int       source_count;     /* number of source events                 */
    int       source_alloc;     /* allocated capacity for source buffers   */

    /* --- Transform chain --- */
    t_chain_entry chain[TELOMERE_MAX_CHAIN];
    int           chain_len;    /* number of active chain entries          */

    /* --- Velocity input --- */
    t_float   current_velocity; /* latched from velocity inlet, default 1.0 */

    /* --- Euclidean pattern --- */
    int      *euclid_pattern;   /* boolean hits for euclidean rhythm       */
    int       euclid_len;       /* length of euclidean pattern             */

    /* --- Quantization --- */
    t_float   quantize_pct;     /* 0.0 = free, 1.0 = fully quantized      */
    int       grid;             /* grid subdivisions per cycle             */

    /* --- Clock / timing --- */
    t_float   tempo;            /* BPM                                     */
    double    cycle_length_ms;  /* derived: (60000 / tempo) * beats/cycle  */
    double    cycle_start_time; /* logical time of current cycle start     */
    int       beats_per_cycle;  /* how many beats form one cycle           */

    /* --- Clock following (Phase 1) --- */
    int       clock_follow;     /* 0 = manual tempo, 1 = derive from bangs */
    double    last_bang_time;   /* logical time of previous clock bang      */
    int       clock_div;        /* bangs per cycle (1 = 1 bang = 1 cycle)  */
    int       clock_bang_count; /* counts bangs within a cycle             */

    /* --- Metric modulation --- */
    t_float   metric_num;       /* numerator for metric modulation ratio   */
    t_float   metric_den;       /* denominator for metric modulation ratio */

    /* --- Playback / recording state --- */
    int       recording;        /* 1 = recording taps, 0 = idle            */
    int       armed;            /* 1 = start recording at next cycle end   */
    int       playing;          /* 1 = playback clock is running           */
    int       loop;             /* 1 = auto-restart at cycle end           */
    int       sync_mode;        /* 1 = bang resets cycle phase (ext clock) */

    /* --- Overdub / transparent (Phase 4) --- */
    int       overdub;          /* 1 = overdub mode active                 */
    int       transparent;      /* 1 = pass through taps to outlets        */

    /* --- Variation parameters --- */
    t_float   jitter_amt;       /* random displacement amount (0.0–1.0)    */
    t_float   skip_prob;        /* base probability of skipping an event   */
    t_float   swing_amt;        /* position offset applied to odd-indexed
                                 * events at output/scheduling time        */

    /* --- Modulation inlet targets (Phase 5) --- */
    t_float   mod_accent;       /* velocity multiplier from inlet, def 1.0 */

    /* --- Phase offset (Phase 7) --- */
    t_float   phase_offset;     /* 0.0-1.0, shifts cycle start             */

    /* --- Outlets --- */
    t_outlet *out_bang;         /* fires a bang per event during playback  */
    t_outlet *out_position;     /* outputs event position as float         */
    t_outlet *out_velocity;     /* outputs event velocity as float         */
    t_outlet *out_count;        /* outputs current event count             */
    t_outlet *out_status;       /* outputs status: 0=idle 1=rec 2=armed    */
    t_outlet *out_state;        /* state query outlet (Phase 2)            */

    /* --- Scene memory (Phase 8) --- */
    t_scene   scenes[TELOMERE_MAX_SCENES];

    /* --- Clock object --- */
    t_clock  *playback_clock;   /* clock for scheduling playback events    */
    int       play_index;       /* current index during playback           */

    /* --- Inlet value --- */
    t_float   f_inlet;          /* dummy float inlet                       */
} t_telomere;

/* Externally visible class pointer */
extern t_class *telomere_class;

/* Setup function */
EXTERN void telomere_setup(void);

#endif /* TELOMERE_H */
