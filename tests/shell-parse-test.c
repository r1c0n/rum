#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../system/shell/shell.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    struct shell_pipeline plan;
    assert(!shell_parse_pipeline("cat<IN.BIN|cat|cat>>OUT.BIN", &plan));
    assert(plan.count == 3 && plan.stages[0].argc == 1 && plan.stages[1].argc == 1);
    assert(plan.stages[0].redirect_count == 1 && !strcmp(plan.stages[0].redirects[0].path, "IN.BIN"));
    assert(plan.stages[2].redirects[0].kind == SHELL_APPEND && !strcmp(plan.stages[2].redirects[0].path, "OUT.BIN"));
    assert(!shell_parse_pipeline(" >first.txt echo hello >second.txt", &plan));
    assert(plan.stages[0].argc == 2 && plan.stages[0].redirect_count == 2 &&
           !strcmp(plan.stages[0].argv[1], "hello"));
    const char *invalid[] = {"", " ", "| cat", "cat |", "cat || cat", "cat >", "cat >>>out",
        "cat <<in", ">out", "cat <| cat", "echo 'word", "echo a& | cat", "echo a; | cat", "echo trailing\\", "\"\""};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
        assert(shell_parse_pipeline(invalid[i], &plan) == -RUM_EINVAL);
    assert(!shell_parse_pipeline("cat|cat|cat|cat", &plan) && plan.count == SHELL_PIPELINE_LIMIT);
    assert(!shell_parse_pipeline("cat<IN.BIN|cat>OUT.BIN& ", &plan) && plan.background && plan.count == 2);
    assert(!shell_parse_pipeline("spin&", &plan) && plan.background && plan.count == 1);
    assert(!shell_parse_pipeline("echo hi", &plan) && !plan.background);
    assert(!shell_parse_pipeline("echo '' \"\" a\" b\"' c' \\| \"x>y&z\"", &plan));
    assert(plan.stages[0].argc == 6 && !*plan.stages[0].argv[1] && !*plan.stages[0].argv[2]);
    assert(!strcmp(plan.stages[0].argv[3], "a b c") && !strcmp(plan.stages[0].argv[4], "|") &&
           !strcmp(plan.stages[0].argv[5], "x>y&z"));
    assert(!shell_parse_pipeline("cat \"/DOCS/NOTE.TXT\" >'OUT.TXT'", &plan));
    assert(!strcmp(plan.stages[0].argv[1], "/DOCS/NOTE.TXT"));
    struct shell_completion_context context;
    assert(!shell_completion_context("cat \"DO", 7, &context) && !context.command && !strcmp(context.prefix, "DO"));
    assert(!shell_completion_context("run he", 6, &context) && context.command);
    assert(!shell_completion_context("echo > DO", 9, &context) && !context.command && context.start == 7);
    assert(shell_parse_pipeline("spin&echo no", &plan) == -RUM_EINVAL);
    assert(shell_parse_pipeline("spin&&", &plan) == -RUM_EINVAL);
    assert(shell_parse_pipeline("&", &plan) == -RUM_EINVAL);
    assert(shell_parse_pipeline("cat|cat|cat|cat|cat", &plan) == -RUM_E2BIG);
    assert(!shell_parse_pipeline("echo >a >b >c >d >e >f >g >h", &plan));
    assert(shell_parse_pipeline("echo >a >b >c >d >e >f >g >h >i", &plan) == -RUM_E2BIG);
    char text[RUM_COMMAND_TEXT_CAPACITY + 1];
    memset(text, 'x', sizeof text); text[sizeof text - 1] = 0;
    assert(shell_parse_pipeline(text, &plan) == -RUM_E2BIG);
    text[sizeof text - 2] = 0; assert(!shell_parse_pipeline(text, &plan));
    for (unsigned i = 0; i < 33; ++i) { text[i * 2] = 'a'; text[i * 2 + 1] = ' '; }
    text[64] = 0; assert(!shell_parse_pipeline(text, &plan) && plan.stages[0].argc == 32);
    text[64] = 'a'; text[65] = 0; assert(shell_parse_pipeline(text, &plan) == -RUM_E2BIG);
    puts("PASS: bounded pipeline grammar, adjacent operators, redirection order and rejected syntax");
    return 0;
}
