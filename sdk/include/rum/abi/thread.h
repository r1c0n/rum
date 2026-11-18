#ifndef RUM_ABI_THREAD_H
#define RUM_ABI_THREAD_H

#include <rum/abi/object.h>

/* 0.9 wire design. No thread or synchronization syscall is dispatched yet.
   See docs/threading.md for validation, rights and lifetime rules. */
#define RUM_THREAD_ABI_VERSION 1u
#define RUM_OBJECT_THREAD      8u
#define RUM_OBJECT_EVENT       9u
#define RUM_OBJECT_MUTEX      10u
#define RUM_OBJECT_SEMAPHORE  11u
#define RUM_RIGHT_SIGNAL      0x200u
#define RUM_RIGHT_THREAD      (RUM_RIGHT_COMMON | RUM_RIGHT_WAIT)
#define RUM_RIGHT_SYNC        (RUM_RIGHT_COMMON | RUM_RIGHT_WAIT | RUM_RIGHT_SIGNAL)

#define RUM_THREAD_CREATE     1u
#define RUM_THREAD_EXIT       2u
#define RUM_THREAD_JOIN       3u
#define RUM_THREAD_SELF       4u
#define RUM_THREAD_QUERY      5u

#define RUM_SYNC_CREATE       1u
#define RUM_SYNC_WAIT         2u
#define RUM_SYNC_SIGNAL       3u
#define RUM_SYNC_RESET        4u
#define RUM_SYNC_QUERY        5u
#define RUM_EVENT_MANUAL      1u
#define RUM_WAIT_FOREVER      0xffffffffu
#define RUM_WAIT_ABANDONED    1 /* Positive success: caller owns the mutex. */

#ifndef __ASSEMBLER__
typedef uint32_t rum_tid_t;

struct rum_thread_request {
    uint32_t version, size, operation;
    rum_object_handle_t handle;
    rum_address_t start, argument;
    uint32_t timeout_ms;
    rum_result_t status;
    rum_tid_t tid;
    rum_object_handle_t result_handle;
    uint32_t finished, reserved;
};

struct rum_sync_request {
    uint32_t version, size, operation, type;
    rum_object_handle_t handle;
    uint32_t flags, value, maximum, timeout_ms;
    rum_object_handle_t result_handle;
    uint32_t reserved[2];
};

/* Separate caller-local diagnostic packet. Counts are sampled together.
   A blocked caller counts as live; completed thread objects can outlive it. */
struct rum_thread_resources {
    uint32_t version, size, live_threads, blocked_threads;
    uint32_t thread_objects, events, mutexes, semaphores;
    uint32_t reserved[2];
};

_Static_assert(sizeof(struct rum_thread_request) == 48 &&
               offsetof(struct rum_thread_request, status) == 28 &&
               offsetof(struct rum_thread_request, result_handle) == 36, "thread packet layout");
_Static_assert(sizeof(struct rum_sync_request) == 48 &&
               offsetof(struct rum_sync_request, value) == 24 &&
               offsetof(struct rum_sync_request, result_handle) == 36, "sync packet layout");
_Static_assert(sizeof(struct rum_thread_resources) == 40 &&
               offsetof(struct rum_thread_resources, thread_objects) == 16, "thread counts layout");
#endif
#endif
