#include <assert.h>
#include <stdio.h>
#include <rum/keyboard_decode.h>

int main(void)
{
    struct keyboard_decoder state = {0};
    assert(keyboard_decode(&state, 0x1E) == 'a');
    const uint8_t navigation[] = {0x4B, 0x4D, 0x48, 0x50, 0x47, 0x4F, 0x53, 0x49, 0x51};
    for (unsigned i = 0; i < sizeof navigation; ++i) {
        assert(!keyboard_decode_event(&state, 0xE0));
        assert(keyboard_decode_event(&state, navigation[i]) == RUM_KEY_LEFT + i);
        assert(!keyboard_decode_event(&state, 0xE0));
        assert(!keyboard_decode_event(&state, navigation[i] | 0x80));
    }
    assert(keyboard_decode(&state, 0x9E) == 0);
    assert(keyboard_decode(&state, 0x2A) == 0);
    assert(keyboard_decode(&state, 0x1E) == 'A');
    assert(keyboard_decode(&state, 0x02) == '!');
    assert(keyboard_decode(&state, 0x36) == 0);
    assert(keyboard_decode(&state, 0xAA) == 0);
    assert(keyboard_decode(&state, 0x30) == 'B'); /* Right shift still held. */
    assert(keyboard_decode(&state, 0xB6) == 0);
    assert(keyboard_decode(&state, 0x30) == 'b');
    assert(keyboard_decode(&state, 0x3A) == 0);
    assert(keyboard_decode(&state, 0x3A) == 0); /* Caps repeat doesn't toggle. */
    assert(keyboard_decode(&state, 0xBA) == 0);
    assert(keyboard_decode(&state, 0x2E) == 'C');
    assert(keyboard_decode(&state, 0x02) == '1');
    assert(keyboard_decode(&state, 0x2A) == 0);
    assert(keyboard_decode(&state, 0x2E) == 'c');
    assert(keyboard_decode(&state, 0xAA) == 0);
    assert(keyboard_decode(&state, 0x3A) == 0);
    assert(keyboard_decode(&state, 0xBA) == 0);
    assert(keyboard_decode(&state, 0x2E) == 'c');

    const uint8_t ignored[] = {0xE0,0x48,0xE0,0xC8, /* Arrow */
        0xE0,0x2A,0xE0,0x37,0xE0,0xB7,0xE0,0xAA, /* Print Screen */
        0xE1,0x1D,0x45,0xE1,0x9D,0xC5}; /* Pause */
    for (unsigned i = 0; i < sizeof ignored; ++i)
        assert(keyboard_decode(&state, ignored[i]) == 0);
    assert(keyboard_decode(&state, 0x1E) == 'a');
    assert(keyboard_decode(&state, 0xE0) == 0);
    assert(keyboard_decode(&state, 0x1C) == '\n');
    assert(keyboard_decode(&state, 0xE0) == 0);
    assert(keyboard_decode(&state, 0x35) == '/');
    assert(keyboard_decode(&state, 0x0E) == '\b');
    assert(keyboard_decode(&state, 0x0F) == '\t');
    assert(keyboard_decode(&state, 0x1C) == '\n');

    assert(keyboard_decode(&state, 0x1D) == 0);
    assert(keyboard_decode(&state, 0x1E) == 0);
    assert(keyboard_decode(&state, 0x2E) == '\x03');
    assert(keyboard_decode(&state, 0xAE) == 0);
    assert(keyboard_decode(&state, 0x9D) == 0);
    assert(keyboard_decode(&state, 0x38) == 0);
    assert(keyboard_decode(&state, 0x1E) == 0);
    assert(keyboard_decode(&state, 0xB8) == 0);
    assert(keyboard_decode(&state, 0xE0) == 0);
    assert(keyboard_decode(&state, 0x1D) == 0);
    assert(keyboard_decode(&state, 0x1E) == 0);
    assert(keyboard_decode(&state, 0xE0) == 0);
    assert(keyboard_decode(&state, 0x9D) == 0);
    assert(keyboard_decode(&state, 0x1E) == 'a');
    puts("PASS: keyboard make/break, both shifts, Caps Lock, punctuation, E0/E1, Ctrl/Alt");
    return 0;
}
