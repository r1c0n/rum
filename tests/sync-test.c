#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rum/tools.h>
int sync_main(int, char **);
static rum_result_t open_result, flush_result, close_result, reported;
static unsigned opened, flushed, closed;
void tool_print(const char *text) { assert(!strcmp(text, "Usage: sync\n")); }
void tool_error(const char *name, rum_result_t result) { assert(!strcmp(name, "sync")); reported = result; }
rum_result_t rum_open(const char *path, uint32_t access, uint32_t flags)
{
    assert(!strcmp(path, "/") && access == RUM_OPEN_READ && flags == RUM_OPEN_DIRECTORY);
    ++opened; return open_result;
}
rum_result_t rum_flush(rum_handle_t handle) { assert(handle == 3); ++flushed; return flush_result; }
rum_result_t rum_close(rum_handle_t handle) { assert(handle == 3); ++closed; return close_result; }
static void reset(void) { open_result = 3; flush_result = close_result = reported = 0; opened = flushed = closed = 0; }
int main(int argc, char **argv)
{
    (void)argc; (void)argv; char *words[] = {"sync", "extra"};
    reset(); assert(!sync_main(1, words) && opened == 1 && flushed == 1 && closed == 1 && !reported);
    reset(); assert(sync_main(2, words) == 1 && !opened && !flushed && !closed);
    reset(); open_result = -RUM_ENODEV;
    assert(sync_main(1, words) == 1 && opened == 1 && !flushed && !closed && reported == -RUM_ENODEV);
    reset(); flush_result = -RUM_EIO; close_result = -RUM_EBUSY;
    assert(sync_main(1, words) == 1 && flushed == 1 && closed == 1 && reported == -RUM_EIO);
    reset(); close_result = -RUM_EBUSY;
    assert(sync_main(1, words) == 1 && flushed == 1 && closed == 1 && reported == -RUM_EBUSY);
    puts("PASS: sync root-volume flush, argument validation, error reporting and close after failure"); return 0;
}
