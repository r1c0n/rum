#ifndef RUM_PATH_H
#define RUM_PATH_H
#include <rum/fs_types.h>
#include <rum/fs_limits.h>

struct fs_tokens {
    char text[FS_PATH_CAPACITY];
    uint16_t offsets[FS_TOKEN_LIMIT], count;
    bool absolute, directory;
};
struct fs_path {
    struct fs_tokens base, input;
    char canonical[FS_PATH_CAPACITY];
    enum fs_mount_id mount;
    uint16_t depth;
};

enum fs_error fs_name_check(enum fs_naming naming, const char *name);
/* Pure preflight: parses all bytes and checks naming, depth, length and mount
   traversal before backend I/O. The resolver must still walk the original
   tokens: missing/file components cannot disappear through lexical '..'.
   base is an absolute directory path. Inputs must not overlap result. */
enum fs_error fs_path_parse(const char *base, const char *input, struct fs_path *result);
#endif
