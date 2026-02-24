/* ----------------------------------------------------------------
 * Minimal Pure Data header stub for building telomere.
 *
 * Provides type definitions and EXTERN declarations compatible
 * with the real m_pd.h.  All Pd runtime functions are declared
 * here but NOT defined; their implementations are supplied by Pd
 * at load time (Linux: dynamic symbol export; macOS: -undefined
 * dynamic_lookup).
 *
 * Replace with the real m_pd.h from your Pd installation when
 * using `make PD_PATH=/path/to/pd`.
 * ---------------------------------------------------------------- */
#ifndef __m_pd_h_
#define __m_pd_h_

#include <stddef.h>

#ifdef _WIN32
#  define EXTERN __declspec(dllexport)
#else
#  define EXTERN __attribute__((visibility("default")))
#endif

typedef float  t_float;
typedef float  t_sample;
typedef int    t_int;

/* ---- atom types ---- */
#define A_FLOAT    0
#define A_SYMBOL   1
#define A_DEFFLOAT 0
#define A_DEFSYM   1

typedef struct _symbol {
    const char     *s_name;
    void          **s_thing;
    struct _symbol *s_next;
} t_symbol;

typedef union _word {
    t_float   w_float;
    t_symbol *w_symbol;
} t_word;

typedef struct _atom {
    int    a_type;
    t_word a_w;
} t_atom;

/* Atom accessors — pure struct field reads, safe to inline */
static inline t_float atom_getfloat(const t_atom *a) {
    return (a->a_type == A_FLOAT) ? a->a_w.w_float : 0.0f;
}
static inline t_float atom_getfloatarg(int which, int argc, const t_atom *argv) {
    if (which >= 0 && which < argc && argv[which].a_type == A_FLOAT)
        return argv[which].a_w.w_float;
    return 0.0f;
}
static inline t_int atom_getint(const t_atom *a) {
    return (t_int)atom_getfloat(a);
}
static inline t_int atom_getintarg(int which, int argc, const t_atom *argv) {
    return (t_int)atom_getfloatarg(which, argc, argv);
}
static inline t_symbol *atom_getsymbol(const t_atom *a) {
    return (a->a_type == A_SYMBOL) ? a->a_w.w_symbol : NULL;
}
static inline t_symbol *atom_getsymbolarg(int which, int argc, const t_atom *argv) {
    if (which >= 0 && which < argc && argv[which].a_type == A_SYMBOL)
        return argv[which].a_w.w_symbol;
    return NULL;
}

#define SETFLOAT(atom, f)  do { (atom)->a_type = A_FLOAT;  (atom)->a_w.w_float  = (f); } while(0)
#define SETSYMBOL(atom, s) do { (atom)->a_type = A_SYMBOL; (atom)->a_w.w_symbol = (s); } while(0)

/* ---- object system ---- */

/*
 * These structs MUST match vanilla Pd's layout exactly, because
 * t_object is embedded as the first member of every external's struct
 * and sizeof(t_telomere) is passed to class_new.  A size mismatch
 * causes Pd's internal writes (outlet/inlet pointers, etc.) to land
 * on the external's own fields → heap corruption → SIGSEGV.
 */

typedef struct _class *t_pd;

typedef struct _gobj {
    t_pd            *g_pd;
    struct _gobj    *g_next;
} t_gobj;

typedef struct _text {
    t_gobj           te_g;
    void            *te_binbuf;     /* t_binbuf * */
    struct _outlet  *te_outlet;
    struct _inlet   *te_inlet;
    short            te_xpix;
    short            te_ypix;
    short            te_width;
    short            te_type;
} t_object;

typedef struct _class t_class;

typedef void     (*t_method)(void);
typedef void    *(*t_newmethod)(void);
typedef void     (*t_freemethod)(void *);

#define CLASS_DEFAULT 0

/* ---- well-known symbols (provided by Pd runtime) ---- */
extern t_symbol s_bang, s_list, s_float, s_symbol, s_anything;

/* ---- Pd runtime API — implemented by Pd, resolved at load time ---- */

EXTERN t_symbol *gensym(const char *s);

/* class */
EXTERN t_class *class_new(t_symbol *name, t_newmethod newm,
    t_freemethod freem, size_t size, int flags, int arg1, ...);
EXTERN void class_addbang(t_class *c, t_method fn);
EXTERN void class_doaddfloat(t_class *c, t_method fn);
#define class_addfloat(x, y) class_doaddfloat((x), (t_method)(y))
EXTERN void class_addmethod(t_class *c, t_method fn, t_symbol *sel, int arg1, ...);
EXTERN void class_addanything(t_class *c, t_method fn);

/* object allocation */
EXTERN void *pd_new(t_class *c);

/* outlets */
typedef struct _outlet t_outlet;
EXTERN t_outlet *outlet_new(t_object *owner, t_symbol *type);
EXTERN void outlet_bang(t_outlet *o);
EXTERN void outlet_float(t_outlet *o, t_float f);
EXTERN void outlet_symbol(t_outlet *o, t_symbol *s);
EXTERN void outlet_list(t_outlet *o, t_symbol *s, int argc, t_atom *argv);
EXTERN void outlet_anything(t_outlet *o, t_symbol *s, int argc, t_atom *argv);

/* inlets */
typedef struct _inlet t_inlet;
EXTERN t_inlet *inlet_new(t_object *owner, void *dest, t_symbol *s1, t_symbol *s2);
EXTERN t_inlet *floatinlet_new(t_object *owner, t_float *fp);

/* memory */
EXTERN void *getbytes(size_t n);
EXTERN void *resizebytes(void *p, size_t old_sz, size_t new_sz);
EXTERN void  freebytes(void *p, size_t n);

/* posting */
EXTERN void post(const char *fmt, ...);
EXTERN void pd_error(void *obj, const char *fmt, ...);

/* clock */
typedef struct _clock t_clock;
EXTERN t_clock *clock_new(void *owner, t_method fn);
EXTERN void     clock_delay(t_clock *c, double ms);
EXTERN void     clock_unset(t_clock *c);
EXTERN void     clock_free(t_clock *c);
EXTERN double   clock_getlogicaltime(void);
EXTERN double   clock_gettimesince(double t);
EXTERN double   clock_getsystimeafter(double ms);

/* canvas */
typedef void t_canvas;
EXTERN t_canvas  *canvas_getcurrent(void);
EXTERN t_symbol  *canvas_getdir(t_canvas *c);

#endif /* __m_pd_h_ */
