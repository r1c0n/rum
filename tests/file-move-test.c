#include <assert.h>
#include <stdio.h>
#include "../system/lib/files.h"
int file_move_main(int, char **);
static unsigned copies, removals, renames;
static rum_result_t copy_error, remove_error;
static int force;
void tool_print(const char *text) { (void)text; }
void tool_error(const char *name, rum_result_t error) { (void)name; (void)error; }
void file_copy_error(const char *name, rum_result_t error, const struct file_copy_status *status) { (void)name; (void)error; (void)status; }
rum_result_t file_copy(const char *from, const char *to, int replace, struct file_copy_status *status)
{
    (void)from; (void)to; ++copies; assert(replace == force); *status = (struct file_copy_status){0}; return copy_error;
}
rum_result_t rum_remove(const char *path) { (void)path; assert(copies && !copy_error); ++removals; return remove_error; }
rum_result_t rum_rename(const char *from, const char *to, uint32_t flags)
{
    (void)from; (void)to; (void)flags; ++renames; return -RUM_EXDEV;
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *normal[] = {"mv", "source", "destination"};
    assert(file_move_main(3, normal) == 1 && renames == 1 && !copies && !removals);
    char *explicit[] = {"mv", "--copy", "source", "destination"};
    copy_error = -RUM_ENOSPC;
    assert(file_move_main(4, explicit) == 1 && copies == 1 && !removals);
    copy_error = -RUM_EIO; /* Includes flush and close failures from file_copy. */
    assert(file_move_main(4, explicit) == 1 && copies == 2 && !removals);
    copy_error = 0; remove_error = -RUM_EBUSY;
    assert(file_move_main(4, explicit) == 1 && copies == 3 && removals == 1);
    remove_error = 0; force = 1;
    char *forced[] = {"mv", "-f", "--copy", "source", "destination"};
    assert(!file_move_main(5, forced) && copies == 4 && removals == 2 && renames == 1);
    puts("PASS: cross-mount moves require explicit copy; failed copy/flush/close never removes the source"); return 0;
}
