#ifndef RUM_COMMAND_STREAM_H
#define RUM_COMMAND_STREAM_H
#include <rum/tools.h>
struct command_input {
    rum_handle_t handle;
    unsigned used, cursor;
    int own;
    unsigned char buffer[512];
};
struct text_decoder { unsigned cr; };
rum_result_t command_input_open(struct command_input *, const char *path);
rum_result_t command_input_next(struct command_input *, unsigned char *);
rum_result_t command_input_close(struct command_input *);
unsigned text_decode(struct text_decoder *, unsigned char byte, char output[8]);
unsigned text_finish(struct text_decoder *, char output[8]);
/* Copy at most lines LF-delimited lines; raw preserves every input byte.
   UINT32_MAX means no line limit. Console display escapes unsupported controls. */
rum_result_t command_copy(struct command_input *, uint32_t lines, int raw);
int command_equal(const char *, const char *);
rum_result_t command_count(const char *, uint32_t *);
#endif
