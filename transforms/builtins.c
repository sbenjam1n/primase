/* builtins.c — Registration aggregator for all built-in transforms */

/* Extern declarations for each transform's registration function */
extern void palindrome_register(void);
extern void rotate_register(void);
extern void reverse_register(void);
extern void fast_register(void);
extern void slow_register(void);
extern void euclid_register(void);
extern void jitter_register(void);
extern void skip_register(void);
extern void degrade_register(void);
extern void ratio_register(void);
extern void ratchet_register(void);
extern void accent_register(void);
extern void drift_register(void);

void telomere_transforms_builtins_setup(void) {
    palindrome_register();
    rotate_register();
    reverse_register();
    fast_register();
    slow_register();
    euclid_register();
    jitter_register();
    skip_register();
    degrade_register();
    ratio_register();
    ratchet_register();
    accent_register();
    drift_register();
}
