#ifndef RUM_FS_LIMITS_H
#define RUM_FS_LIMITS_H
#include <rum/abi/filesystem.h>
#include <rum/process_limits.h>

#define FS_PATH_CAPACITY RUM_ABI_PATH_CAPACITY
#define FS_NAME_CAPACITY RUM_ABI_COMPONENT_CAPACITY
#define FS_PATH_DEPTH RUM_ABI_PATH_DEPTH
#define FS_TOKEN_LIMIT (FS_PATH_CAPACITY / 2)
#define FS_OBJECT_LIMIT 128u
#define FS_REFERENCE_LIMIT (RUM_PROCESS_LIMIT * RUM_PROCESS_FILE_LIMIT + RUM_PROCESS_LIMIT + 2u)

_Static_assert(FS_PATH_CAPACITY <= UINT16_MAX && FS_NAME_CAPACITY > 1 &&
               FS_PATH_DEPTH > 0 && FS_REFERENCE_LIMIT >= FS_OBJECT_LIMIT,
               "bounded filesystem tables and parser offsets");
#endif
