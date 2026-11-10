#include <string.h>
#include <stdlib.h>
#include <stdint.h>

void *memcpy(void *restrict target, const void *restrict source, size_t count)
{
    unsigned char *out = target; const unsigned char *in = source;
    for (size_t i = 0; i < count; ++i) out[i] = in[i];
    return target;
}
void *memmove(void *target, const void *source, size_t count)
{
    unsigned char *out = target; const unsigned char *in = source;
    if ((uintptr_t)out < (uintptr_t)in) for (size_t i = 0; i < count; ++i) out[i] = in[i];
    else if (out != in) while (count) { --count; out[count] = in[count]; }
    return target;
}
void *memset(void *target, int value, size_t count)
{
    unsigned char *out = target;
    for (size_t i = 0; i < count; ++i) out[i] = (unsigned char)value;
    return target;
}
int memcmp(const void *first, const void *second, size_t count)
{
    const unsigned char *a = first, *b = second;
    for (size_t i = 0; i < count; ++i) if (a[i] != b[i]) return (int)a[i] - b[i];
    return 0;
}
void *memchr(const void *source, int value, size_t count)
{
    const unsigned char *in = source;
    for (size_t i = 0; i < count; ++i) if (in[i] == (unsigned char)value) return (void *)(in + i);
    return NULL;
}
size_t strlen(const char *text) { size_t n = 0; while (text[n]) ++n; return n; }
size_t strnlen(const char *text, size_t limit) { size_t n = 0; while (n < limit && text[n]) ++n; return n; }
char *strcpy(char *restrict target, const char *restrict source)
{
    size_t i = 0; do { target[i] = source[i]; } while (source[i++]); return target;
}
char *strncpy(char *restrict target, const char *restrict source, size_t count)
{
    size_t i = 0; for (; i < count && source[i]; ++i) target[i] = source[i];
    for (; i < count; ++i) target[i] = 0;
    return target;
}
char *strcat(char *restrict target, const char *restrict source) { strcpy(target + strlen(target), source); return target; }
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t count)
{
    for (size_t i = 0; i < count; ++i) if (!a[i] || a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    return 0;
}
char *strchr(const char *text, int value)
{
    do { if ((unsigned char)*text == (unsigned char)value) return (char *)text; } while (*text++);
    return NULL;
}
char *strrchr(const char *text, int value)
{
    const char *found = NULL;
    do { if ((unsigned char)*text == (unsigned char)value) found = text; } while (*text++);
    return (char *)found;
}
char *strstr(const char *text, const char *needle)
{
    size_t length = strlen(needle);
    if (!length) return (char *)text;
    for (; *text; ++text) if (!strncmp(text, needle, length)) return (char *)text;
    return NULL;
}
char *strdup(const char *text)
{
    size_t bytes = strlen(text) + 1;
    char *copy = malloc(bytes); if (copy) memcpy(copy, text, bytes); return copy;
}
