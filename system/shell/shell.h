#ifndef RUM_SHELL_PRIVATE_H
#define RUM_SHELL_PRIVATE_H
#include <rum/tools.h>

#define SHELL_PIPELINE_LIMIT 4u
#define SHELL_REDIRECTION_LIMIT 8u
_Static_assert(SHELL_PIPELINE_LIMIT == RUM_JOB_STAGE_LIMIT, "shell and kernel stage limits agree");
enum shell_redirect_kind { SHELL_INPUT, SHELL_OUTPUT, SHELL_APPEND };
struct shell_redirect { enum shell_redirect_kind kind; char *path; };
struct shell_stage {
    unsigned argc, redirect_count;
    char *argv[RUM_ABI_ARGUMENT_LIMIT];
    struct shell_redirect redirects[SHELL_REDIRECTION_LIMIT];
};
struct shell_pipeline {
    unsigned count, background;
    char text[RUM_COMMAND_TEXT_CAPACITY];
    struct shell_stage stages[SHELL_PIPELINE_LIMIT];
};
struct shell_completion_context { unsigned start, end; int command; char prefix[RUM_COMMAND_TEXT_CAPACITY]; };
rum_result_t shell_completion_context(const char *, unsigned cursor, struct shell_completion_context *);
/* No mutations or syscalls until the complete bounded grammar is accepted. */
rum_result_t shell_parse_pipeline(const char *, struct shell_pipeline *);
void shell_run_pipeline(struct shell_pipeline *);
void shell_job_notices(int editing);
#endif
