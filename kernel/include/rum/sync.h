#ifndef RUM_SYNC_H
#define RUM_SYNC_H
#include <rum/abi/thread.h>
#include <rum/object_handle.h>

#define SYNC_OBJECT_LIMIT 64u
/* Trusted table and kernel-resident packet. The service pins the target until
   the operation returns, including while blocked. Task context with IF set. */
rum_result_t sync_request(struct object_handles *table, struct rum_sync_request *request);
/* Trusted IRQ producer must already own a reference to this event or semaphore.
   count is one for events, positive for semaphores. Never drops the pin. */
rum_result_t sync_signal_pinned(struct kernel_object *object, uint32_t count);
/* Called by the exiting owner in destruction-safe task context. */
void sync_abandon_current(void);
#endif
