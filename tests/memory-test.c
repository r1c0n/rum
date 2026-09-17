#include <assert.h>
#include <stdio.h>
#include <rum/memory.h>

int main(void)
{
    unsigned char data[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    unsigned char copy[8] = {0};
    assert(memcpy(copy, data, 8) == copy);
    for (size_t i = 0; i < 8; ++i)
        assert(copy[i] == i);
    assert(memset(copy + 1, 0x1FF, 6) == copy + 1);
    assert(copy[0] == 0 && copy[7] == 7);
    for (size_t i = 1; i < 7; ++i)
        assert(copy[i] == 255);

    assert(memmove(data + 2, data, 6) == data + 2);
    for (size_t i = 2; i < 8; ++i)
        assert(data[i] == i - 2);
    assert(memmove(data, data + 2, 6) == data);
    for (size_t i = 0; i < 6; ++i)
        assert(data[i] == i);
    assert(memmove(data, data, 8) == data);
    assert(memcmp(data, data, 8) == 0);
    unsigned char high = 255, low = 1;
    assert(memcmp(&high, &low, 1) > 0);
    assert(memcmp(&low, &high, 1) < 0);
    assert(memcmp(&high, &low, 0) == 0);
    assert(memset(copy, 42, 0) == copy && copy[0] == 0);
    assert(memcpy(copy, data, 0) == copy && copy[0] == 0);
    assert(memmove(copy, data, 0) == copy && copy[0] == 0);
    puts("PASS: memory copy, overlapping moves, fill, comparison, zero-length calls");
    return 0;
}
