# telomere — Transform System Architecture (Open/Closed Principle)

## The Problem

Without OCP, adding a new transformation (say `degrade` or `chop`) means editing the core telomere source: adding a new method, a new `class_addmethod` call in setup, and potentially touching shared state management. Every addition risks breaking existing transforms and requires recompilation of the entire external.

## The Design

The transform system separates three concerns:

1. **Transform interface** — A uniform function signature that all transforms implement
2. **Transform registry** — A lookup table that maps message names to transform functions
3. **Core engine** — Dispatches to registered transforms without knowing what they do

The core is **closed for modification** (you never edit `telomere.c` to add a transform) and **open for extension** (new transforms register themselves at load time).

---

## Transform Interface

Every transform is a function with this signature:

```c
// telomere_transform.h

#ifndef TELOMERE_TRANSFORM_H
#define TELOMERE_TRANSFORM_H

#include "m_pd.h"

// Forward declaration — transforms receive the full object
// but should only interact through the pattern API
typedef struct _telomere t_telomere;

// The universal transform signature
//   x    — the telomere instance
//   argc — number of arguments passed with the message
//   argv — the arguments themselves (floats, symbols, etc.)
typedef void (*t_transform_fn)(t_telomere *x, int argc, t_atom *argv);

// Metadata that accompanies each registered transform
typedef struct _transform_entry {
    t_symbol           *name;        // Message name (e.g., "palindrome")
    t_transform_fn      fn;          // The function pointer
    const char         *description; // Human-readable help text
    int                 min_args;    // Minimum required arguments
    int                 max_args;    // Maximum accepted arguments (-1 = variadic)
    struct _transform_entry *next;   // Linked list pointer
} t_transform_entry;

// Registration function — called by each transform module
void telomere_register_transform(
    t_symbol       *name,
    t_transform_fn  fn,
    const char     *description,
    int             min_args,
    int             max_args
);

#endif
```

## Transform Registry

The registry is a simple linked list (sufficient for the expected number of transforms — dozens, not thousands). It lives in its own compilation unit.

```c
// telomere_registry.c

#include "telomere_transform.h"
#include <stdlib.h>

static t_transform_entry *registry_head = NULL;

void telomere_register_transform(
    t_symbol       *name,
    t_transform_fn  fn,
    const char     *description,
    int             min_args,
    int             max_args
) {
    // Check for duplicate registration
    t_transform_entry *cur = registry_head;
    while (cur) {
        if (cur->name == name) {
            post("telomere: warning — transform '%s' already registered, replacing",
                 name->s_name);
            cur->fn = fn;
            cur->description = description;
            cur->min_args = min_args;
            cur->max_args = max_args;
            return;
        }
        cur = cur->next;
    }

    // Allocate and prepend
    t_transform_entry *entry = (t_transform_entry *)malloc(sizeof(t_transform_entry));
    entry->name        = name;
    entry->fn          = fn;
    entry->description = description;
    entry->min_args    = min_args;
    entry->max_args    = max_args;
    entry->next        = registry_head;
    registry_head      = entry;

    post("telomere: registered transform '%s'", name->s_name);
}

t_transform_entry *telomere_lookup_transform(t_symbol *name) {
    t_transform_entry *cur = registry_head;
    while (cur) {
        if (cur->name == name) return cur;
        cur = cur->next;
    }
    return NULL;
}

// Iterate all transforms (used by the "help" command)
t_transform_entry *telomere_get_registry_head(void) {
    return registry_head;
}

void telomere_registry_free(void) {
    t_transform_entry *cur = registry_head;
    while (cur) {
        t_transform_entry *next = cur->next;
        free(cur);
        cur = next;
    }
    registry_head = NULL;
}
```

## Pattern API

Transforms should not reach directly into the struct to manipulate `normalized_times`. Instead they use a narrow API that enforces invariants (sorted order, bounds checking, capacity management):

```c
// telomere_pattern_api.h

#ifndef TELOMERE_PATTERN_API_H
#define TELOMERE_PATTERN_API_H

#include "telomere_transform.h"

// Read access
int      pattern_num_events(t_telomere *x);
t_float  pattern_get_event(t_telomere *x, int index);
t_float *pattern_get_buffer(t_telomere *x);  // Direct read access for bulk ops

// Write access — all mutations go through here
void     pattern_set_event(t_telomere *x, int index, t_float value);
void     pattern_append_event(t_telomere *x, t_float value);
void     pattern_resize(t_telomere *x, int new_size);
void     pattern_clear(t_telomere *x);
void     pattern_sort(t_telomere *x);  // Re-sort events chronologically

// Bulk operations
void     pattern_replace(t_telomere *x, t_float *new_data, int count);
void     pattern_copy_to(t_telomere *x, t_float *dest, int *count);

// State queries transforms may need
t_float  pattern_get_quantize_pct(t_telomere *x);
int      pattern_get_grid(t_telomere *x);
t_float  pattern_get_tempo(t_telomere *x);

#endif
```

This is the **seam** between the core engine and the transform system. Transforms depend on this API, not on the struct layout. If the internal representation changes (e.g., switching from a flat array to a ring buffer), only the API implementation needs updating.

## Core Dispatch

The core telomere object uses a single `A_GIMME` method to catch all transform messages via a dispatcher, rather than binding each transform individually:

```c
// In telomere.c

// Called for any message not handled by explicit methods
static void telomere_anything(t_telomere *x, t_symbol *s, int argc, t_atom *argv) {
    t_transform_entry *entry = telomere_lookup_transform(s);

    if (!entry) {
        pd_error(x, "telomere: unknown message '%s'", s->s_name);
        return;
    }

    // Argument count validation
    if (argc < entry->min_args) {
        pd_error(x, "telomere: '%s' requires at least %d argument(s), got %d",
                 s->s_name, entry->min_args, argc);
        return;
    }
    if (entry->max_args >= 0 && argc > entry->max_args) {
        pd_error(x, "telomere: '%s' accepts at most %d argument(s), got %d",
                 s->s_name, entry->max_args, argc);
        return;
    }

    // Dispatch
    entry->fn(x, argc, argv);
}

// In telomere_setup:
void telomere_setup(void) {
    telomere_class = class_new(
        gensym("telomere"),
        (t_newmethod)telomere_new,
        (t_freemethod)telomere_free,
        sizeof(t_telomere),
        CLASS_DEFAULT,
        A_DEFFLOAT, 0
    );

    // Only the core messages are bound directly
    class_addbang(telomere_class, telomere_bang);
    class_addfloat(telomere_class, telomere_float);

    // Everything else routes through the dispatcher
    class_addanything(telomere_class, telomere_anything);

    // Register built-in transforms
    telomere_transforms_builtins_setup();

    post("telomere: ready (%d transforms loaded)",
         telomere_registry_count());
}
```

## Writing a Transform

A transform is a self-contained `.c` file. It includes the header, defines its function, and registers itself. Here are the built-ins:

```c
// transforms/palindrome.c

#include "telomere_transform.h"
#include "telomere_pattern_api.h"

static void transform_palindrome(t_telomere *x, int argc, t_atom *argv) {
    (void)argc; (void)argv;  // No arguments needed

    int n = pattern_num_events(x);
    if (n == 0) {
        post("telomere: palindrome — pattern is empty");
        return;
    }

    // Read current events
    t_float *buf = pattern_get_buffer(x);

    // Build palindromic version: original (scaled to first half)
    // followed by reverse (scaled to second half)
    int new_count = 2 * n;
    t_float *new_buf = (t_float *)getbytes(new_count * sizeof(t_float));

    for (int i = 0; i < n; i++) {
        new_buf[i]             = buf[i] * 0.5f;             // First half
        new_buf[new_count-1-i] = 0.5f + buf[i] * 0.5f;     // Mirrored second half
    }

    pattern_replace(x, new_buf, new_count);
    freebytes(new_buf, new_count * sizeof(t_float));

    post("telomere: palindrome — %d events → %d events", n, new_count);
}

// Registration — called from telomere_transforms_builtins_setup()
void palindrome_register(void) {
    telomere_register_transform(
        gensym("palindrome"),
        transform_palindrome,
        "Append reversed pattern to create a palindromic loop",
        0,   // min args
        0    // max args
    );
}
```

```c
// transforms/rotate.c

#include "telomere_transform.h"
#include "telomere_pattern_api.h"

static void transform_rotate(t_telomere *x, int argc, t_atom *argv) {
    int n = pattern_num_events(x);
    if (n == 0) return;

    int shift = (int)atom_getfloat(&argv[0]);
    t_float offset = (t_float)shift / (t_float)n;

    for (int i = 0; i < n; i++) {
        t_float val = pattern_get_event(x, i) + offset;
        // Wrap into 0.0–1.0
        val = val - floorf(val);
        pattern_set_event(x, i, val);
    }
    pattern_sort(x);
}

void rotate_register(void) {
    telomere_register_transform(
        gensym("rotate"),
        transform_rotate,
        "Cyclically shift pattern start point by N positions",
        1, 1
    );
}
```

```c
// transforms/reverse.c

#include "telomere_transform.h"
#include "telomere_pattern_api.h"

static void transform_reverse(t_telomere *x, int argc, t_atom *argv) {
    (void)argc; (void)argv;

    int n = pattern_num_events(x);
    if (n < 2) return;

    // Reverse means event at position p moves to position (1.0 - p)
    for (int i = 0; i < n; i++) {
        t_float val = 1.0f - pattern_get_event(x, i);
        if (val < 0.0f) val += 1.0f;
        pattern_set_event(x, i, val);
    }
    pattern_sort(x);
}

void reverse_register(void) {
    telomere_register_transform(
        gensym("reverse"),
        transform_reverse,
        "Reverse the temporal order of the pattern",
        0, 0
    );
}
```

```c
// transforms/fast.c

#include "telomere_transform.h"
#include "telomere_pattern_api.h"
#include <math.h>

static void transform_fast(t_telomere *x, int argc, t_atom *argv) {
    t_float factor = atom_getfloat(&argv[0]);
    if (factor <= 0.0f) {
        pd_error(x, "telomere: fast — factor must be > 0");
        return;
    }

    int n = pattern_num_events(x);

    // For integer factors, duplicate events across sub-cycles
    // For non-integer, compress and wrap
    int reps = (int)ceilf(factor);
    int new_count = n * reps;
    t_float *new_buf = (t_float *)getbytes(new_count * sizeof(t_float));
    int idx = 0;

    for (int r = 0; r < reps; r++) {
        for (int i = 0; i < n; i++) {
            t_float pos = (pattern_get_event(x, i) + (t_float)r) / factor;
            if (pos >= 0.0f && pos < 1.0f) {
                new_buf[idx++] = pos;
            }
        }
    }

    pattern_replace(x, new_buf, idx);
    pattern_sort(x);
    freebytes(new_buf, new_count * sizeof(t_float));
}

void fast_register(void) {
    telomere_register_transform(
        gensym("fast"),
        transform_fast,
        "Compress pattern to repeat N times per cycle",
        1, 1
    );
}
```

The built-in registration aggregator:

```c
// transforms/builtins.c

// Forward declarations from each transform file
extern void palindrome_register(void);
extern void rotate_register(void);
extern void reverse_register(void);
extern void fast_register(void);
extern void slow_register(void);
extern void euclid_register(void);
extern void jitter_register(void);
extern void skip_register(void);

void telomere_transforms_builtins_setup(void) {
    palindrome_register();
    rotate_register();
    reverse_register();
    fast_register();
    slow_register();
    euclid_register();
    jitter_register();
    skip_register();
}
```

## Adding a New Transform

To add `degrade` (probabilistically remove events, with removal rate increasing each cycle):

1. Create `transforms/degrade.c`
2. Implement `transform_degrade` using only `telomere_pattern_api.h`
3. Write `degrade_register` calling `telomere_register_transform`
4. Add `extern void degrade_register(void)` and call it in `builtins.c`
5. Recompile

**Files touched in telomere core: zero.** Only `builtins.c` changes (the aggregator), and even that could be eliminated with an auto-registration scheme.

## Optional: Dynamic Loading

For true runtime extensibility without recompilation, transforms could be compiled as separate shared libraries and loaded via `dlopen`/`LoadLibrary`:

```c
// telomere_load_transform.c

#include <dlfcn.h>  // POSIX

void telomere_load_external_transform(t_symbol *path) {
    void *handle = dlopen(path->s_name, RTLD_NOW);
    if (!handle) {
        post("telomere: failed to load transform: %s", dlerror());
        return;
    }

    // Convention: every transform .so exports a setup function
    // named <basename>_register
    typedef void (*register_fn)(void);
    register_fn reg = (register_fn)dlsym(handle, "register_transform");
    if (reg) {
        reg();
    } else {
        post("telomere: loaded library has no register_transform symbol");
        dlclose(handle);
    }
    // Note: handle is intentionally not closed — the code must stay loaded
}
```

This would let users distribute individual transforms as `.pd_linux` / `.pd_darwin` files and load them via `[load_transform path/to/degrade.pd_linux]`.

---

## Self-Documentation

Because each transform registers a description string, the object can respond to a `[help]` message by iterating the registry:

```c
static void telomere_help(t_telomere *x) {
    t_transform_entry *cur = telomere_get_registry_head();
    post("telomere — available transforms:");
    post("─────────────────────────────────");
    while (cur) {
        post("  %-16s  args: %d–%d  %s",
             cur->name->s_name,
             cur->min_args,
             cur->max_args < 0 ? 999 : cur->max_args,
             cur->description);
        cur = cur->next;
    }
}
```

---

## File Structure

```
telomere/
├── telomere.c                  # Core: struct, new/free, bang, float, dispatch
├── telomere.h                  # Main struct definition
├── telomere_transform.h        # Transform interface + registration API
├── telomere_pattern_api.h      # Narrow API transforms use to read/write patterns
├── telomere_pattern_api.c      # Implementation of the pattern API
├── telomere_registry.c         # Transform registry (linked list)
├── transforms/
│   ├── builtins.c              # Aggregator that calls all *_register functions
│   ├── palindrome.c
│   ├── rotate.c
│   ├── reverse.c
│   ├── fast.c
│   ├── slow.c
│   ├── euclid.c
│   ├── jitter.c
│   ├── skip.c
│   └── degrade.c              # Example user-added transform
├── Makefile
└── README.md
```

## Principles Enforced

| Principle | How |
|---|---|
| **Open for extension** | New transforms are added as new files, registered through a uniform API |
| **Closed for modification** | Core dispatch, pattern storage, and playback never change when adding transforms |
| **Single Responsibility** | Each transform file owns exactly one transformation |
| **Dependency Inversion** | Transforms depend on the pattern API abstraction, not on the concrete struct layout |
| **Interface Segregation** | The pattern API exposes only what transforms need — no access to clock internals, outlet pointers, or modulation state |
