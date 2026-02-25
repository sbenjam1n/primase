# telomere

Like its namesake — the repetitive sequence capping each chromosome, ensuring faithful replication cycle after cycle while gradually introducing variation — telomere is a Pure Data external that captures rhythmic patterns and replays them through non-destructive transform chains, preserving the source recording intact while layering controlled mutation with every pass.

## Features

- **Tap-input recording** — capture rhythms in real-time as normalized positions (0.0–1.0) within a cycle
- **Variable quantization** — continuously adjustable snap strength (0–100%) against a configurable grid
- **Non-destructive transform chain** — apply ordered transforms that stay editable live; the original recording is never mutated
- **Euclidean rhythms** — generate Bjorklund-distributed patterns (k hits in N steps)
- **Pattern transforms** — `palindrome`, `rotate`, `reverse`, `fast`, `slow`, `euclid`, `jitter`, `skip`, `degrade`
- **Loop mode** — auto-restart at cycle end for hands-free looping
- **Armed recording** — quantize record start to the next cycle boundary so overdubs land in time
- **External clock sync** — lock cycle phase to an incoming bang (from `[metro]`, MIDI clock, etc.)
- **Swing** — delay odd-indexed events to add rhythmic feel without modifying the stored pattern
- **Per-event skip weight** — independent skip probability per event for compositional variation
- **Metric modulation** — smooth tempo transitions between related meters
- **Extensible architecture** — add new transforms without modifying core code
- **OSC control** — full remote control via Open Sound Control (39 endpoints across 8 categories) using `[telomere-osc]` abstraction

## Building

**Prerequisites:** gcc, make, Pure Data headers

```bash
make                              # build with default Pd path
make PD_PATH=/usr/include/pd      # specify custom Pd header location
make test                         # compile-check using stub headers (no Pd install required)
make test_unit                    # build and run standalone unit tests (110 assertions)
make clean                        # remove build artifacts
```

The output binary is `telomere.pd_linux` on Linux or `telomere.pd_darwin` on macOS.

## Installation

Copy the compiled binary into your Pd search path or into the same directory as your patch.

## Usage

Create a `[telomere]` object in your Pd patch. An optional float argument sets the initial tempo: `[telomere 120]`.

**Inlets (left to right):**
1. `bang` — tap event during recording / trigger playback; `float` — set tempo; messages — all commands
2. `bang` — external clock input (for `clockfollow` and `sync` modes)
3. `float` — velocity for the next recorded event (0.0–1.0, default 1.0)
4. `float` — accent modulation multiplier (default 1.0)
5. `float` — jitter amount (0.0–1.0)
6. `float` — skip probability (0.0–1.0)
7. `float` — swing amount

**Outlets (left to right):**
1. Bang — fires on each playback event
2. Float — event position (0.0–1.0), with jitter and swing applied
3. Float — event velocity (0.0–1.0)
4. Float — current event count (fires at cycle end)
5. Float — status: `0` idle, `1` recording, `2` armed

### Recording and playback

```
record          start recording; if playing, arms for next cycle boundary
stop            stop recording or playback; cancel arm
play            start playback
loop 1          auto-restart at end of each cycle (default: 0)
clear           clear pattern
```

### Timing

```
tempo 120       set tempo in BPM
beats 4         set beats per cycle (default: 4)
grid 16         set quantization grid subdivisions (default: 16)
quantize 0.8    quantize strength: 0 = free, 1 = fully snapped (default: 0)
metric 3 2      metric modulation: next cycle plays at 3:2 speed ratio
```

### Variation

```
jitter 0.02     random timing displacement per event (0.0–1.0)
skip 0.3        global probability of dropping each event (0.0–1.0)
skipweight 2 0  set per-event skip weight: event 2 is never dropped
skipweight 0 2  set per-event skip weight: event 0 skips at 2× global rate
swing 0.04      delay odd-indexed events by 0.04 cycle-units for feel
```

`skip_prob × skip_weight[i]` gives the effective probability for event `i`. Default weight is 1.0. Setting weight to 0 pins an event; setting it above 1.0 amplifies the global probability for that event (clamped to 1.0 effective).

Swing is applied at output and scheduling time — it affects when events actually fire, not just the position outlet value. Range is ±0.5 (in 0–1 cycle space).

### Direct transforms

Transforms applied directly mutate the derived pattern immediately:

```
palindrome      append reversed pattern to create a palindromic loop
rotate 2        shift pattern start by 2 grid positions
reverse         reverse temporal order
fast 2          repeat pattern twice per cycle (double density)
slow 2          stretch pattern to half density (keep events that fit)
euclid 3 8      replace with 3-over-8 Euclidean rhythm
jitter 0.05     bake random displacement into pattern positions
skip 0.3        probabilistically remove events
degrade 0.5     probabilistic removal, keeps at least one event
dump            print pattern to console
```

### Transform chain

The chain system records which transforms to apply and re-evaluates them from the original frozen recording on every change. Transforms can be added, removed, reordered, or bypassed without losing the source pattern.

```
chain_add reverse            append "reverse" to the end of the chain
chain_add fast 2             append "fast 2"
chain_dump                   print the current chain
chain_bypass 0 1             bypass chain entry 0 (temporarily disable)
chain_bypass 0 0             restore chain entry 0
chain_replace 1 slow 3       swap entry 1 for "slow 3" live
chain_remove 0               remove entry 0; remaining entries shift down
chain_clear                  remove all entries; pattern restores to source
```

Chain order matters — `chain_add fast 2` then `chain_add reverse` applies fast first, then reverses the result, like pedals in series. Swapping them produces a different pattern.

`chain_clear` is a full undo back to the raw recording. `chain_remove` of the last entry is single-step undo.

### External clock sync

External clock and sync bangs are received on **inlet 2** (the dedicated clock inlet), keeping them separate from tap recording and playback triggers on inlet 1.

`sync 1` enables sync mode. When playback is running, a bang on inlet 2 **resets the cycle phase**: the pattern jumps back to event 0 and the cycle's time reference is re-stamped to the moment of the bang.

```
sync 1          enable external sync mode
sync 0          disable (bangs on inlet 2 are ignored)
```

`clockfollow 1` enables clock-following mode. Bangs on inlet 2 are used to derive tempo from inter-bang intervals.

Connect any periodic bang source — `[metro]`, a MIDI clock divider, a tap-tempo output — to **inlet 2** to lock the pattern's cycle boundary to that source. Since the reset is hard (not tempo-tracking), telomere's internal event spacing still follows its own tempo; `sync` only eliminates drift at the cycle boundary.

Typical setup:

```
[metro 2000]     ← one bang per bar at 120 BPM, 4/4
     |
     |  [sync 1(         ← sent once to inlet 1 to enable sync mode
     |       |
[telomere 120]           ← metro connects to inlet 2 (clock)
```

Every time the metro fires on inlet 2, the pattern restarts from the top. Taps on inlet 1 can still record while the clock runs. Drift across bars is zero regardless of how many cycles have passed.

### OSC control

The `telomere-osc` abstraction provides full remote control via Open Sound Control. Requires the mrpeach library.

```
[telomere-osc 9001]   ← create with UDP port number
        |
   [telomere 120]     ← connect outlet to telomere inlet
```

OSC address scheme: `/telomere/<category>/<parameter>`

Categories: `/transport`, `/tempo`, `/pattern`, `/variation`, `/chain`, `/scene`, `/file`, `/query` (39 endpoints total). See `telomere-osc-help.pd` for the complete reference.

```bash
oscsend localhost 9001 /telomere/transport/loop f 1
oscsend localhost 9001 /telomere/tempo/bpm f 140
oscsend localhost 9001 /telomere/pattern/set fff 0.0 0.25 0.5
oscsend localhost 9001 /telomere/chain/add sf euclid 3 8
oscsend localhost 9001 /telomere/query/dump
```

## Adding a transform

1. Create `transforms/mytransform.c`:
   ```c
   #include "../telomere_transform.h"
   #include "../telomere_pattern_api.h"

   static void transform_mytransform(t_telomere *x, int argc, t_atom *argv) {
       /* read/write pattern via pattern_get_event / pattern_set_event etc. */
       pattern_sort(x);   /* REQUIRED if you modified any positions */
   }

   void mytransform_register(void) {
       telomere_register_transform(gensym("mytransform"), transform_mytransform,
                                   "description", min_args, max_args);
   }
   ```
2. Add `extern void mytransform_register(void);` and a `mytransform_register()` call in `transforms/builtins.c`.
3. Add the file to `TRANSFORM_SRC` in `Makefile`.
4. Recompile.

**Important:** transforms must call `pattern_sort(x)` after any operation that modifies event positions. Failure to sort causes events to fire out of order silently. See `telomere_pattern_api.h` for the full invariant.

## Project structure

```
telomere.c                  Core dispatch, chain eval, and object lifecycle
telomere.h                  Struct definition (t_telomere, t_chain_entry)
telomere_transform.h        Transform interface and registry types
telomere_pattern_api.h/c    Pattern and source buffer API for transforms
telomere_registry.c         Transform registry (linked-list)
transforms/
  builtins.c                Registration aggregator for built-in transforms
  palindrome.c rotate.c reverse.c fast.c slow.c
  euclid.c jitter.c skip.c degrade.c
tests/
  test_main.c               Unit tests (110 assertions, no Pd required)
  pd_stub.c                 Minimal Pd runtime stubs for testing
pd/
  m_pd.h                    Stub header for compile-time checking
telomere-osc.pd             OSC receiver/router abstraction (mrpeach)
telomere-osc-help.pd        OSC endpoint reference documentation
Makefile                    Platform-aware build system
```

## Design notes

All timing data uses **normalized 0.0–1.0 positions** within a cycle, decoupling patterns from tempo and time signature. The transform system follows the Open/Closed Principle: a registry maps message names to transform functions so the core dispatcher never needs modification when transforms are added. Transforms access pattern data exclusively through a narrow API (`telomere_pattern_api.h`), insulating them from internal struct changes.

The **source/derived split** separates what was recorded from what plays back. `source[]` is written only by recording and `read`; `pattern[]` is written only by `telomere_chain_eval()`. Every chain operation re-derives `pattern[]` from scratch, so any combination of transforms can be undone by removing chain entries — the recording is always intact.

**Swing and jitter** are both applied at playback time, not baked into stored positions. Swing adjusts actual firing times (scheduling delay) as well as the position outlet; jitter affects only the output position and does not move the clock tick. This means swing changes can be heard immediately on the next event, and removing swing restores exact original timing with no residual drift.
