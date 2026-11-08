#ifndef RUM_SYSTEM_FILES_H
#define RUM_SYSTEM_FILES_H
#include <rum/tools.h>
struct file_copy_status { int output_opened, same_file; };
rum_result_t file_copy(const char *, const char *, int replace, struct file_copy_status *);
void file_copy_error(const char *command, rum_result_t error, const struct file_copy_status *);
#endif
