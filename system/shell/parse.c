#include "shell.h"

enum token_kind { DONE, WORD, INPUT, OUTPUT, APPEND, PIPE, BACKGROUND };
struct token { enum token_kind kind; char *word; unsigned start, end; };
static int space(char c) { return c == ' ' || c == '\t'; }
static int operator(char c) { return c == '<' || c == '>' || c == '|' || c == '&'; }
static int equal(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
/* Strict parsing and unfinished completion words share this lexer. */
static rum_result_t next(const char *line, unsigned limit, unsigned *position,
                          char *text, unsigned *used, int partial, struct token *token)
{
    unsigned p = *position;
    while (p < limit && space(line[p])) ++p;
    *token = (struct token){ .start = p, .end = p };
    if (p == limit) { *position = p; return 0; }
    char c = line[p];
    if (operator(c)) {
        token->kind = c == '<' ? INPUT : c == '>' ? OUTPUT : c == '|' ? PIPE : BACKGROUND;
        ++p;
        if (c == '>' && p < limit && line[p] == '>') { token->kind = APPEND; ++p; }
    } else {
        token->kind = WORD; token->word = text + *used;
        char quote = 0;
        while (p < limit) {
            c = line[p];
            if (!quote && (space(c) || operator(c))) break;
            if ((unsigned char)c < 32 || (unsigned char)c > 126 || (!quote && c == ';')) return -RUM_EINVAL;
            ++p;
            if (c == '\\' && quote != '\'') {
                if (p == limit) { if (partial) break; return -RUM_EINVAL; }
                c = line[p++];
                if ((unsigned char)c < 32 || (unsigned char)c > 126) return -RUM_EINVAL;
            } else if (c == '\'' || c == '"') {
                if (!quote) { quote = c; continue; }
                if (quote == c) { quote = 0; continue; }
            }
            if (*used >= RUM_COMMAND_TEXT_CAPACITY - 1) return -RUM_E2BIG;
            text[(*used)++] = c;
        }
        if (quote && !partial) return -RUM_EINVAL;
        if (*used == RUM_COMMAND_TEXT_CAPACITY) return -RUM_E2BIG;
        text[(*used)++] = 0;
    }
    token->end = p; *position = p; return 0;
}
rum_result_t shell_parse_pipeline(const char *line, struct shell_pipeline *plan)
{
    if (!line || !plan) return -RUM_EINVAL;
    *plan = (struct shell_pipeline){0};
    unsigned length = 0;
    while (length < RUM_COMMAND_TEXT_CAPACITY && line[length]) ++length;
    if (length == RUM_COMMAND_TEXT_CAPACITY) return -RUM_E2BIG;
    unsigned position = 0, used = 0, redirects = 0;
    plan->count = 1;
    for (;;) {
        struct token token;
        rum_result_t result = next(line, length, &position, plan->text, &used, 0, &token);
        if (result) return result;
        if (token.kind == DONE) break;
        struct shell_stage *stage = &plan->stages[plan->count - 1];
        if (token.kind == WORD) {
            if (stage->argc == RUM_ABI_ARGUMENT_LIMIT) return -RUM_E2BIG;
            if (!stage->argc && !*token.word) return -RUM_EINVAL;
            stage->argv[stage->argc++] = token.word;
        } else if (token.kind == PIPE) {
            if (!stage->argc) return -RUM_EINVAL;
            if (plan->count == SHELL_PIPELINE_LIMIT) return -RUM_E2BIG;
            ++plan->count;
        } else if (token.kind == BACKGROUND) {
            if (!stage->argc) return -RUM_EINVAL;
            result = next(line, length, &position, plan->text, &used, 0, &token);
            if (result || token.kind != DONE) return result ? result : -RUM_EINVAL;
            plan->background = 1; break;
        } else {
            if (redirects++ == SHELL_REDIRECTION_LIMIT) return -RUM_E2BIG;
            enum shell_redirect_kind kind = token.kind == INPUT ? SHELL_INPUT : token.kind == OUTPUT ? SHELL_OUTPUT : SHELL_APPEND;
            result = next(line, length, &position, plan->text, &used, 0, &token);
            if (result || token.kind != WORD) return result ? result : -RUM_EINVAL;
            stage->redirects[stage->redirect_count++] = (struct shell_redirect){kind, token.word};
        }
    }
    return plan->stages[plan->count - 1].argc ? 0 : -RUM_EINVAL;
}
rum_result_t shell_completion_context(const char *line, unsigned cursor, struct shell_completion_context *context)
{
    if (cursor >= RUM_COMMAND_TEXT_CAPACITY) return -RUM_EINVAL;
    unsigned length = 0; while (length < RUM_COMMAND_TEXT_CAPACITY && line[length]) ++length;
    if (length >= RUM_COMMAND_TEXT_CAPACITY || cursor > length) return -RUM_EINVAL;
    if (cursor < length && !space(line[cursor]) && !operator(line[cursor])) return -RUM_EINVAL;
    *context = (struct shell_completion_context){ .start = cursor, .end = cursor, .command = 1 };
    unsigned position = 0, used = 0; char text[RUM_COMMAND_TEXT_CAPACITY];
    int command = 1, path = 0, launch_word = 0;
    for (;;) {
        struct token token;
        rum_result_t result = next(line, cursor, &position, text, &used, 1, &token);
        if (result) return result;
        if (token.kind == DONE) { context->command = command && !path; return 0; }
        if (token.kind == BACKGROUND) return -RUM_EINVAL;
        if (token.kind == PIPE) { command = 1; path = launch_word = 0; }
        else if (token.kind != WORD) path = 1;
        else {
            int role = command && !path;
            if (token.end == cursor) {
                context->start = token.start; context->command = role;
                unsigned i = 0; do { context->prefix[i] = token.word[i]; } while (token.word[i++]);
                return 0;
            }
            if (path) path = 0;
            else if (command) {
                if (!launch_word && (equal(token.word, "start") || equal(token.word, "run"))) launch_word = 1;
                else command = 0;
            }
        }
    }
}
