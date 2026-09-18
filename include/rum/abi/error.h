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

#endif
