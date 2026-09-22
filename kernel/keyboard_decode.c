#include <rum/keyboard_decode.h>

static const char normal[128] = {
    [0x02]='1', [0x03]='2', [0x04]='3', [0x05]='4', [0x06]='5',
    [0x07]='6', [0x08]='7', [0x09]='8', [0x0A]='9', [0x0B]='0',
    [0x0C]='-', [0x0D]='=', [0x0E]='\b', [0x0F]='\t',
    [0x10]='q', [0x11]='w', [0x12]='e', [0x13]='r', [0x14]='t',
    [0x15]='y', [0x16]='u', [0x17]='i', [0x18]='o', [0x19]='p',
    [0x1A]='[', [0x1B]=']', [0x1C]='\n',
    [0x1E]='a', [0x1F]='s', [0x20]='d', [0x21]='f', [0x22]='g',
    [0x23]='h', [0x24]='j', [0x25]='k', [0x26]='l',
    [0x27]=';', [0x28]='\'', [0x29]='`', [0x2B]='\\',
    [0x2C]='z', [0x2D]='x', [0x2E]='c', [0x2F]='v', [0x30]='b',
    [0x31]='n', [0x32]='m', [0x33]=',', [0x34]='.', [0x35]='/',
    [0x37]='*', [0x39]=' ', [0x4A]='-', [0x4E]='+'
};

static const char shifted[128] = {
    [0x02]='!', [0x03]='@', [0x04]='#', [0x05]='$', [0x06]='%',
    [0x07]='^', [0x08]='&', [0x09]='*', [0x0A]='(', [0x0B]=')',
    [0x0C]='_', [0x0D]='+', [0x1A]='{', [0x1B]='}',
    [0x27]=':', [0x28]='"', [0x29]='~', [0x2B]='|',
    [0x33]='<', [0x34]='>', [0x35]='?'
};

char keyboard_decode(struct keyboard_decoder *state, uint8_t scancode)
{
    if (state->pause_remaining) {
        --state->pause_remaining;
        return 0;
    }
    if (scancode == 0xE1) {
        state->pause_remaining = 5; /* Pause: E1 1D 45 E1 9D C5. */
        state->extended = false;
        return 0;
    }
    if (scancode == 0xE0) {
        state->extended = true;
        return 0;
    }
    bool extended = state->extended;
    state->extended = false;
    bool released = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;
    if (code == 0x1D) {
        if (extended) state->right_ctrl = !released;
        else state->left_ctrl = !released;
        return 0;
    }
    if (code == 0x38) {
        if (extended) state->right_alt = !released;
        else state->left_alt = !released;
        return 0;
    }
    if (!extended && (code == 0x2A || code == 0x36)) {
        if (code == 0x2A) state->left_shift = !released;
        else state->right_shift = !released;
        return 0;
    }
    if (!extended && code == 0x3A) {
        if (!released && !state->caps_down)
            state->caps_lock = !state->caps_lock;
        state->caps_down = !released;
        return 0;
    }
    bool control = state->left_ctrl || state->right_ctrl;
    if (!released && !extended && control && !state->left_alt && !state->right_alt &&
        code == 0x2E) return '\x03'; /* Ctrl+C */
    if (released || control || state->left_alt || state->right_alt)
        return 0;
    if (extended) {
        /* Ignore navigation and Print Screen's fake shifts. */
        if (code == 0x1C) return '\n';
        if (code == 0x35) return '/';
        return 0;
    }
    char character = normal[code];
    bool shift = state->left_shift || state->right_shift;
    if (character >= 'a' && character <= 'z') {
        if (shift != state->caps_lock)
            character = (char)(character - 'a' + 'A');
    } else if (shift && shifted[code]) {
        character = shifted[code];
    }
    return character;
}
