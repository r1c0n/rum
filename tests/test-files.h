#ifndef RUM_TEST_FILES_H
#define RUM_TEST_FILES_H
#include <rum/abi/elf.h>
#include <rum/fs.h>
#include <rum/path.h>
#include <rum/elf.h>
#define TEST_FILE_LIMIT 160u
struct test_file_statistics { size_t files, bytes; };
typedef bool (*test_file_visitor)(const char *, size_t, void *);
bool test_files_initialize(void);
bool test_files_mount(void);
bool test_file_put(const char *, const void *, size_t);
bool test_file_read(const char *, const unsigned char **, size_t *);
bool test_file_remove(const char *);
struct test_file_statistics test_file_stats(void);
void test_file_list(test_file_visitor, void *);
static inline bool test_file_load(const char *name, const struct rum_arguments *arguments,
                                  struct task_process *process)
{
    const unsigned char *data; size_t bytes;
    return test_file_read(name, &data, &bytes) && elf_load_process(data, bytes, arguments, process);
}
#endif
