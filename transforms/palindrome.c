/* palindrome.c — Append reversed pattern to create a palindromic loop */

#include "../primase_transform.h"
#include "../primase_pattern_api.h"

static void transform_palindrome(t_primase *x, int argc, t_atom *argv) {
    (void)argc; (void)argv;

    int n = pattern_num_events(x);
    if (n == 0) return;

    int new_size = n * 2;
    t_float buf[PRIMASE_MAX_EVENTS];
    t_float vel[PRIMASE_MAX_EVENTS];
    int count;
    pattern_copy_to(x, buf, vel, &count);

    pattern_resize(x, new_size);
    for (int i = 0; i < n; i++) {
        /* First half: compress to 0.0–0.5 */
        pattern_set_event(x, i, buf[i] * 0.5f);
        pattern_set_velocity(x, i, vel[i]);
        /* Second half: mirror into 0.5–1.0 */
        pattern_set_event(x, new_size - 1 - i, 0.5f + buf[i] * 0.5f);
        pattern_set_velocity(x, new_size - 1 - i, vel[i]);
    }

    pattern_sort(x);
}

void palindrome_register(void) {
    primase_register_transform(
        gensym("palindrome"),
        transform_palindrome,
        "Append reversed pattern to create palindromic loop",
        0, 0
    );
}
