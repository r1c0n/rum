#ifndef RUM_ABI_JOB_H
#define RUM_ABI_JOB_H
#include <rum/abi/process.h>
#define RUM_JOB_ABI_VERSION 1u
#define RUM_JOB_LIMIT 8u
#define RUM_JOB_STAGE_LIMIT 4u
#define RUM_JOB_NAME_CAPACITY 64u
#define RUM_JOB_BUILDING 1u
#define RUM_JOB_RUNNING 2u
#define RUM_JOB_DONE 3u
#define RUM_JOB_FOREGROUND 1u
#define RUM_JOB_NOTIFIED 2u
#define RUM_JOB_CANCELLING 4u
#define RUM_JOB_WAIT_NOHANG 1u
#define RUM_JOB_CREATE 1u
#define RUM_JOB_COMMIT 2u
#define RUM_JOB_CANCEL 3u
#define RUM_JOB_LIST 4u
#define RUM_JOB_WAIT 5u
#define RUM_JOB_FOCUS 6u
#define RUM_JOB_RELEASE 7u
#define RUM_JOB_ACK 8u
#ifndef __ASSEMBLER__
struct rum_job_entry {
    uint32_t job_id, state, flags, stage_count;
    struct rum_process_result result, stages[RUM_JOB_STAGE_LIMIT];
    char name[RUM_JOB_NAME_CAPACITY];
};
/* All unused words zero. LIST/WAIT use writable nonoverlapping entries and
   packet storage; CREATE copies a nonempty bounded printable name first.
   count/total are output only. Job IDs are positive and session-scoped.
   WAIT copies results without consuming them. */
struct rum_job_request {
    uint32_t version, size, operation, job_id, flags;
    rum_address_t entries;
    uint32_t capacity, count, total;
    rum_address_t name;
    uint32_t reserved[2];
};
_Static_assert(sizeof(struct rum_job_entry) == 220 && sizeof(struct rum_job_request) == 48,
               "job ABI layout");
#endif
#endif
