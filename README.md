# telomere

A Pure Data (Pd) external for algorithmic rhythm capture, quantization, and pattern transformation. Combines tap-input recording, variable quantization, Euclidean rhythm generation, metric modulation, and TidalCycles-inspired pattern transforms into a single object.

## Features

- **Tap-input recording** — capture rhythms in real-time as normalized positions (0.0–1.0) within a cycle
- **Variable quantization** — continuously adjustable snap strength (0–100%) against a configurable grid
- **Euclidean rhythms** — generate Bjorklund-distributed patterns (k hits in N steps)
- **Pattern transforms** — `palindrome`, `rotate`, `reverse`, `fast`, `slow`, `euclid`, `jitter`, `skip`, `degrade`
- **Metric modulation** — smooth tempo transitions between related meters
- **Extensible architecture** — add new transforms without modifying core code

## Building

**Prerequisites:** gcc, make, Pure Data headers

```bash
make                              # build with default Pd path
make PD_PATH=/usr/include/pd      # specify custom Pd header location
make test                         # compile-check using stub headers (no Pd install required)
make clean                        # remove build artifacts
```

The output binary is `telomere.pd_linux` on Linux or `telomere.pd_darwin` on macOS.

## Installation

Copy the compiled binary into your Pd search path or into the same directory as your patch.

## Usage

Create a `[telomere]` object in your Pd patch. It provides one inlet and four outlets:

**Inlet:**
- `bang` — tap a rhythm event / trigger playback
- `float` — set quantize strength (0.0–1.0)
- Messages: `record`, `stop`, `clear`, `play`, `grid <n>`, `beats <n>`, `tempo <f>`, `euclid <k> <n>`, `dump`, and any registered transform name

**Outlets (left to right):**
1. Bang — quantized/transformed rhythm events
2. Float — current event position
3. Float — event count
4. Float — status messages

### Example messages

```
record          — start recording taps
stop            — stop recording/playback
play            — begin playback
clear           — clear the current pattern
grid 16         — set quantization grid to 16 subdivisions
beats 4         — set 4 beats per cycle
euclid 3 8      — generate a 3-over-8 Euclidean rhythm
palindrome      — append reversed pattern
rotate 2        — shift pattern start by 2 positions
fast 2          — double playback speed
slow 2          — halve playback speed
jitter 0.05     — add random timing displacement
skip 0.3        — 30% chance to drop each event
degrade 0.5     — probabilistic removal, keeps at least one event
dump            — print pattern to console
```

## Adding a transform

1. Create `transforms/mytransform.c` implementing the signature:
   ```c
   void transform_mytransform(t_telomere *x, int argc, t_atom *argv);
   ```
2. Register it in setup:
   ```c
   telomere_register_transform(gensym("mytransform"), transform_mytransform,
                               "description", min_args, max_args);
   ```
3. Add the extern declaration and registration call in `transforms/builtins.c`.
4. Recompile.

No changes to `telomere.c` are required.

## Project structure

```
telomere.c                  Core dispatch and object lifecycle
telomere.h                  Main struct definition
telomere_transform.h        Transform interface and registry types
telomere_pattern_api.h/c    Pattern read/write API for transforms
telomere_registry.c         Transform registry (linked-list)
transforms/
  builtins.c                Aggregator for built-in transforms
  palindrome.c rotate.c reverse.c fast.c slow.c
  euclid.c jitter.c skip.c degrade.c
pd/
  m_pd.h                    Stub header for compile-time checking
Makefile                    Platform-aware build system
```

## Design notes

All timing data uses **normalized 0.0–1.0 positions** within a cycle, decoupling patterns from tempo and time signature. The transform system follows the Open/Closed Principle: a registry maps message names to transform functions so the core dispatcher never needs modification when transforms are added. Transforms access pattern data exclusively through a narrow API (`telomere_pattern_api.h`), insulating them from internal struct changes.
