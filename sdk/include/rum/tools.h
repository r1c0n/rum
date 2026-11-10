#ifndef RUM_TOOLS_H
#define RUM_TOOLS_H
#include <rum/user.h>

unsigned tool_length(const char *);
void tool_output(const void *, unsigned bytes);
void tool_print(const char *);
/* Colored console line; redirected streams receive only text and a newline. */
void tool_line_color(const char *, uint32_t foreground);
void tool_number(int32_t);
void tool_unsigned(uint64_t);
void tool_error(const char *, rum_result_t);
char *tool_word(char **);
char *tool_remainder(char *);
rum_result_t tool_arguments(struct rum_arguments *, int argc, char **argv);
rum_result_t tool_program_path(const char *, char path[RUM_ABI_PATH_CAPACITY]);
/* Join parsed arguments with one separator; preserve bytes within each arg. */
rum_result_t tool_text(char text[RUM_COMMAND_TEXT_CAPACITY], int argc, char **argv);
rum_result_t tool_join(char text[RUM_COMMAND_TEXT_CAPACITY], int first, int argc, char **argv);
void tool_child_status(const struct rum_process_result *, int always);
rum_result_t tool_job_id(const char *, uint32_t *);
int main(int argc, char **argv);
#endif
