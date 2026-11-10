#include "pager.h"
#define HISTORY 256u
static struct {
    char lines[HISTORY][RUM_TERM_MAX_CELLS], partial[RUM_TERM_MAX_CELLS];
    unsigned width, height, total, first, top, column;
    int eof, wrapped;
    rum_result_t error;
} view;
rum_result_t pager_text_next(void *source, char *c)
{
    struct pager_text *text = source;
    if (!text->text[text->offset]) return 0;
    *c = text->text[text->offset++]; return 1;
}
rum_result_t pager_file_next(void *source, char *c)
{
    struct pager_file *file = source;
    while (file->cursor == file->used) {
        if (file->finished) return 0;
        unsigned char byte; rum_result_t result = command_input_next(file->input, &byte);
        if (result < 0) return result;
        file->cursor = 0;
        if (!result) { file->finished = 1; file->used = text_finish(&file->decoder, file->pending); }
        else file->used = text_decode(&file->decoder, byte, file->pending);
    }
    *c = file->pending[file->cursor++]; return 1;
}
static void push(void)
{
    if (view.total == UINT32_MAX - 64u) { view.error = -RUM_EOVERFLOW; view.eof = 1; return; }
    unsigned slot = view.total % HISTORY;
    for (unsigned i = 0; i < view.width; ++i) view.lines[slot][i] = i < view.column ? view.partial[i] : ' ';
    ++view.total;
    if (view.total > HISTORY) view.first = view.total - HISTORY;
    view.column = 0;
}
static void character(char c)
{
    if (view.wrapped && c == '\n') { view.wrapped = 0; return; }
    view.wrapped = 0;
    if (c == '\n') { push(); return; }
    unsigned spaces = c == '\t' ? 8 - view.column % 8 : 1;
    while (spaces--) {
        view.wrapped = 0;
        view.partial[view.column++] = c == '\t' ? ' ' : c;
        if (view.column == view.width) { push(); view.wrapped = 1; }
    }
}
static void ensure(unsigned rows, pager_next next, void *source)
{
    while (!view.eof && view.total < rows) {
        char c; rum_result_t result = next(source, &c);
        if (result <= 0) {
            view.eof = 1; view.error = result;
            if (view.column) push();
        } else character(c);
    }
}
static rum_result_t row(unsigned y, const char *text, unsigned foreground, unsigned background, int syntax)
{
    struct rum_terminal_cell cells[RUM_TERM_MAX_CELLS];
    unsigned n = 0; while (n < view.width && text[n]) ++n;
    int heading = n && text[n - 1] == ':';
    unsigned split = 0;
    while (split < n && text[split] != ' ') ++split;
    for (unsigned i = 0; i < view.width; ++i) {
        unsigned color = foreground;
        if (syntax) color = heading ? RUM_COLOR_LIGHT_CYAN : i < split ? RUM_COLOR_WHITE : RUM_COLOR_LIGHT_GREY;
        cells[i] = (struct rum_terminal_cell){ (uint8_t)(i < n ? text[i] : ' '), (uint8_t)color, (uint8_t)background, 0 };
    }
    return rum_terminal_cells(0, y, cells, view.width);
}
static rum_result_t draw(const char *title)
{
    rum_result_t result = row(0, title, RUM_COLOR_WHITE, RUM_COLOR_BLUE, 0);
    for (unsigned y = 0; result >= 0 && y < view.height; ++y) {
        unsigned index = view.top + y;
        if (index < view.total) {
            char text[RUM_TERM_MAX_CELLS + 1];
            unsigned n = view.width;
            for (unsigned i = 0; i < n; ++i) text[i] = view.lines[index % HISTORY][i];
            while (n && text[n - 1] == ' ') --n;
            text[n] = 0;
            result = row(y + 1, text, RUM_COLOR_LIGHT_GREY, RUM_COLOR_BLACK, 1);
        } else result = row(y + 1, !view.total && !y ? "(empty file)" : "", RUM_COLOR_DARK_GREY, RUM_COLOR_BLACK, 0);
    }
    if (result >= 0) result = row(view.height + 1,
        view.eof && view.top + view.height >= view.total ?
        "End | Space/Q close | B/PgUp back" : "Space/PgDn next | Enter line | B/PgUp back | Q close",
        RUM_COLOR_YELLOW, RUM_COLOR_BLUE, 0);
    return result;
}
rum_result_t pager_view(const char *title, pager_next next, void *source)
{
    struct rum_terminal_capabilities caps;
    if (rum_isatty(RUM_STDIN) != 1 || rum_isatty(RUM_STDOUT) != 1 || rum_terminal_query(&caps) < 0 ||
        !caps.width || caps.width > RUM_TERM_MAX_CELLS || caps.height < 4 || caps.height > 64)
        return PAGER_FALLBACK;
    rum_result_t result = rum_terminal_begin();
    if (result < 0) return PAGER_FALLBACK;
    view.width = caps.width; view.height = caps.height - 2;
    view.total = view.first = view.top = view.column = 0;
    view.eof = view.wrapped = 0; view.error = 0;
    for (;;) {
        ensure(view.top + view.height + 1, next, source);
        if (view.error < 0) { result = view.error; break; }
        result = draw(title);
        if (result < 0) break;
        struct rum_terminal_event event;
        result = rum_terminal_read(&event, 1, RUM_TERM_MAX_WAIT_MS, 0);
        if (!result) continue;
        if (result < 0) break;
        unsigned c = event.value;
        if (c == 'q' || c == 'Q' || c == 27) { result = 0; break; }
        if (c == 'b' || c == 'B' || c == RUM_KEY_PAGEUP || c == RUM_KEY_UP) {
            unsigned amount = c == RUM_KEY_UP ? 1 : view.height;
            view.top = view.top - view.first > amount ? view.top - amount : view.first;
        } else if (c == ' ' || c == '\n' || c == '\r' || c == RUM_KEY_PAGEDOWN || c == RUM_KEY_DOWN) {
            if (view.eof && view.top + view.height >= view.total) { result = 0; break; }
            view.top += c == '\n' || c == '\r' || c == RUM_KEY_DOWN ? 1 : view.height;
        }
    }
    rum_result_t closed = rum_terminal_end();
    return result < 0 ? result : closed < 0 ? closed : 0;
}
