# Primase Implementation Plan

## Phase 1: Transform Chain (non-destructive transform system)

This is the architectural foundation. Many later items (undo, swing, per-event weights) become simpler once patterns are source + chain rather than bare mutable buffers.

### 1a. Source pattern buffer and playback buffer

**primase.h** — add to `t_primase`:
```c
t_float  *source;           /* frozen source pattern (positions)           */
t_float  *source_vel;       /* frozen source velocities                    */
int       source_count;     /* event count in source                       */
int       source_alloc;     /* allocated capacity for source buffers       */
```

`pattern`/`velocity` become the **derived playback buffer** — never written to directly by the user, only by chain evaluation.

**Recording**, **`read`**, **`euclid`**, and **`clear`** write to `source`/`source_vel`, then trigger chain re-evaluation to populate `pattern`/`velocity`.

**primase_new** allocates `source` and `source_vel` (same initial size as `pattern`).
**primase_free** frees them.

### 1b. Chain storage

**primase.h** — new struct and fields:
```c
#define PRIMASE_MAX_CHAIN 16

typedef struct _chain_entry {
    t_symbol *name;                 /* transform name ("fast", "reverse", ...) */
    int       argc;                 /* number of stored arguments              */
    t_float   argv[4];             /* argument values (max 4 floats)          */
    int       bypassed;            /* 1 = skip during evaluation              */
} t_chain_entry;
```

Add to `t_primase`:
```c
t_chain_entry chain[PRIMASE_MAX_CHAIN];
int           chain_len;
```

### 1c. Chain evaluation function

New function in `primase.c` (or a new `primase_chain.c`):

```
primase_chain_eval(t_primase *x)
```

1. Copy `source`/`source_vel` → `pattern`/`velocity` (via `pattern_replace`)
2. For each non-bypassed entry in `chain[0..chain_len-1]`:
   - Build a temporary `t_atom argv[]` from `entry->argv`
   - Look up the transform in the registry
   - Call `entry->fn(x, entry->argc, argv)` — transforms mutate `pattern`/`velocity` as they do today
3. Transforms are unchanged — they still operate on the pattern API. The difference is they now operate on a copy that was seeded from source.

This means **all existing transforms work as-is with zero modification**.

### 1d. Messages

| Message | Effect |
|---|---|
| `chain_add <transform> [args...]` | Append to chain, re-evaluate |
| `chain_remove <index>` | Remove entry at index, re-evaluate |
| `chain_clear` | Empty the chain, re-evaluate (restores source) |
| `chain_replace <index> <transform> [args...]` | Replace entry, re-evaluate |
| `chain_bypass <index> <0\|1>` | Toggle bypass on entry, re-evaluate |
| `chain_dump` | Print current chain to console |

All of these call `primase_chain_eval` after modifying the chain.

### 1e. Backward compatibility

Direct transform messages (`reverse`, `fast 2`, etc.) continue to work exactly as before — they mutate the playback buffer destructively, same as today. The chain is an opt-in parallel system. Users who never send `chain_add` see zero behavioral change.

---

## Phase 2: Fix `slow` transform

**Current bug:** `slow N` multiplies all positions by `1/N`, compressing events into `[0, 1/N]` of the cycle. Events fire *faster* and closer together — the opposite of what "slow" means.

**Fix:** `slow N` should *stretch* the pattern so it spans N cycles, then keep only the events that fall within the first cycle. Concretely:
- Divide all positions by N: `pos = pos / factor` — NO, that's the same bug.

Actually the correct semantics: `slow N` means the pattern now takes N cycles to complete. Since our cycle is normalized to 0–1, we keep the positions unchanged but only emit 1/N of them per cycle. But we don't have multi-cycle support.

**Practical fix for single-cycle model:** `slow N` keeps only every Nth event, thinning the pattern while preserving the time range. This is the TidalCycles interpretation adapted to a fixed-length cycle.

Alternative: multiply positions by N and discard events >= 1.0. This stretches the pattern temporally — event at 0.25 moves to 0.5 (with factor 2), effectively playing half as fast. Events beyond 1.0 are clipped since they'd fall in the next cycle.

```c
/* For each event: new_pos = old_pos * factor.
   Keep only events where new_pos < 1.0 */
```

This is the correct inversion of `fast N` (which divides positions by N and repeats). `slow 2` on a 4-event pattern at [0, 0.25, 0.5, 0.75] → [0, 0.5] (the 0.5 and 0.75 map to 1.0 and 1.5, both >= 1.0, discarded). Pattern now plays at half density in the same cycle time = perceptually slower.

---

## Phase 3: Loop mode

**primase.h** — add:
```c
int loop;  /* 1 = auto-restart at end of cycle */
```

**primase_tick** — where playback currently stops (`x->playing = 0`), instead check:
```c
if (x->loop) {
    x->play_index = 0;
    x->cycle_start_time = clock_getlogicaltime();
    double delay = x->pattern[0] * x->cycle_length_ms;
    if (delay < 0.1) delay = 0.1;
    clock_delay(x->playback_clock, delay);
} else {
    x->playing = 0;
}
```

**Message:** `loop <0|1>` — sets `x->loop`. Default 0 (backward compatible).

---

## Phase 4: Armed recording (cycle-quantized record start)

The `armed` field already exists. Implement:

**`record` message when not recording:**
- If currently playing, set `x->armed = 1` instead of starting immediately.
- At cycle boundary (in `primase_tick` when cycle completes), if `armed`:
  - `x->armed = 0; x->recording = 1; pattern_clear(source); x->cycle_start_time = clock_getlogicaltime();`
- If not playing, start recording immediately (current behavior).

**`record` while armed:** cancel arm (`x->armed = 0`).

**Status outlet:** output 2.0 for armed (distinct from 1.0 for recording, 0.0 for idle).

---

## Phase 5: External clock sync

**primase.h** — add:
```c
int sync_mode;  /* 0 = internal, 1 = external clock */
```

**Message:** `sync <0|1>` — switch between internal and external clock.

**`sync 1` behavior:**
- A bang on the main inlet while in sync mode resets the cycle: `x->cycle_start_time = clock_getlogicaltime()`.
- This lets an external `[metro]` or MIDI clock drive the cycle boundary.
- Playback re-triggers from the top of the pattern on each sync bang.

**Implementation:** In `primase_bang`, add a branch for `x->sync_mode && x->playing`:
```c
if (x->sync_mode && x->playing) {
    /* External sync: restart cycle */
    clock_unset(x->playback_clock);
    x->play_index = 0;
    x->cycle_start_time = clock_getlogicaltime();
    double delay = x->pattern[0] * x->cycle_length_ms;
    if (delay < 0.1) delay = 0.1;
    clock_delay(x->playback_clock, delay);
    return;
}
```

---

## Phase 6: Swing

**primase.h** — add:
```c
t_float swing_amt;  /* 0.0 = straight, positive = delay even-indexed events */
```

**Applied at playback time in `primase_tick`** (not baked into the pattern):
```c
t_float swing_offset = 0.0f;
if (x->swing_amt != 0.0f && (x->play_index % 2 == 1)) {
    swing_offset = x->swing_amt / (t_float)x->grid;
}
t_float out_pos = pos + swing_offset;
```

This delays every odd-indexed event by `swing_amt` grid subdivisions. Applied at output time so it doesn't mutate the pattern.

**Message:** `swing <float>` — sets `x->swing_amt`. Range clamped to [-0.5, 0.5].

---

## Phase 7: Per-event skip weight

**primase.h** — add:
```c
t_float *skip_weight;  /* per-event probability multiplier, parallel to pattern[] */
```

Allocated/freed alongside `pattern`/`velocity`. Default all 1.0.

**primase_tick** skip logic changes from:
```c
if (r < x->skip_prob)
```
to:
```c
if (r < x->skip_prob * x->skip_weight[x->play_index])
```

**Message:** `skipweight <index> <weight>` — set per-event weight (0.0–1.0).

The `skip_weight` array also needs to be carried through `pattern_replace`, `pattern_copy_to`, `pattern_sort`, etc. — this means extending the pattern API or adding a third parallel array to all the bulk operations.

Alternatively, store skip weight inside the velocity value (pack both into the 0–1 range) — but that sacrifices resolution. Better to keep it as a separate parallel array.

---

## Phase 8: Test suite

Create `tests/` directory with a standalone test runner. Since transforms use the pattern API and the pattern API only needs the struct + Pd memory functions, we can stub the Pd functions (they're already stubbed in `pd/m_pd.h`) and test in a normal C program.

**tests/test_main.c** — main test runner:
- `test_pattern_api()` — append, clear, sort, resize, replace, copy_to
- `test_transform_palindrome()` — known input → expected output
- `test_transform_reverse()` — known input → expected output
- `test_transform_fast()` — known input → expected output
- `test_transform_slow()` — known input → expected output (after fix)
- `test_transform_euclid()` — E(3,8), E(5,16) against known patterns
- `test_transform_rotate()` — rotate by known offset
- `test_chain_eval()` — source + chain → expected playback buffer
- `test_pattern_sort_required()` — verify transforms leave pattern sorted

**Makefile** — add `make test_unit` target that compiles the test runner against the stub headers and runs it.

**Document `pattern_sort` requirement:** Add a comment to `primase_transform.h` near the `t_transform_fn` typedef stating that transforms MUST leave the pattern in sorted order (call `pattern_sort` if positions were modified). Also add a post-condition check in `primase_chain_eval` that warns if the pattern is unsorted after a transform.

---

## File Changes Summary

| File | Changes |
|---|---|
| `primase.h` | Add source buffers, chain storage, loop, sync_mode, swing_amt, skip_weight fields |
| `primase.c` | Add chain_eval, chain messages, loop logic in tick, armed logic, sync logic, swing in tick, skip_weight in tick |
| `primase_pattern_api.h` | Add skip_weight accessors; document sort requirement |
| `primase_pattern_api.c` | Carry skip_weight through all bulk ops; add accessors |
| `transforms/slow.c` | Fix to multiply positions by factor, discard >= 1.0 |
| `transforms/builtins.c` | No changes needed |
| `tests/test_main.c` | New: unit test runner |
| `Makefile` | Add test_unit target |

## Implementation Order

1. **Phase 2: Fix `slow`** — smallest change, highest impact, no new fields
2. **Phase 3: Loop mode** — single field + 6 lines in tick, huge usability win
3. **Phase 1: Transform chain** — the big architectural piece; source buffers, chain storage, eval, messages
4. **Phase 4: Armed recording** — uses existing field, needs chain awareness
5. **Phase 5: External clock sync** — independent of chain
6. **Phase 6: Swing** — playback-time only, no pattern mutation
7. **Phase 7: Per-event skip weight** — extends pattern API
8. **Phase 8: Test suite** — validates everything above
