#ifndef RUM_PAGER_H
#define RUM_PAGER_H
#include "stream.h"
#define PAGER_FALLBACK 1 /* No interactive console; source has not been read. */
typedef rum_result_t (*pager_next)(void *, char *);
rum_result_t pager_view(const char *title, pager_next, void *source);
struct pager_text { const char *text; unsigned offset; };
rum_result_t pager_text_next(void *, char *);
struct pager_file {
    struct command_input *input;
    struct text_decoder decoder;
    char pending[8]; unsigned used, cursor;
    int finished;
};
rum_result_t pager_file_next(void *, char *);
#endif
