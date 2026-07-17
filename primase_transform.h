/* primase_transform.h — Transform interface definition */
#ifndef PRIMASE_TRANSFORM_H
#define PRIMASE_TRANSFORM_H

#include "primase.h"

/* Every transform function has this signature */
typedef void (*t_transform_fn)(t_primase *x, int argc, t_atom *argv);

/* Registry entry — linked list node */
typedef struct _transform_entry {
    t_symbol               *name;
    t_transform_fn          fn;
    const char             *description;
    int                     min_args;
    int                     max_args;   /* -1 = unlimited */
    struct _transform_entry *next;
} t_transform_entry;

/* --- Registry API --- */
void              primase_register_transform(t_symbol *name, t_transform_fn fn,
                      const char *description, int min_args, int max_args);
t_transform_entry *primase_lookup_transform(t_symbol *name);
t_transform_entry *primase_get_registry_head(void);
void              primase_registry_free(void);

/* --- Builtins aggregator --- */
void primase_transforms_builtins_setup(void);

#endif /* PRIMASE_TRANSFORM_H */
