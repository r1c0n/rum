#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../system/lib/pager.h"
int cat_main(int, char **); int head_main(int, char **); int wc_main(int, char **);
int hexdump_main(int, char **); int more_main(int, char **); int help_main(int, char **);
static const unsigned char *source; static unsigned length, position, chunk, reads, closes;
static int tty, interactive, read_error, close_error, last_error, begun, ended;
static unsigned char output[131072]; static unsigned output_bytes;
static char screen[24][81], frames[16][24][81]; static unsigned frame_count;
static unsigned keys[16], key_count, key_cursor;
rum_result_t rum_open(const char *path, uint32_t access, uint32_t flags)
{
    assert(access == RUM_OPEN_READ && !flags); return !strcmp(path, "INPUT") ? 3 : -RUM_ENOENT;
}
rum_result_t rum_close(rum_handle_t handle) { assert(handle == 3); ++closes; return close_error; }
rum_result_t rum_read(rum_handle_t handle, void *buffer, uint32_t bytes)
{
    assert(handle == 0 || handle == 3); ++reads;
    if (read_error) return read_error;
    if (bytes > chunk) bytes = chunk;
    if (bytes > length - position) bytes = length - position;
    memcpy(buffer, source + position, bytes); position += bytes; return (rum_result_t)bytes;
}
rum_result_t rum_isatty(rum_handle_t handle) { (void)handle; return tty; }
unsigned tool_length(const char *s) { return (unsigned)strlen(s); }
void tool_output(const void *data, unsigned bytes)
{
    assert(bytes < sizeof output - output_bytes);
    memcpy(output + output_bytes, data, bytes); output_bytes += bytes; output[output_bytes] = 0;
}
void tool_print(const char *s) { tool_output(s, (unsigned)strlen(s)); }
void tool_unsigned(uint64_t value) { char text[32]; snprintf(text, sizeof text, "%" PRIu64, value); tool_print(text); }
void tool_error(const char *name, rum_result_t error) { (void)name; last_error = error; }
rum_result_t rum_terminal_query(struct rum_terminal_capabilities *caps)
{
    *caps = (struct rum_terminal_capabilities){ .width = 80, .height = 24 }; return 0;
}
rum_result_t rum_terminal_begin(void) { if (!interactive) return -RUM_EACCES; ++begun; return 0; }
rum_result_t rum_terminal_end(void) { ++ended; return 0; }
rum_result_t rum_terminal_cells(uint32_t x, uint32_t y, const struct rum_terminal_cell *cells, uint32_t count)
{
    assert(y < 24 && x + count <= 80);
    for (unsigned i = 0; i < count; ++i) screen[y][x + i] = (char)cells[i].character;
    return 0;
}
rum_result_t rum_terminal_read(struct rum_terminal_event *event, uint32_t capacity, uint32_t wait, uint32_t *ticks)
{
    (void)wait; (void)ticks; assert(capacity == 1 && key_cursor < key_count && frame_count < 16);
    memcpy(frames[frame_count++], screen, sizeof screen);
    unsigned key = keys[key_cursor++];
    *event = (struct rum_terminal_event){ .type = key < 256 ? RUM_TERM_TEXT_EVENT : RUM_TERM_KEY_EVENT, .value = key };
    return 1;
}
static void reset(const void *data, unsigned bytes, int console)
{
    source = data; length = bytes; position = reads = closes = output_bytes = 0; chunk = 7;
    tty = console; interactive = read_error = close_error = last_error = begun = ended = 0;
    frame_count = key_cursor = 0; key_count = 1; keys[0] = 'q';
    memset(output, 0, sizeof output); memset(screen, 0, sizeof screen);
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *cat[] = {"cat", "INPUT"}; char *head[] = {"head", "-n", "1", "INPUT"};
    const unsigned char binary[] = {'a','\r','\n','b',0,0xff,'\r'};
    reset(binary, sizeof binary, 0);
    assert(!cat_main(2, cat) && closes == 1 && output_bytes == sizeof binary && !memcmp(output, binary, sizeof binary));
    reset(binary, sizeof binary, 1);
    assert(!cat_main(2, cat) && !strcmp((char *)output, "a\nb\\x00\\xFF\\x0D\n"));
    reset(binary, sizeof binary, 0);
    assert(!head_main(4, head) && output_bytes == 3 && !memcmp(output, "a\r\n", 3));
    head[2] = "0"; reset(binary, sizeof binary, 0);
    assert(!head_main(4, head) && !reads && !output_bytes && closes == 1);
    head[2] = "4294967296"; reset(binary, sizeof binary, 0); assert(head_main(4, head) == 1 && !reads);
    char *wc[] = {"wc", "INPUT"}; reset("a b\nc\r\n", 7, 0);
    assert(!wc_main(2, wc) && !strcmp((char *)output, "2 3 7 INPUT\n"));
    char *word_only[] = {"wc", "-w"}; reset("", 0, 0);
    assert(!wc_main(2, word_only) && !strcmp((char *)output, "0\n") && !closes);
    char *hex[] = {"hexdump", "INPUT"}; reset(binary, sizeof binary, 0);
    assert(!hexdump_main(2, hex) && strstr((char *)output, "00000000  61 0D 0A 62 00 FF 0D") &&
           strstr((char *)output, "|a..b...|"));
    char *more[] = {"more", "INPUT"}; reset(binary, sizeof binary, 0);
    assert(!more_main(2, more) && output_bytes == sizeof binary && !memcmp(output, binary, sizeof binary));
    reset(binary, sizeof binary, 0); read_error = -RUM_EIO;
    assert(cat_main(2, cat) == 1 && last_error == -RUM_EIO && closes == 1);
    reset(binary, sizeof binary, 0); close_error = -RUM_EIO;
    assert(wc_main(2, wc) == 1 && last_error == -RUM_EIO && !output_bytes);

    unsigned char boundary[520]; memset(boundary, 'x', 511); memcpy(boundary + 511, "\r\nend\r\n", 7);
    reset(boundary, 518, 1); chunk = 512;
    assert(!cat_main(2, cat) && output_bytes == 516 && !memcmp(output + 511, "\nend\n", 5));
    char *help[] = {"help", "--all"}; reset("", 0, 1);
    assert(!help_main(2, help) && strstr((char *)output, "text:") && strstr((char *)output, "hexdump [path]") && !begun);
    reset("", 0, 1); interactive = 1; key_count = 4;
    keys[0] = ' '; keys[1] = RUM_KEY_PAGEUP; keys[2] = '\n'; keys[3] = 'q';
    assert(!help_main(1, help) && begun == 1 && ended == 1 && frame_count == 4);
    assert(!strcmp(frames[0][1], frames[2][1]) && strcmp(frames[0][1], frames[1][1]));

    char lines[4000]; unsigned used = 0;
    for (unsigned i = 0; i < 300; ++i) used += (unsigned)sprintf(lines + used, "line%03u\n", i);
    reset(lines, used, 1); interactive = 1; key_count = 15;
    for (unsigned i = 0; i < 13; ++i) keys[i] = ' ';
    keys[13] = RUM_KEY_PAGEUP; keys[14] = 'q';
    assert(!more_main(2, more) && ended == 1 && !strncmp(frames[13][1], "line286", 7) &&
           !strncmp(frames[14][1], "line264", 7));
    puts("PASS: command bytes, CRLF boundaries, limits, errors, paging and bounded history");
    return 0;
}
