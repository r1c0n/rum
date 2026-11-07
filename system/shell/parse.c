#include "shell.h"

static int space(char c) { return c == ' ' || c == '\t'; }
static int operator(char c) { return c == '<' || c == '>' || c == '|'; }
static rum_result_t word(const char **input, struct shell_pipeline *plan, unsigned *used, char **output)
{
    const char *p = *input;
    while (space(*p)) ++p;
    if (!*p || operator(*p)) return -RUM_EINVAL;
    *output = plan->text + *used;
    while (*p && !space(*p) && !operator(*p)) {
        unsigned char c = (unsigned char)*p++;
        if (c < 32 || c > 126 || c == '\'' || c == '"' || c == '&' || c == ';') return -RUM_EINVAL;
        if (*used >= sizeof plan->text - 1) return -RUM_E2BIG;
        plan->text[(*used)++] = (char)c;
    }
    plan->text[(*used)++] = 0; *input = p; return 0;
}
rum_result_t shell_parse_pipeline(const char *line, struct shell_pipeline *plan)
{
    if (!line || !plan) return -RUM_EINVAL;
    *plan = (struct shell_pipeline){0};
    unsigned length = 0;
    while (length < RUM_COMMAND_TEXT_CAPACITY && line[length]) ++length;
    if (length == RUM_COMMAND_TEXT_CAPACITY) return -RUM_E2BIG;
    unsigned used = 0, redirects = 0;
    plan->count = 1;
    const char *p = line;
    while (1) {
        while (space(*p)) ++p;
        if (!*p) break;
        struct shell_stage *stage = &plan->stages[plan->count - 1];
        if (*p == '|') {
            if (!stage->argc) return -RUM_EINVAL;
            if (plan->count == SHELL_PIPELINE_LIMIT) return -RUM_E2BIG;
            ++plan->count; ++p;
        } else if (*p == '<' || *p == '>') {
            if (redirects++ == SHELL_REDIRECTION_LIMIT) return -RUM_E2BIG;
            enum shell_redirect_kind kind = *p++ == '<' ? SHELL_INPUT : SHELL_OUTPUT;
            if (kind == SHELL_OUTPUT && *p == '>') { kind = SHELL_APPEND; ++p; }
            char *path;
            rum_result_t result = word(&p, plan, &used, &path);
            if (result) return result;
            stage->redirects[stage->redirect_count++] = (struct shell_redirect){kind, path};
        } else {
            if (stage->argc == RUM_ABI_ARGUMENT_LIMIT) return -RUM_E2BIG;
            rum_result_t result = word(&p, plan, &used, &stage->argv[stage->argc]);
            if (result) return result;
            ++stage->argc;
        }
    }
    return plan->stages[plan->count - 1].argc ? 0 : -RUM_EINVAL;
}
