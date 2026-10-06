#ifndef RUM_TEST_FS_BACKEND_H
#define RUM_TEST_FS_BACKEND_H
#include <rum/fs.h>

/* A directory tree for namespace tests, not a persistent filesystem format. */
void fs_test_backend_initialize(void);
const struct fs_backend *fs_test_backend(void);
unsigned fs_test_backend_calls(void);
unsigned fs_test_backend_pins(void);
extern enum fs_error fs_test_backend_failure;
extern bool fs_test_backend_bad_cursor, fs_test_backend_reenter;
extern enum fs_error fs_test_backend_reentry_result;
extern size_t fs_test_backend_short;
extern enum fs_error fs_test_backend_partial_error;
#endif
