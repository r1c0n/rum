#include "stream.h"
int command_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
rum_result_t command_count(const char *text, uint32_t *value)
{
    if (!*text) return -RUM_EINVAL;
    uint32_t n = 0;
    do {
        unsigned digit = (unsigned char)*text++ - '0';
        if (digit > 9) return -RUM_EINVAL;
        if (n > (UINT32_MAX - digit) / 10) return -RUM_EOVERFLOW;
        n = n * 10 + digit;
    } while (*text);
    *value = n; return 0;
}
rum_result_t command_input_open(struct command_input *input, const char *path)
{
    *input = (struct command_input){0};
    if (!path || command_equal(path, "-")) { input->handle = RUM_STDIN; return 0; }
    rum_result_t handle = rum_open(path, RUM_OPEN_READ, 0);
    if (handle < 0) return handle;
    input->own = 1; input->handle = (rum_handle_t)handle; return 0;
}
rum_result_t command_input_next(struct command_input *input, unsigned char *byte)
{
    if (input->cursor == input->used) {
        rum_result_t n = rum_read(input->handle, input->buffer, sizeof input->buffer);
        if (n <= 0) return n;
        input->cursor = 0; input->used = (unsigned)n;
    }
    *byte = input->buffer[input->cursor++]; return 1;
}
rum_result_t command_input_close(struct command_input *input)
{
    int own = input->own; input->own = 0;
    return own ? rum_close(input->handle) : 0;
}
static unsigned escaped(unsigned char c, char *out)
{
    if ((c >= 32 && c <= 126) || c == '\n' || c == '\t') { out[0] = (char)c; return 1; }
    const char hex[] = "0123456789ABCDEF";
    out[0] = '\\'; out[1] = 'x'; out[2] = hex[c >> 4]; out[3] = hex[c & 15]; return 4;
}
unsigned text_decode(struct text_decoder *decoder, unsigned char c, char out[8])
{
    unsigned n = 0;
    if (decoder->cr && c != '\n') n = escaped('\r', out);
    decoder->cr = c == '\r';
    return decoder->cr ? n : n + escaped(c, out + n);
}
unsigned text_finish(struct text_decoder *decoder, char out[8])
{
    unsigned n = decoder->cr ? escaped('\r', out) : 0; decoder->cr = 0; return n;
}
rum_result_t command_copy(struct command_input *input, uint32_t lines, int raw)
{
    rum_result_t tty = rum_isatty(RUM_STDOUT);
    if (tty < 0) return tty;
    raw |= !tty;
    char output[512]; unsigned used = 0; int last = '\n';
    struct text_decoder decoder = {0}; rum_result_t result = 0;
    while (lines) {
        unsigned char byte;
        result = command_input_next(input, &byte);
        if (result <= 0) break;
        char decoded[8]; unsigned n;
        last = byte;
        if (raw) { decoded[0] = (char)byte; n = 1; } else n = text_decode(&decoder, byte, decoded);
        if (used + n > sizeof output) { tool_output(output, used); used = 0; }
        for (unsigned i = 0; i < n; ++i) output[used++] = decoded[i];
        if (byte == '\n' && lines != UINT32_MAX) --lines;
    }
    if (!raw) {
        char tail[8]; unsigned n = text_finish(&decoder, tail);
        if (used + n + 1 > sizeof output) { tool_output(output, used); used = 0; }
        for (unsigned i = 0; i < n; ++i) output[used++] = tail[i];
        if (last != '\n') output[used++] = '\n';
    }
    if (used) tool_output(output, used);
    return result < 0 ? result : 0;
}
