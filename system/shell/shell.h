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
#define SHELL_HISTORY_LIMIT 16u
#define SHELL_COMPLETION_LIMIT 16u
#define SHELL_COMPLETION_SCAN_LIMIT 128u
struct shell_editor {
    char line[RUM_COMMAND_TEXT_CAPACITY], draft[RUM_COMMAND_TEXT_CAPACITY];
    char history[SHELL_HISTORY_LIMIT][RUM_COMMAND_TEXT_CAPACITY];
    unsigned length, cursor, history_first, history_count, browse, draft_cursor;
};
enum shell_edit_action { SHELL_EDIT_NONE, SHELL_EDIT_CHANGED, SHELL_EDIT_ACCEPT, SHELL_EDIT_CANCEL, SHELL_EDIT_COMPLETE };
struct shell_matches {
    char values[SHELL_COMPLETION_LIMIT][RUM_COMMAND_TEXT_CAPACITY];
    unsigned count, scanned, truncated, common;
};
enum shell_edit_action shell_editor_key(struct shell_editor *, uint32_t key);
void shell_editor_commit(struct shell_editor *);
void shell_editor_reset(struct shell_editor *);
int shell_editor_replace(struct shell_editor *, unsigned start, unsigned end, const char *);
rum_result_t shell_complete(const struct shell_completion_context *, struct shell_matches *);
/* No mutations or syscalls until the complete bounded grammar is accepted. */
rum_result_t shell_parse_pipeline(const char *, struct shell_pipeline *);
void shell_run_pipeline(struct shell_pipeline *);
void shell_job_notices(int editing);
#endif
