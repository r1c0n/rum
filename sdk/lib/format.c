#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>

/* Integer-only formatting. Unsupported conversions fail instead of consuming
   a variadic argument with an incorrect type. No floating-point runtime. */
struct output {
    char *text;
    size_t capacity, count, pending;
    FILE *file;
    char buffer[256];
    int error;
};
static void drain(struct output *out)
{
    if (out->pending && fwrite(out->buffer, 1, out->pending, out->file) != out->pending) out->error = 1;
    out->pending = 0;
}
static void span(struct output *out, const char *text, size_t bytes)
{
    if (out->error) return;
    if (bytes > (size_t)INT_MAX - out->count) { errno = EOVERFLOW; out->error = 1; return; }
    if (out->file) {
        for (size_t i = 0; i < bytes && !out->error; ++i) {
            out->buffer[out->pending++] = text[i];
            if (out->pending == sizeof out->buffer) drain(out);
        }
    } else if (out->capacity && out->count < out->capacity - 1) {
        size_t available = out->capacity - 1 - out->count;
        memcpy(out->text + out->count, text, bytes < available ? bytes : available);
    }
    out->count += bytes;
}
static void repeat(struct output *out, char value, size_t count)
{
    if (!out->file) {
        if (out->error) return;
        if (count > (size_t)INT_MAX - out->count) { errno = EOVERFLOW; out->error = 1; return; }
        if (out->capacity && out->count < out->capacity - 1) {
            size_t available = out->capacity - 1 - out->count;
            memset(out->text + out->count, value, count < available ? count : available);
        }
        out->count += count;
    } else {
        char text[64]; memset(text, value, sizeof text);
        while (count && !out->error) { size_t n = count < sizeof text ? count : sizeof text; span(out, text, n); count -= n; }
    }
}
static int decimal(const char **format, size_t *value)
{
    *value = 0;
    while (**format >= '0' && **format <= '9') {
        unsigned digit = (unsigned)(*(*format)++ - '0');
        if (*value > ((size_t)INT_MAX - digit) / 10) { errno = EOVERFLOW; return 0; }
        *value = *value * 10 + digit;
    }
    return 1;
}
static int format(struct output *out, const char *text, va_list arguments)
{
    while (*text && !out->error) {
        if (*text != '%') { const char *start = text; while (*text && *text != '%') ++text; span(out, start, (size_t)(text - start)); continue; }
        ++text;
        int left = 0, zero = 0, plus = 0, blank = 0, alternate = 0;
        for (;; ++text) {
            if (*text == '-') left = 1; else if (*text == '0') zero = 1;
            else if (*text == '+') plus = 1; else if (*text == ' ') blank = 1;
            else if (*text == '#') alternate = 1; else break;
        }
        size_t width = 0, precision = 0; int specified = 0;
        if (*text == '*') {
            int value = va_arg(arguments, int); ++text;
            if (value == INT_MIN) { errno = EOVERFLOW; out->error = 1; break; }
            if (value < 0) { left = 1; value = -value; } width = (size_t)value;
        } else if (!decimal(&text, &width)) { out->error = 1; break; }
        if (*text == '.') {
            ++text; specified = 1;
            if (*text == '*') {
                int value = va_arg(arguments, int); ++text;
                if (value < 0) specified = 0; else precision = (size_t)value;
            } else if (!decimal(&text, &precision)) { out->error = 1; break; }
        }
        enum { NORMAL, SMALL, TINY, LONG, WIDE, SIZE } length = NORMAL;
        if (*text == 'l') { length = LONG; if (*++text == 'l') { length = WIDE; ++text; } }
        else if (*text == 'h') { length = SMALL; if (*++text == 'h') { length = TINY; ++text; } }
        else if (*text == 'z') { length = SIZE; ++text; }
        char kind = *text;
        if (!kind) { errno = EINVAL; out->error = 1; break; }
        ++text;
        char digits[65], prefix[3]; size_t n = 0, prefixes = 0, leading = 0;
        const char *data = digits;
        if ((kind == 's' || kind == 'c' || kind == '%') && length != NORMAL) {
            errno = EINVAL; out->error = 1; break;
        }
        if (kind == 's') { data = va_arg(arguments, const char *); if (!data) data = "(null)"; n = specified ? strnlen(data, precision) : strlen(data); zero = 0; }
        else if (kind == 'c' || kind == '%') { digits[0] = kind == '%' ? '%' : (char)va_arg(arguments, int); n = 1; zero = 0; }
        else if (kind == 'd' || kind == 'i' || kind == 'u' || kind == 'x' || kind == 'X' || kind == 'o' || kind == 'p') {
            uint64_t value; int negative = 0;
            if (kind == 'p') {
                if (length != NORMAL) { errno = EINVAL; out->error = 1; break; }
                value = (uintptr_t)va_arg(arguments, void *);
            } else if (kind == 'd' || kind == 'i') {
                int64_t number;
                if (length == WIDE) number = va_arg(arguments, long long);
                else if (length == LONG) number = va_arg(arguments, long);
                else if (length == SIZE) number = va_arg(arguments, ptrdiff_t);
                else { number = va_arg(arguments, int); if (length == SMALL) number = (short)number; if (length == TINY) number = (signed char)number; }
                negative = number < 0;
                value = negative ? 0u - (uint64_t)number : (uint64_t)number;
                if (negative || plus || blank) prefix[prefixes++] = negative ? '-' : plus ? '+' : ' ';
            } else {
                if (length == WIDE) value = va_arg(arguments, unsigned long long);
                else if (length == LONG) value = va_arg(arguments, unsigned long);
                else if (length == SIZE) value = va_arg(arguments, size_t);
                else { value = va_arg(arguments, unsigned); if (length == SMALL) value = (unsigned short)value; if (length == TINY) value = (unsigned char)value; }
            }
            unsigned base = kind == 'o' ? 8 : kind == 'x' || kind == 'X' || kind == 'p' ? 16 : 10;
            const char *alphabet = kind == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            uint64_t original = value;
            if (value || !specified || precision) do { digits[n++] = alphabet[value % base]; value /= base; } while (value);
            for (size_t i = 0; i < n / 2; ++i) { char c = digits[i]; digits[i] = digits[n - 1 - i]; digits[n - 1 - i] = c; }
            leading = specified && precision > n ? precision - n : 0;
            if (base == 16 && (kind == 'p' || (alternate && original))) { prefix[prefixes++] = '0'; prefix[prefixes++] = kind == 'X' ? 'X' : 'x'; }
            if (base == 8 && alternate && !leading && (!n || digits[0] != '0')) leading = 1;
            if (specified) zero = 0;
        } else { errno = EINVAL; out->error = 1; break; }
        size_t total = prefixes + leading + n, padding = width > total ? width - total : 0;
        if (!left && !zero) repeat(out, ' ', padding);
        span(out, prefix, prefixes);
        if (!left && zero) repeat(out, '0', padding);
        repeat(out, '0', leading); span(out, data, n);
        if (left) repeat(out, ' ', padding);
    }
    if (out->file && !out->error) drain(out);
    if (!out->file && out->capacity) out->text[out->count < out->capacity ? out->count : out->capacity - 1] = 0;
    return out->error ? -1 : (int)out->count;
}
int vsnprintf(char *restrict out, size_t capacity, const char *restrict text, va_list args)
{
    if ((!out && capacity) || !text) { errno = EINVAL; return -1; }
    struct output target = { .text = out, .capacity = capacity };
    va_list copy; va_copy(copy, args); int result = format(&target, text, copy); va_end(copy); return result;
}
int snprintf(char *restrict out, size_t capacity, const char *restrict text, ...)
{
    va_list args; va_start(args, text); int result = vsnprintf(out, capacity, text, args); va_end(args); return result;
}
int vfprintf(FILE *restrict file, const char *restrict text, va_list args)
{
    if (!file || !text) { errno = EINVAL; return -1; }
    struct output target = { .file = file };
    va_list copy; va_copy(copy, args); int result = format(&target, text, copy); va_end(copy); return result;
}
int fprintf(FILE *restrict file, const char *restrict text, ...)
{
    va_list args; va_start(args, text); int result = vfprintf(file, text, args); va_end(args); return result;
}
int printf(const char *restrict text, ...)
{
    va_list args; va_start(args, text); int result = vfprintf(stdout, text, args); va_end(args); return result;
}
