#ifndef RUM_KEYBOARD_DECODE_H
#define RUM_KEYBOARD_DECODE_H

#include <stdbool.h>
#include <stdint.h>

/* Controller-translated scan code set 1; a US QWERTY layout. */
struct keyboard_decoder {
    bool left_shift, right_shift, caps_lock, caps_down, extended;
    bool left_ctrl, right_ctrl, left_alt, right_alt;
    uint8_t pause_remaining;
};

/* Return an ASCII character for a make code, or zero for non-text events. */
char keyboard_decode(struct keyboard_decoder *state, uint8_t scancode);

#endif
