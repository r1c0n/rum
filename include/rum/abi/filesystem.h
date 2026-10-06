#ifndef RUM_ABI_FILESYSTEM_H
#define RUM_ABI_FILESYSTEM_H

/* Capacities include NUL. Depth counts names below a mount root, including
   the final filename. These limits do not introduce filesystem syscalls. */
#define RUM_ABI_PATH_CAPACITY      256
#define RUM_ABI_COMPONENT_CAPACITY 64
#define RUM_ABI_PATH_DEPTH         16

#endif
