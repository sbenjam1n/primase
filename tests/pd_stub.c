/* tests/pd_stub.c — Minimal Pd runtime stubs for unit testing.
 *
 * Provides implementations of the Pd API functions used by
 * telomere_pattern_api.c, telomere_registry.c, and the transform files,
 * without requiring a real Pd installation or Pd headers beyond our stub.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "m_pd.h"

/* ---- Memory ---- */

void *getbytes(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "pd_stub: getbytes: out of memory\n"); exit(1); }
    return p;
}

void *resizebytes(void *p, size_t old_sz, size_t new_sz) {
    (void)old_sz;
    void *q = realloc(p, new_sz ? new_sz : 1);
    if (!q) { fprintf(stderr, "pd_stub: resizebytes: out of memory\n"); exit(1); }
    return q;
}

void freebytes(void *p, size_t n) {
    (void)n;
    free(p);
}

/* ---- Symbol intern table ---- */

#define SYM_CAPACITY 256
static char   sym_names[SYM_CAPACITY][64];
static t_symbol sym_store[SYM_CAPACITY];
static int    sym_count = 0;

t_symbol *gensym(const char *s) {
    for (int i = 0; i < sym_count; i++)
        if (strcmp(sym_names[i], s) == 0)
            return &sym_store[i];
    if (sym_count >= SYM_CAPACITY) {
        fprintf(stderr, "pd_stub: gensym table full\n");
        exit(1);
    }
    strncpy(sym_names[sym_count], s, 63);
    sym_names[sym_count][63] = '\0';
    sym_store[sym_count].s_name  = sym_names[sym_count];
    sym_store[sym_count].s_thing = NULL;
    sym_store[sym_count].s_next  = NULL;
    return &sym_store[sym_count++];
}

/* ---- Logging ---- */

void post(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

void pd_error(void *obj, const char *fmt, ...) {
    (void)obj;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "pd_error: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---- Outlet stubs ---- */

void outlet_bang(t_outlet *o) { (void)o; }
void outlet_float(t_outlet *o, t_float f) { (void)o; (void)f; }
void outlet_symbol(t_outlet *o, t_symbol *s) { (void)o; (void)s; }
void outlet_list(t_outlet *o, t_symbol *s, int argc, t_atom *argv) {
    (void)o; (void)s; (void)argc; (void)argv;
}
void outlet_anything(t_outlet *o, t_symbol *s, int argc, t_atom *argv) {
    (void)o; (void)s; (void)argc; (void)argv;
}
