# telomere — A Pure Data External for Algorithmic Rhythm

## Overview

**telomere** is a C external for Pure Data (Pd) that combines tap-input rhythm capture, variable quantization, Euclidean rhythm generation, metric modulation, and TidalCycles-inspired pattern transformations into a single object. It is designed to function as a self-contained rhythmic manipulation engine, drawing inspiration from the Soma Pulsar-23's approach to tapped-in loops with continuously variable quantization, while extending far beyond it with algorithmic composition tools.

The name reflects the idea of rhythmic cycles that repeat, shorten, extend, and transform — much like the biological structures they reference.

---

## Core Architecture

The external is structured around five interconnected modules:

1. **Input Capture** — Recording tapped rhythms in real time
2. **Quantization Engine** — Snapping events to a grid with 0–100% variable strength
3. **Euclidean Generator** — Algorithmically distributing beats across subdivisions
4. **Pattern Transformation** — TidalCycles-style manipulations (palindrome, rotation, fast/slow, stack)
5. **Metric Modulation & I/O** — Tempo transitions and pattern save/load

### Internal Data Representation

All timing data is stored as **normalized floats between 0.0 and 1.0**, representing positions within a single abstract cycle. This is the key design decision borrowed from TidalCycles: by decoupling pattern data from absolute time, every transformation becomes time-signature-independent. A separate clock mechanism (either internal or via Pd's `[tempo]` object) maps normalized positions to real-world milliseconds.

```c
typedef struct _telomere {
    t_object x_obj;

    // Outlets
    t_outlet *x_outlet_bang;      // Quantized trigger output
    t_outlet *x_outlet_status;    // Status/debug float output

    // Pattern storage
    t_float *normalized_times;    // Event positions, 0.0–1.0
    int      num_events;          // Number of events in current pattern
    int      buffer_capacity;     // Allocated size of the array

    // Euclidean pattern (binary)
    int     *euclidean_pattern;   // Array of 0s and 1s
    int      euclidean_steps;     // Total steps (N)
    int      euclidean_beats;     // Total beats (k)

    // Quantization
    t_float  quantize_pct;        // 0.0 (raw) to 1.0 (fully snapped)
    t_float  grid_subdivision;    // Grid resolution in subdivisions per cycle

    // Clock
    t_float  cycle_length_ms;     // Duration of one full cycle in milliseconds
    t_float  tempo_bpm;           // Current tempo
    int      beats_per_cycle;     // e.g. 4 for 4/4, 3 for 3/4

    // Metric modulation
    t_float  target_tempo;        // Tempo to ramp toward
    t_float  ramp_duration_ms;    // How long the transition takes
    int      pivot_subdivision;   // Common subdivision for modulation

    // Playback
    int      current_index;       // Position in pattern during playback
    t_clock *playback_clock;      // Pd clock for scheduled output

    // Variation
    t_float  jitter_amount;       // Random timing offset, 0.0–1.0
    t_float  skip_probability;    // Chance of dropping an event, 0.0–1.0

} t_telomere;
```

---

## Module 1: Input Capture

When `telomere` receives a `bang` on its primary inlet, it records the current time relative to the cycle start. The raw timestamp is converted to a normalized 0.0–1.0 value using the current cycle length.

```
normalized_position = (current_time - cycle_start_time) / cycle_length_ms
```

Events are stored in a dynamically allocated array. The buffer grows as needed using `realloc`. When the user sends a `[clear]` message, the buffer is reset.

### Messages

| Message | Effect |
|---|---|
| `bang` | Record a tap at the current time |
| `clear` | Empty the event buffer |
| `record 1` / `record 0` | Enable/disable recording mode |

---

## Module 2: Quantization Engine

The quantization engine implements continuous interpolation between raw tap timing and a perfect grid. For each recorded event, the output time is calculated as:

```
T_out = T_tap + (T_grid - T_tap) × P
```

Where:
- `T_tap` is the original normalized tap position
- `T_grid` is the nearest grid position (determined by `grid_subdivision`)
- `P` is the quantization percentage (0.0 = raw, 1.0 = fully quantized)

The grid is defined by the number of subdivisions per cycle. For example, if `grid_subdivision` is 16, the grid positions are at 0/16, 1/16, 2/16, ... 15/16.

Finding the nearest grid point:

```c
t_float snap_to_grid(t_float tap, int subdivisions) {
    t_float step = 1.0f / (t_float)subdivisions;
    t_float nearest = roundf(tap / step) * step;
    // Wrap to stay within 0.0–1.0
    if (nearest >= 1.0f) nearest -= 1.0f;
    return nearest;
}
```

### Messages

| Message | Effect |
|---|---|
| `quantize <float>` | Set quantization strength, 0–100 |
| `grid <int>` | Set grid resolution (subdivisions per cycle) |

---

## Module 3: Euclidean Rhythm Generator

The Euclidean (Bjorklund) algorithm distributes `k` beats as evenly as possible across `N` steps, producing rhythms commonly found in world music traditions. The implementation follows the iterative merge procedure:

1. Start with `k` groups of `[1]` and `N-k` groups of `[0]`.
2. Repeatedly merge the shorter list into the longer one, appending remainders from one group to another.
3. Concatenate the final groups to produce the binary pattern.

### Determining N from polymetric context

When working with multiple simultaneous meters (e.g., 3 cycles of 3/4 against 2 cycles of 4/4), `N` is derived from the LCM of the total beat counts, converted to the smallest common subdivision:

1. Total beats for each meter: 3×3 = 9 beats, 4×2 = 8 beats
2. LCM(9, 8) = 72 beats (coprime, so product)
3. Convert to eighth notes: 72 × 2 = 144 subdivisions

The user chooses `k` (the number of onsets). For musically interesting results, `k` should not be a trivial factor of `N`.

### Coprime vs. non-coprime behavior

- **Coprime (k, N):** The pattern is a single non-repeating sequence across the full cycle. Feels complex and syncopated.
- **Non-coprime (k, N):** The pattern decomposes into `GCD(k, N)` identical sub-patterns, each of length `N / GCD(k, N)`. More repetitive, with interest arising from the sub-pattern's interaction with the primary pulse.

Example — E(6, 16): GCD is 2, so the result is the 8-step pattern `10010100` repeated twice, yielding `1001010010010100`.

### Messages

| Message | Effect |
|---|---|
| `euclid <k> <N>` | Generate Euclidean rhythm with k beats in N steps |
| `euclid_to_pattern` | Convert the current Euclidean binary sequence into normalized event times |

---

## Module 4: Pattern Transformation

This module implements TidalCycles-inspired operations on the internal pattern data. All transformations operate on the normalized time array, keeping them independent of tempo and meter.

### Palindrome

Appends the reversed pattern to itself, doubling the cycle length.

```c
void telomere_palindrome(t_telomere *x) {
    int n = x->num_events;
    // Resize buffer to 2n
    x->normalized_times = realloc(x->normalized_times, 2 * n * sizeof(t_float));
    for (int i = 0; i < n; i++) {
        // Mirror: the reversed event at position p maps to (1.0 - p),
        // then shift into the second half of the new double-length cycle
        x->normalized_times[n + i] = 0.5f + (0.5f - x->normalized_times[n - 1 - i] * 0.5f);
    }
    x->num_events = 2 * n;
    // Renormalize all events to the new 0.0–1.0 range (double cycle)
    for (int i = 0; i < x->num_events; i++) {
        x->normalized_times[i] *= 0.5f;
    }
    // ... second half gets 0.5 + mirrored positions
}
```

Original `1010` → Palindrome `10100101` (8 steps).

### Rotation

Cyclically shifts event positions by a number of steps. Equivalent to changing where in the pattern "beat 1" falls.

```c
void telomere_rotate(t_telomere *x, int n) {
    // Shift all normalized_times by n/num_events, wrapping with fmod
    t_float offset = (t_float)n / (t_float)x->num_events;
    for (int i = 0; i < x->num_events; i++) {
        x->normalized_times[i] = fmodf(x->normalized_times[i] + offset, 1.0f);
    }
    // Re-sort the array to maintain chronological order
    // ... (qsort on floats)
}
```

### Fast / Slow

- `fast N` — Compresses the pattern so it repeats N times within one cycle. Each event position is mapped as: `new_pos = fmod(pos * N, 1.0)`.
- `slow N` — Stretches the pattern across N cycles. Events beyond 1.0 are only reached in subsequent cycles.

### Stack (Superimpose)

Merges a second pattern into the current one. The playback engine manages both sets of events simultaneously, outputting bangs from the combined timeline. Patterns of different lengths create polymetric textures.

### Messages

| Message | Effect |
|---|---|
| `palindrome` | Apply palindrome transformation |
| `rotate <int>` | Rotate pattern by N positions |
| `fast <float>` | Speed up pattern by factor N |
| `slow <float>` | Slow down pattern by factor N |
| `stack <filename>` | Load and merge a second pattern |
| `reverse` | Reverse the pattern in place |

---

## Module 5: Metric Modulation & Pattern I/O

### Metric Modulation

Metric modulation transitions smoothly between tempos using a shared subdivision as a pivot. The external calculates the new tempo based on the ratio of the old subdivision value to the new one, then ramps the internal clock over a user-defined duration.

For example, transitioning from 4/4 at 120 BPM to 6/8 using the eighth note as pivot:
- In 4/4 at 120 BPM, an eighth note = 250ms
- In 6/8, the dotted quarter gets the beat; keeping the eighth note constant at 250ms yields a new perceived tempo

The ramp is implemented using Pd's `clock_delay` mechanism, incrementally adjusting `cycle_length_ms` on each tick until the target is reached.

### Messages

| Message | Effect |
|---|---|
| `modulate_to_tempo <float>` | Set target tempo for modulation |
| `modulate_to_meter <int> <int>` | Set target meter (e.g., `6 8`) |
| `pivot_subdivision <int>` | Define the shared subdivision (e.g., `8` for eighth notes) |
| `ramp_duration <float>` | Duration of the tempo transition in ms |

### Pattern Export / Import

Patterns are saved as plain text files containing metadata and the normalized event list:

```
# telomere pattern file
tempo 120.0
meter 4 4
grid 16
quantize 75.0
events 8
0.000000
0.125000
0.250000
0.375000
0.500000
0.625000
0.750000
0.875000
```

The external uses standard C file I/O (`fopen`, `fprintf`, `fscanf`) wrapped through the Pd API's path resolution to locate files relative to the current patch.

### Messages

| Message | Effect |
|---|---|
| `save <filename>` | Export current pattern to file |
| `load <filename>` | Import pattern from file |

---

## Algorithmic Variation

On top of the deterministic transformations, telomere supports controlled randomness:

- **Jitter:** Adds a small random offset to each event's output time. The offset is scaled by `jitter_amount` and bounded to prevent events from crossing each other. `T_out = T_event + (random_float(-1, 1) * jitter_amount / grid_subdivision)`
- **Probability gate:** Each event has a chance of being silently skipped based on `skip_probability`. A random float is generated per event; if it falls below the threshold, the bang is suppressed.
- **Accent shifting:** Events coinciding with downbeats of the underlying meter can be flagged for stronger velocity output (sent as a float rather than a bang).

### Messages

| Message | Effect |
|---|---|
| `jitter <float>` | Set timing jitter amount, 0–100 |
| `skip <float>` | Set event skip probability, 0–100 |

---

## Inlets and Outlets

### Inlets (left to right)

1. **Primary inlet** — Receives `bang` (tap input / trigger playback step), `float` (quantize percentage), and all message commands
2. **Clock inlet** — Receives tempo/clock sync from external source (e.g., `[metro]` or `[tempo]`)

### Outlets (left to right)

1. **Bang outlet** — Outputs quantized/transformed rhythm events
2. **Float outlet** — Outputs status information (current tempo, pattern length, etc.)

---

## Setup Function

```c
void telomere_setup(void) {
    telomere_class = class_new(
        gensym("telomere"),
        (t_newmethod)telomere_new,
        (t_freemethod)telomere_free,
        sizeof(t_telomere),
        CLASS_DEFAULT,
        A_DEFFLOAT,  // Optional: initial tempo
        0
    );

    class_addbang(telomere_class, telomere_bang);
    class_addfloat(telomere_class, telomere_float);

    // Quantization & grid
    class_addmethod(telomere_class, (t_method)telomere_quantize,
                    gensym("quantize"), A_FLOAT, 0);
    class_addmethod(telomere_class, (t_method)telomere_grid,
                    gensym("grid"), A_FLOAT, 0);

    // Euclidean
    class_addmethod(telomere_class, (t_method)telomere_euclid,
                    gensym("euclid"), A_FLOAT, A_FLOAT, 0);

    // Transformations
    class_addmethod(telomere_class, (t_method)telomere_palindrome,
                    gensym("palindrome"), 0);
    class_addmethod(telomere_class, (t_method)telomere_rotate,
                    gensym("rotate"), A_FLOAT, 0);
    class_addmethod(telomere_class, (t_method)telomere_fast,
                    gensym("fast"), A_FLOAT, 0);
    class_addmethod(telomere_class, (t_method)telomere_slow,
                    gensym("slow"), A_FLOAT, 0);
    class_addmethod(telomere_class, (t_method)telomere_reverse,
                    gensym("reverse"), 0);

    // Metric modulation
    class_addmethod(telomere_class, (t_method)telomere_mod_tempo,
                    gensym("modulate_to_tempo"), A_FLOAT, 0);
    class_addmethod(telomere_class, (t_method)telomere_mod_meter,
                    gensym("modulate_to_meter"), A_FLOAT, A_FLOAT, 0);
    class_addmethod(telomere_class, (t_method)telomere_pivot,
                    gensym("pivot_subdivision"), A_FLOAT, 0);

    // Variation
    class_addmethod(telomere_class, (t_method)telomere_jitter,
                    gensym("jitter"), A_FLOAT, 0);
    class_addmethod(telomere_class, (t_method)telomere_skip,
                    gensym("skip"), A_FLOAT, 0);

    // I/O
    class_addmethod(telomere_class, (t_method)telomere_save,
                    gensym("save"), A_SYMBOL, 0);
    class_addmethod(telomere_class, (t_method)telomere_load,
                    gensym("load"), A_SYMBOL, 0);

    // Utility
    class_addmethod(telomere_class, (t_method)telomere_clear,
                    gensym("clear"), 0);

    post("telomere: algorithmic rhythm engine loaded");
}
```

---

## Extensibility

The architecture is designed for easy addition of new transformations via function pointers:

```c
typedef void (*pattern_transform_fn)(t_telomere *x, int argc, t_atom *argv);

// Register new transforms at runtime or compile time
class_addmethod(telomere_class, (t_method)my_new_transform,
                gensym("my_transform"), A_GIMME, 0);
```

Potential future additions include `chop` (subdividing events), `every N` (applying a transform only every Nth cycle), `degrade` (probabilistic event removal that increases over time), and `striate` (interleaving slices of two patterns).

---

## Compilation

The external compiles against the Pd headers using standard platform toolchains:

```bash
# Linux
gcc -shared -o telomere.pd_linux -fPIC \
    -I/usr/include/pd telomere.c -lm

# macOS
gcc -bundle -undefined dynamic_lookup -o telomere.pd_darwin \
    -I/Applications/Pd.app/Contents/Resources/src telomere.c -lm

# Windows (MinGW)
gcc -shared -o telomere.dll \
    -I"C:/Program Files/Pd/src" telomere.c -lm -lpd
```

Place the compiled binary in your Pd search path or alongside your patch.

---

## Example Patch Usage

```
[metro 500]
|
[telomere 120]  <-- create with 120 BPM
|            |
[makenote]   [print status]
|
[noteout]

Messages to send to telomere:
[euclid 5 16(        -- generate E(5,16) rhythm
[quantize 75(         -- 75% quantization
[palindrome(          -- double the pattern as a palindrome
[rotate 3(            -- shift start point by 3 positions
[jitter 10(           -- add 10% timing humanization
[save mybeat.txt(     -- export to file
[load mybeat.txt(     -- import from file
[modulate_to_tempo 90( -- ramp to 90 BPM
```

---

## Summary

telomere unifies several distinct rhythmic concepts — Euclidean distribution, variable quantization, polymetric cycle calculation, Steve Reich-style phasing, and TidalCycles pattern algebra — into a single Pd object with a clean message interface. Its normalized-time internal representation ensures that all transformations compose cleanly regardless of tempo or meter, and its C implementation keeps timing precise enough for real-time performance.
