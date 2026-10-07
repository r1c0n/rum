#ifndef RUM_ABI_ERROR_H
#define RUM_ABI_ERROR_H

/* Syscalls return a nonnegative result or the negative of one of these values.
   These are rum values; they do not promise host errno compatibility. */
#define RUM_EINVAL  1
#define RUM_EFAULT  2
#define RUM_ENOMEM  3
#define RUM_ENOSYS  4
#define RUM_EBADF   5
#define RUM_EIO     6
#define RUM_ENOENT  7
#define RUM_E2BIG   8
#define RUM_EBUSY   9
#define RUM_EINTR  10
#define RUM_EACCES 11
#define RUM_EROFS 12
#define RUM_ENOSPC 13
#define RUM_EMFILE 14
#define RUM_ENOTDIR 15
#define RUM_EISDIR 16
#define RUM_EEXIST 17
#define RUM_ENOTEMPTY 18
#define RUM_ENAMETOOLONG 19
#define RUM_EOVERFLOW 20
#define RUM_ERANGE 21
#define RUM_ETIMEDOUT 22
#define RUM_ENODEV 23
#define RUM_ENOEXEC 24

#endif
