#ifndef RUM_C_ERRNO_H
#define RUM_C_ERRNO_H
#include <rum/abi/error.h>
/* Process-local for the current single-threaded runtime. */
extern int errno;
#define EINVAL RUM_EINVAL
#define ENOMEM RUM_ENOMEM
#define EIO RUM_EIO
#define EBADF RUM_EBADF
#define ENOENT RUM_ENOENT
#define EACCES RUM_EACCES
#define EROFS RUM_EROFS
#define ENOSPC RUM_ENOSPC
#define EMFILE RUM_EMFILE
#define EOVERFLOW RUM_EOVERFLOW
#define ERANGE RUM_ERANGE
#define ENOSYS RUM_ENOSYS
#define EINTR RUM_EINTR
#define ESPIPE RUM_ESPIPE
#endif
