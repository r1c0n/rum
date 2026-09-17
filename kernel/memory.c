#include <stdint.h>
#include <rum/memory.h>

void *memcpy(void *restrict destination, const void *restrict source, size_t length)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0; i < length; ++i)
        out[i] = in[i];
    return destination;
}

void *memmove(void *destination, const void *source, size_t length)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    if ((uintptr_t)out < (uintptr_t)in) {
        for (size_t i = 0; i < length; ++i)
            out[i] = in[i];
    } else {
        for (size_t i = length; i > 0; --i)
            out[i - 1] = in[i - 1];
    }
    return destination;
}

void *memset(void *destination, int value, size_t length)
{
    unsigned char *out = destination;
    for (size_t i = 0; i < length; ++i)
        out[i] = (unsigned char)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t length)
{
    const unsigned char *a = left;
    const unsigned char *b = right;
    for (size_t i = 0; i < length; ++i)
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
    return 0;
}
