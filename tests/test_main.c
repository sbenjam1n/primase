/* tests/test_main.c — Unit tests for telomere pattern API and transforms.
 *
 * Tests are deliberately standalone: no Pd runtime, no clock, no outlets.
 * Each test creates a minimal t_telomere on the heap, exercises the API,
 * and frees it.  See tests/pd_stub.c for the runtime stubs.
 *
 * Run: make test_unit
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "m_pd.h"
#include "telomere.h"
#include "telomere_pattern_api.h"
#include "telomere_transform.h"

/* ------------------------------------------------------------------ */
/* Minimal test harness                                               */
/* ------------------------------------------------------------------ */

static int g_passes  = 0;
static int g_failures = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL [%s:%d]: %s\n", __func__, __LINE__, (msg)); \
        g_failures++; \
    } else { \
        g_passes++; \
    } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b, eps, msg) \
    ASSERT(fabsf((float)(a) - (float)(b)) < (float)(eps), msg)

/* ------------------------------------------------------------------ */
/* Helper: allocate a minimal t_telomere for testing                  */
/* ------------------------------------------------------------------ */

static t_telomere *new_telo(void) {
    t_telomere *x = (t_telomere *)calloc(1, sizeof(t_telomere));
    x->pattern_alloc = 32;
    x->pattern     = (t_float *)getbytes(32 * sizeof(t_float));
    x->velocity    = (t_float *)getbytes(32 * sizeof(t_float));
    x->skip_weight = (t_float *)getbytes(32 * sizeof(t_float));
    x->num_events  = 0;
    x->source_alloc = 32;
    x->source      = (t_float *)getbytes(32 * sizeof(t_float));
    x->source_vel  = (t_float *)getbytes(32 * sizeof(t_float));
    x->source_count = 0;
    x->chain_len   = 0;
    x->grid        = 16;
    x->quantize_pct = 0.0f;
    x->tempo       = 120.0f;
    return x;
}

static void free_telo(t_telomere *x) {
    freebytes(x->pattern,     x->pattern_alloc * sizeof(t_float));
    freebytes(x->velocity,    x->pattern_alloc * sizeof(t_float));
    freebytes(x->skip_weight, x->pattern_alloc * sizeof(t_float));
    freebytes(x->source,      x->source_alloc  * sizeof(t_float));
    freebytes(x->source_vel,  x->source_alloc  * sizeof(t_float));
    free(x);
}

/* ------------------------------------------------------------------ */
/* pattern_api tests                                                  */
/* ------------------------------------------------------------------ */

static void test_append_and_count(void) {
    t_telomere *x = new_telo();
    ASSERT(pattern_num_events(x) == 0, "empty count == 0");
    pattern_append_event(x, 0.25f, 0.8f);
    pattern_append_event(x, 0.75f, 0.5f);
    ASSERT(pattern_num_events(x) == 2, "count after 2 appends");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.25f, 1e-5f, "event[0] pos");
    ASSERT_FLOAT_EQ(pattern_get_velocity(x, 0), 0.8f, 1e-5f, "event[0] vel");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 1), 0.75f, 1e-5f, "event[1] pos");
    free_telo(x);
}

static void test_skip_weight_default(void) {
    t_telomere *x = new_telo();
    pattern_append_event(x, 0.5f, 1.0f);
    ASSERT_FLOAT_EQ(pattern_get_skip_weight(x, 0), 1.0f, 1e-5f,
                    "new event default skip_weight == 1.0");
    free_telo(x);
}

static void test_skip_weight_set(void) {
    t_telomere *x = new_telo();
    pattern_append_event(x, 0.1f, 1.0f);
    pattern_append_event(x, 0.5f, 1.0f);
    pattern_set_skip_weight(x, 1, 0.3f);
    ASSERT_FLOAT_EQ(pattern_get_skip_weight(x, 0), 1.0f, 1e-5f, "sw[0] unchanged");
    ASSERT_FLOAT_EQ(pattern_get_skip_weight(x, 1), 0.3f, 1e-5f, "sw[1] == 0.3");
    free_telo(x);
}

static void test_clear(void) {
    t_telomere *x = new_telo();
    pattern_append_event(x, 0.1f, 1.0f);
    pattern_append_event(x, 0.5f, 1.0f);
    pattern_clear(x);
    ASSERT(pattern_num_events(x) == 0, "count after clear == 0");
    free_telo(x);
}

static void test_sort(void) {
    t_telomere *x = new_telo();
    /* Append out of order */
    pattern_append_event(x, 0.9f, 0.1f);
    pattern_append_event(x, 0.1f, 0.9f);
    pattern_append_event(x, 0.5f, 0.5f);
    pattern_sort(x);
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.1f, 1e-5f, "sort[0] == 0.1");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 1), 0.5f, 1e-5f, "sort[1] == 0.5");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 2), 0.9f, 1e-5f, "sort[2] == 0.9");
    /* Velocities travel with their positions */
    ASSERT_FLOAT_EQ(pattern_get_velocity(x, 0), 0.9f, 1e-5f, "vel[0] after sort");
    ASSERT_FLOAT_EQ(pattern_get_velocity(x, 2), 0.1f, 1e-5f, "vel[2] after sort");
    free_telo(x);
}

static void test_sort_carries_skip_weight(void) {
    t_telomere *x = new_telo();
    pattern_append_event(x, 0.8f, 1.0f);
    pattern_append_event(x, 0.2f, 1.0f);
    x->skip_weight[0] = 0.7f; /* attached to pos=0.8 */
    x->skip_weight[1] = 0.3f; /* attached to pos=0.2 */
    pattern_sort(x);
    /* After sort: [0.2, 0.8], skip_weight should follow */
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.2f, 1e-5f, "sorted pos[0]");
    ASSERT_FLOAT_EQ(pattern_get_skip_weight(x, 0), 0.3f, 1e-5f,
                    "skip_weight follows sort: pos=0.2 had sw=0.3");
    ASSERT_FLOAT_EQ(pattern_get_skip_weight(x, 1), 0.7f, 1e-5f,
                    "skip_weight follows sort: pos=0.8 had sw=0.7");
    free_telo(x);
}

static void test_replace_resets_skip_weight(void) {
    t_telomere *x = new_telo();
    t_float pos[] = {0.25f, 0.75f};
    t_float vel[] = {0.8f,  0.5f};
    pattern_replace(x, pos, vel, 2);
    ASSERT(pattern_num_events(x) == 2, "count after replace");
    ASSERT_FLOAT_EQ(pattern_get_skip_weight(x, 0), 1.0f, 1e-5f,
                    "replace resets skip_weight[0] to 1.0");
    ASSERT_FLOAT_EQ(pattern_get_skip_weight(x, 1), 1.0f, 1e-5f,
                    "replace resets skip_weight[1] to 1.0");
    free_telo(x);
}

static void test_source_api(void) {
    t_telomere *x = new_telo();
    ASSERT(source_num_events(x) == 0, "source empty initially");
    source_append_event(x, 0.1f, 0.9f);
    source_append_event(x, 0.6f, 0.5f);
    ASSERT(source_num_events(x) == 2, "source count after 2 appends");
    ASSERT_FLOAT_EQ(x->source[0], 0.1f, 1e-5f, "source[0] pos");
    ASSERT_FLOAT_EQ(x->source_vel[1], 0.5f, 1e-5f, "source[1] vel");
    source_clear(x);
    ASSERT(source_num_events(x) == 0, "source empty after clear");
    free_telo(x);
}

/* ------------------------------------------------------------------ */
/* Transform tests                                                    */
/* ------------------------------------------------------------------ */

static void call_transform(t_telomere *x, const char *name,
                           int argc, t_float *fargs) {
    t_symbol *sym = gensym(name);
    t_transform_entry *e = telomere_lookup_transform(sym);
    if (!e) { fprintf(stderr, "call_transform: '%s' not found\n", name); return; }
    t_atom argv[4];
    for (int i = 0; i < argc; i++) SETFLOAT(&argv[i], fargs[i]);
    e->fn(x, argc, argv);
}

static void test_reverse(void) {
    t_telomere *x = new_telo();
    /* [0.0, 0.25, 0.5, 0.75] reversed -> [0.25, 0.5, 0.75, 1.0-0=0.0→wrap]
     * reverse maps p → 1-p, then sorts.
     * 0.0  → 1.0 wraps to 0.0
     * 0.25 → 0.75
     * 0.5  → 0.5
     * 0.75 → 0.25
     * sorted: [0.0, 0.25, 0.5, 0.75] */
    pattern_append_event(x, 0.0f,  1.0f);
    pattern_append_event(x, 0.25f, 1.0f);
    pattern_append_event(x, 0.5f,  1.0f);
    pattern_append_event(x, 0.75f, 1.0f);
    call_transform(x, "reverse", 0, NULL);
    ASSERT(pattern_num_events(x) == 4, "reverse preserves count");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.0f,  1e-5f, "reverse[0]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 1), 0.25f, 1e-5f, "reverse[1]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 2), 0.5f,  1e-5f, "reverse[2]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 3), 0.75f, 1e-5f, "reverse[3]");
    free_telo(x);
}

static void test_fast(void) {
    t_telomere *x = new_telo();
    /* [0.0, 0.5] with fast 2 → 4 events: [0, 0.25, 0.5, 0.75] */
    pattern_append_event(x, 0.0f, 1.0f);
    pattern_append_event(x, 0.5f, 1.0f);
    t_float args[] = {2.0f};
    call_transform(x, "fast", 1, args);
    ASSERT(pattern_num_events(x) == 4, "fast 2 doubles event count");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.0f,  1e-4f, "fast[0]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 1), 0.25f, 1e-4f, "fast[1]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 2), 0.5f,  1e-4f, "fast[2]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 3), 0.75f, 1e-4f, "fast[3]");
    free_telo(x);
}

static void test_slow(void) {
    t_telomere *x = new_telo();
    /* [0.0, 0.25, 0.5, 0.75] with slow 2:
     * 0.0  * 2 = 0.0  -> keep
     * 0.25 * 2 = 0.5  -> keep
     * 0.5  * 2 = 1.0  -> discard (>= 1.0)
     * 0.75 * 2 = 1.5  -> discard
     * result: [0.0, 0.5] */
    pattern_append_event(x, 0.0f,  1.0f);
    pattern_append_event(x, 0.25f, 1.0f);
    pattern_append_event(x, 0.5f,  1.0f);
    pattern_append_event(x, 0.75f, 1.0f);
    t_float args[] = {2.0f};
    call_transform(x, "slow", 1, args);
    ASSERT(pattern_num_events(x) == 2, "slow 2 halves event count");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.0f, 1e-5f, "slow[0] == 0.0");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 1), 0.5f, 1e-5f, "slow[1] == 0.5");
    free_telo(x);
}

static void test_slow_not_compressing(void) {
    /* Regression: old slow compressed events toward 0, making them
     * fire closer together (wrong). New slow stretches events apart. */
    t_telomere *x = new_telo();
    pattern_append_event(x, 0.1f, 1.0f);
    pattern_append_event(x, 0.4f, 1.0f);
    t_float args[] = {2.0f};
    call_transform(x, "slow", 1, args);
    /* 0.1*2=0.2 keep, 0.4*2=0.8 keep */
    ASSERT(pattern_num_events(x) == 2, "slow 2 on 2 events -> 2 survive");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.2f, 1e-5f, "slow stretches to 0.2");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 1), 0.8f, 1e-5f, "slow stretches to 0.8");
    /* Verify events are further apart, not closer together */
    float gap = pattern_get_event(x, 1) - pattern_get_event(x, 0);
    ASSERT(gap > 0.3f, "slow makes events further apart (gap > original 0.3)");
    free_telo(x);
}

static void test_fast_slow_inverse(void) {
    /* fast N then slow N should approximately restore original spacing
     * (events that fit in 1/N of the cycle) */
    t_telomere *x = new_telo();
    pattern_append_event(x, 0.0f,  1.0f);
    pattern_append_event(x, 0.1f,  1.0f);
    pattern_append_event(x, 0.2f,  1.0f);
    pattern_append_event(x, 0.3f,  1.0f);

    t_float args2[] = {2.0f};
    call_transform(x, "fast", 1, args2); /* 8 events */
    call_transform(x, "slow", 1, args2); /* back to 4 events in first half */

    /* After fast 2: positions in [0,0.5) and [0.5,1.0)
     * After slow 2: multiply by 2, keep < 1.0
     * First rep at [0, 0.05, 0.1, 0.15] * 2 = [0, 0.1, 0.2, 0.3] -> all < 1
     * Second rep at [0.5, 0.55, 0.6, 0.65] * 2 = [1.0, 1.1, 1.2, 1.3] -> all dropped
     * Result: 4 events at [0, 0.1, 0.2, 0.3] */
    ASSERT(pattern_num_events(x) == 4, "fast2+slow2 restores 4 events");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.0f,  1e-4f, "f+s[0]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 1), 0.1f,  1e-4f, "f+s[1]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 2), 0.2f,  1e-4f, "f+s[2]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 3), 0.3f,  1e-4f, "f+s[3]");
    free_telo(x);
}

static void test_palindrome(void) {
    t_telomere *x = new_telo();
    /* [0.0, 0.5] -> palindrome -> [0.0, 0.25, 0.5, 0.75]
     * first half: 0.0*0.5=0.0, 0.5*0.5=0.25
     * second half (reversed): mirror into [0.5,1.0)
     *   new_size-1-0 = 3: 0.5 + 0.0*0.5 = 0.5
     *   new_size-1-1 = 2: 0.5 + 0.5*0.5 = 0.75
     * sorted: [0.0, 0.25, 0.5, 0.75] */
    pattern_append_event(x, 0.0f, 1.0f);
    pattern_append_event(x, 0.5f, 1.0f);
    call_transform(x, "palindrome", 0, NULL);
    ASSERT(pattern_num_events(x) == 4, "palindrome doubles count");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.0f,  1e-5f, "palindrome[0]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 1), 0.25f, 1e-5f, "palindrome[1]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 2), 0.5f,  1e-5f, "palindrome[2]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 3), 0.75f, 1e-5f, "palindrome[3]");
    free_telo(x);
}

static void test_rotate(void) {
    t_telomere *x = new_telo();
    /* [0.0, 0.25, 0.5, 0.75], grid=4, rotate 1 -> shift by 1/4 = 0.25
     * 0.0+0.25=0.25, 0.25+0.25=0.50, 0.50+0.25=0.75, 0.75+0.25=1.0 wraps to 0.0
     * sorted: [0.0, 0.25, 0.5, 0.75] (same pattern, just phase shifted) */
    x->grid = 4;
    pattern_append_event(x, 0.0f,  1.0f);
    pattern_append_event(x, 0.25f, 1.0f);
    pattern_append_event(x, 0.5f,  1.0f);
    pattern_append_event(x, 0.75f, 1.0f);
    t_float args[] = {1.0f};
    call_transform(x, "rotate", 1, args);
    ASSERT(pattern_num_events(x) == 4, "rotate preserves count");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.0f,  1e-5f, "rotate[0] wraps to 0");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 1), 0.25f, 1e-5f, "rotate[1]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 2), 0.5f,  1e-5f, "rotate[2]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 3), 0.75f, 1e-5f, "rotate[3]");
    free_telo(x);
}

static void test_euclid(void) {
    /* E(3,8) — 3 hits in 8 slots: Bresenham gives positions 0, 3, 6
     * as slot indices -> 0/8=0.0, 3/8=0.375, 6/8=0.75 */
    t_telomere *x = new_telo();
    t_float args[] = {3.0f, 8.0f};
    call_transform(x, "euclid", 2, args);
    ASSERT(pattern_num_events(x) == 3, "euclid(3,8) produces 3 events");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 0), 0.0f,     1e-5f, "euclid(3,8)[0]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 1), 2.0f/8.0f, 1e-5f, "euclid(3,8)[1]");
    ASSERT_FLOAT_EQ(pattern_get_event(x, 2), 5.0f/8.0f, 1e-5f, "euclid(3,8)[2]");
    free_telo(x);
}

static void test_transforms_leave_sorted(void) {
    /* Verify that every transform leaves pattern in sorted order */
    const char *names[] = {"reverse", "palindrome", "rotate", NULL};
    t_float no_args[] = {0.0f};
    t_float rot_args[] = {1.0f};
    t_float *argsets[] = {no_args, no_args, rot_args, NULL};
    int     argcnts[]  = {0, 0, 1, 0};

    for (int t = 0; names[t]; t++) {
        t_telomere *x = new_telo();
        x->grid = 8;
        pattern_append_event(x, 0.1f, 1.0f);
        pattern_append_event(x, 0.4f, 1.0f);
        pattern_append_event(x, 0.7f, 1.0f);
        call_transform(x, names[t], argcnts[t], argsets[t]);

        int sorted = 1;
        for (int i = 1; i < pattern_num_events(x); i++) {
            if (pattern_get_event(x, i) < pattern_get_event(x, i-1))
                sorted = 0;
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "transform '%s' leaves pattern sorted", names[t]);
        ASSERT(sorted, msg);
        free_telo(x);
    }
}

static void test_pattern_sort_requirement_documented(void) {
    /* Ensure pattern_sort is exported and callable — this is the
     * documented contract for new transform authors. */
    t_telomere *x = new_telo();
    pattern_append_event(x, 0.9f, 1.0f);
    pattern_append_event(x, 0.1f, 1.0f);
    /* Unsorted: [0.9, 0.1] */
    ASSERT(pattern_get_event(x, 0) > pattern_get_event(x, 1),
           "pre-sort: first > second (unsorted)");
    pattern_sort(x);
    ASSERT(pattern_get_event(x, 0) < pattern_get_event(x, 1),
           "post-sort: first < second (sorted)");
    free_telo(x);
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    /* Register all built-in transforms so call_transform() can find them */
    telomere_transforms_builtins_setup();

    printf("=== telomere unit tests ===\n");

    /* Pattern API */
    printf("\n-- pattern API --\n");
    test_append_and_count();
    test_skip_weight_default();
    test_skip_weight_set();
    test_clear();
    test_sort();
    test_sort_carries_skip_weight();
    test_replace_resets_skip_weight();
    test_source_api();

    /* Transforms */
    printf("\n-- transforms --\n");
    test_reverse();
    test_fast();
    test_slow();
    test_slow_not_compressing();
    test_fast_slow_inverse();
    test_palindrome();
    test_rotate();
    test_euclid();
    test_transforms_leave_sorted();
    test_pattern_sort_requirement_documented();

    printf("\n=== results: %d passed, %d failed ===\n",
           g_passes, g_failures);
    return g_failures > 0 ? 1 : 0;
}
