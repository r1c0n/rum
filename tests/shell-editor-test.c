#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../system/shell/shell.h"

static void type(struct shell_editor *e, const char *text)
{
    while (*text) assert(shell_editor_key(e, (unsigned char)*text++) == SHELL_EDIT_CHANGED);
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    struct shell_editor e = {0};
    type(&e, "echo ac"); shell_editor_key(&e, RUM_KEY_LEFT); type(&e, "b");
    assert(!strcmp(e.line, "echo abc") && e.cursor == 7);
    shell_editor_key(&e, RUM_KEY_HOME); shell_editor_key(&e, RUM_KEY_DELETE);
    assert(!strcmp(e.line, "cho abc") && !e.cursor);
    assert(shell_editor_key(&e, '\b') == SHELL_EDIT_NONE);
    shell_editor_key(&e, RUM_KEY_END); shell_editor_key(&e, '\b');
    assert(!strcmp(e.line, "cho ab") && e.cursor == e.length);
    assert(shell_editor_key(&e, RUM_KEY_DELETE) == SHELL_EDIT_NONE);
    assert(shell_editor_key(&e, '\n') == SHELL_EDIT_ACCEPT);
    shell_editor_commit(&e); assert(e.history_count == 1 && !e.length);
    type(&e, "cho ab"); shell_editor_commit(&e); assert(e.history_count == 1);
    type(&e, "  "); shell_editor_commit(&e); assert(e.history_count == 1);
    for (unsigned i = 0; i < 20; ++i) {
        char text[32]; snprintf(text, sizeof text, "echo %u", i); type(&e, text); shell_editor_commit(&e);
    }
    assert(e.history_count == SHELL_HISTORY_LIMIT);
    type(&e, "draft"); shell_editor_key(&e, RUM_KEY_LEFT);
    for (unsigned i = 0; i < 16; ++i) assert(shell_editor_key(&e, RUM_KEY_UP) == SHELL_EDIT_CHANGED);
    assert(!strcmp(e.line, "echo 4") && shell_editor_key(&e, RUM_KEY_UP) == SHELL_EDIT_NONE);
    for (unsigned i = 0; i < 16; ++i) shell_editor_key(&e, RUM_KEY_DOWN);
    assert(!strcmp(e.line, "draft") && e.cursor == 4);
    shell_editor_key(&e, RUM_KEY_UP); type(&e, "!");
    assert(!strcmp(e.line, "echo 19!") && !e.browse);
    shell_editor_key(&e, RUM_KEY_UP); assert(!strcmp(e.line, "echo 19"));
    assert(shell_editor_key(&e, 3) == SHELL_EDIT_CANCEL && !e.length && !e.browse && e.history_count == 16);
    type(&e, "echo ac"); shell_editor_key(&e, RUM_KEY_LEFT);
    assert(shell_editor_key(&e, 12) == SHELL_EDIT_CLEAR && !strcmp(e.line, "echo ac") && e.cursor == 6);
    shell_editor_key(&e, RUM_KEY_UP);
    assert(shell_editor_key(&e, 12) == SHELL_EDIT_CLEAR && e.browse == 1 && !strcmp(e.line, "echo 19"));
    assert(shell_editor_key(&e, 21) == SHELL_EDIT_CHANGED && !e.length && !e.cursor && !e.browse && e.history_count == 16);
    assert(shell_editor_key(&e, RUM_KEY_DOWN) == SHELL_EDIT_NONE);
    for (unsigned i = 0; i < RUM_COMMAND_TEXT_CAPACITY - 1; ++i) type(&e, "a");
    assert(shell_editor_key(&e, 'b') == SHELL_EDIT_NONE && e.length == 255 && !e.line[255]);
    assert(!shell_editor_replace(&e, 0, 0, "b") && !shell_editor_replace(&e, 2, 1, ""));
    assert(shell_editor_replace(&e, 0, e.length, "echo revised") && !strcmp(e.line, "echo revised"));
    assert(shell_editor_key(&e, '\t') == SHELL_EDIT_COMPLETE && e.cursor == e.length);
    puts("PASS: cursor insertion/delete, history eviction and draft restoration, cancellation and atomic line bounds");
    return 0;
}
