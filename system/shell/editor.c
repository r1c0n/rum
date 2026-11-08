#include "shell.h"
static unsigned length(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
static void copy(char *to, const char *from) { while ((*to++ = *from++)) {} }
static int equal(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
void shell_editor_reset(struct shell_editor *e) { e->line[0] = 0; e->length = e->cursor = e->browse = 0; }
void shell_editor_commit(struct shell_editor *e)
{
    unsigned i = 0; while (e->line[i] == ' ' || e->line[i] == '\t') ++i;
    if (e->line[i] && (!e->history_count || !equal(e->line,
        e->history[(e->history_first + e->history_count - 1) % SHELL_HISTORY_LIMIT]))) {
        unsigned slot;
        if (e->history_count < SHELL_HISTORY_LIMIT) slot = (e->history_first + e->history_count++) % SHELL_HISTORY_LIMIT;
        else { slot = e->history_first; e->history_first = (e->history_first + 1) % SHELL_HISTORY_LIMIT; }
        copy(e->history[slot], e->line);
    }
    shell_editor_reset(e);
}
int shell_editor_replace(struct shell_editor *e, unsigned start, unsigned end, const char *text)
{
    if (start > end || end > e->length) return 0;
    unsigned n = length(text), retained = e->length - (end - start);
    if (n > RUM_COMMAND_TEXT_CAPACITY - 1 - retained) return 0;
    char line[RUM_COMMAND_TEXT_CAPACITY]; unsigned out = 0;
    for (unsigned i = 0; i < start; ++i) line[out++] = e->line[i];
    for (unsigned i = 0; i < n; ++i) line[out++] = text[i];
    for (unsigned i = end; i < e->length; ++i) line[out++] = e->line[i];
    line[out] = 0; copy(e->line, line); e->length = out; e->cursor = start + n; e->browse = 0; return 1;
}
enum shell_edit_action shell_editor_key(struct shell_editor *e, uint32_t key)
{
    if (key == '\n' || key == '\r') return SHELL_EDIT_ACCEPT;
    if (key == 3) { shell_editor_reset(e); return SHELL_EDIT_CANCEL; }
    if (key == 12) return SHELL_EDIT_CLEAR;
    if (key == 21) { shell_editor_reset(e); return SHELL_EDIT_CHANGED; }
    if (key == '\t') return SHELL_EDIT_COMPLETE;
    if (key == RUM_KEY_LEFT) { if (!e->cursor) return SHELL_EDIT_NONE; --e->cursor; }
    else if (key == RUM_KEY_RIGHT) { if (e->cursor == e->length) return SHELL_EDIT_NONE; ++e->cursor; }
    else if (key == RUM_KEY_HOME) e->cursor = 0;
    else if (key == RUM_KEY_END) e->cursor = e->length;
    else if (key == RUM_KEY_UP || key == RUM_KEY_DOWN) {
        if (key == RUM_KEY_UP) {
            if (e->browse == e->history_count) return SHELL_EDIT_NONE;
            if (!e->browse) { copy(e->draft, e->line); e->draft_cursor = e->cursor; }
            ++e->browse;
        } else { if (!e->browse) return SHELL_EDIT_NONE; --e->browse; }
        copy(e->line, e->browse ? e->history[(e->history_first + e->history_count - e->browse) % SHELL_HISTORY_LIMIT] : e->draft);
        e->length = length(e->line); e->cursor = e->browse ? e->length : e->draft_cursor;
    } else if (key == '\b' || key == 127 || key == RUM_KEY_DELETE) {
        if (key != RUM_KEY_DELETE && !e->cursor) return SHELL_EDIT_NONE;
        unsigned at = key == RUM_KEY_DELETE ? e->cursor : e->cursor - 1;
        if (at == e->length) return SHELL_EDIT_NONE;
        (void)shell_editor_replace(e, at, at + 1, "");
    } else if (key >= 32 && key <= 126) {
        char text[] = {(char)key, 0};
        if (!shell_editor_replace(e, e->cursor, e->cursor, text)) return SHELL_EDIT_NONE;
    } else return SHELL_EDIT_NONE;
    return SHELL_EDIT_CHANGED;
}
