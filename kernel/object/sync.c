#include <rum/sync.h>
#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/task.h>
#include <rum/timer.h>

struct sync_object {
    struct kernel_object header;
    struct task_wait_queue waiters;
    uint32_t value, maximum;
    task_id owner;
    bool manual, abandoned;
};

static struct sync_object pool[SYNC_OBJECT_LIMIT];

static rum_result_t destroy(struct kernel_object *header)
{
    struct sync_object *object = (struct sync_object *)header;
    if (object->owner || object->waiters.head || object->waiters.tail) return -RUM_EBUSY;
    *object = (struct sync_object){0};
    return 0;
}

static const struct object_type event_type = { .kind = OBJECT_EVENT, .destroy = destroy };
static const struct object_type mutex_type = { .kind = OBJECT_MUTEX, .destroy = destroy };
static const struct object_type semaphore_type = { .kind = OBJECT_SEMAPHORE, .destroy = destroy };

static const struct object_type *sync_type(uint32_t kind)
{
    switch (kind) {
    case RUM_OBJECT_EVENT: return &event_type;
    case RUM_OBJECT_MUTEX: return &mutex_type;
    case RUM_OBJECT_SEMAPHORE: return &semaphore_type;
    default: return NULL;
    }
}

static rum_result_t create(struct object_handles *table, struct rum_sync_request *r,
                           const struct object_type *type)
{
    if (r->handle || r->timeout_ms) return -RUM_EINVAL;
    if (type == &semaphore_type) {
        if (r->flags || !r->maximum || r->value > r->maximum) return -RUM_EINVAL;
    } else if (type == &event_type) {
        if (r->flags & ~RUM_EVENT_MANUAL || r->value > 1 || r->maximum) return -RUM_EINVAL;
    } else if (r->flags || r->value || r->maximum) return -RUM_EINVAL;

    /* Only task context constructs/destroys objects, with no scheduling
       boundary between taking a free slot and installing the handle. */
    for (unsigned i = 0; i < SYNC_OBJECT_LIMIT; ++i) {
        struct sync_object *object = &pool[i];
        if (object->header.state != OBJECT_EMPTY) continue;
        rum_result_t result = object_initialize(&object->header, type);
        if (result) return result;
        object->manual = r->flags == RUM_EVENT_MANUAL;
        object->value = r->value;
        object->maximum = r->maximum;
        result = object_publish(&object->header, OBJECT_OWNER);
        if (!result) result = object_handle_install(table, &object->header, RUM_RIGHT_SYNC,
            RUM_RIGHT_SYNC, 0, &r->result_handle);
        if (result && object_release(&object->header, OBJECT_OWNER)) cpu_halt();
        return result;
    }
    return -RUM_ENOMEM;
}

/* Direct handoff reserves the resource for the selected waiter before it can
   resume. A timeout/cancel winner has already been removed from this FIFO. */
rum_result_t sync_signal_pinned(struct kernel_object *header, uint32_t count)
{
    if (!header || header->state != OBJECT_PUBLISHED || !count) return -RUM_EINVAL;
    struct sync_object *object = (struct sync_object *)header;
    uint32_t saved = cpu_interrupt_save();
    rum_result_t result = 0;
    if (header->type == &event_type && count == 1) {
        if (object->manual) {
            object->value = 1;
            while (task_queue_grant_locked(&object->waiters, false)) {}
        } else {
            object->value = !task_queue_grant_locked(&object->waiters, false);
        }
    } else if (header->type == &semaphore_type) {
        if (count > object->maximum - object->value) result = -RUM_EOVERFLOW;
        else {
            while (count && task_queue_grant_locked(&object->waiters, false)) --count;
            object->value += count;
        }
    } else result = -RUM_EINVAL;
    cpu_interrupt_restore(saved);
    return result;
}

static rum_result_t release_mutex(struct sync_object *object, bool abandoning)
{
    uint32_t saved = cpu_interrupt_save();
    if (object->owner != task_current_id()) {
        cpu_interrupt_restore(saved);
        return -RUM_EACCES;
    }
    object->owner = task_queue_grant_locked(&object->waiters, abandoning);
    bool handed_off = object->owner != 0;
    object->abandoned = abandoning && !handed_off;
    cpu_interrupt_restore(saved);
    /* The owner's pin moves with a handoff. A final drop can destroy the
       mutex when every handle was closed while the owner still held it. */
    if (!handed_off && object_release(&object->header, OBJECT_OWNER)) cpu_halt();
    return 0;
}

void sync_abandon_current(void)
{
    task_id id = task_current_id();
    for (unsigned i = 0; i < SYNC_OBJECT_LIMIT; ++i)
        if (pool[i].owner == id && release_mutex(&pool[i], true)) cpu_halt();
}

static rum_result_t wait_locked(struct sync_object *object, bool timed, uint32_t ticks)
{
    if (task_current_cancelled()) return -RUM_EINTR;
    if (object->header.type == &mutex_type) {
        task_id id = task_current_id();
        if (object->owner == id) return -RUM_EBUSY;
        if (!object->owner) {
            rum_result_t result = object_acquire(&object->header, OBJECT_OWNER);
            if (result) return result;
            object->owner = id;
            result = object->abandoned ? RUM_WAIT_ABANDONED : 0;
            object->abandoned = false;
            return result;
        }
    } else if (object->value) {
        if (!object->manual) --object->value;
        return 0;
    }
    switch (task_queue_wait_locked(&object->waiters, timed, ticks)) {
    case TASK_WAIT_EVENT: return 0;
    case TASK_WAIT_ABANDONED: return RUM_WAIT_ABANDONED;
    case TASK_WAIT_TIMEOUT: return -RUM_ETIMEDOUT;
    case TASK_WAIT_CANCELLED: return -RUM_EINTR;
    default: return -RUM_EBUSY;
    }
}

rum_result_t sync_request(struct object_handles *table, struct rum_sync_request *r)
{
    if (!table || !r || r->version != RUM_THREAD_ABI_VERSION || r->size != sizeof *r ||
        r->result_handle || r->reserved[0] || r->reserved[1]) return -RUM_EINVAL;
    const struct object_type *type = sync_type(r->type);
    if (!type) return -RUM_EINVAL;
    if (!task_blocking_allowed()) return -RUM_EBUSY;
    if (r->operation == RUM_SYNC_CREATE) return create(table, r, type);
    if (r->flags || r->maximum || (r->operation != RUM_SYNC_WAIT && r->timeout_ms) ||
        (r->value && !(r->operation == RUM_SYNC_SIGNAL && type == &semaphore_type))) return -RUM_EINVAL;

    uint32_t rights, ticks = 0;
    bool timed = r->timeout_ms != RUM_WAIT_FOREVER;
    switch (r->operation) {
    case RUM_SYNC_WAIT:
        if (timed && !timer_duration_ticks(r->timeout_ms, &ticks)) return -RUM_ERANGE;
        rights = RUM_RIGHT_WAIT;
        if (type == &mutex_type) rights |= RUM_RIGHT_SIGNAL;
        break;
    case RUM_SYNC_RESET:
        if (type != &event_type) return -RUM_EINVAL;
        rights = RUM_RIGHT_SIGNAL;
        break;
    case RUM_SYNC_SIGNAL:
        if (type == &semaphore_type && !r->value) return -RUM_EINVAL;
        rights = RUM_RIGHT_SIGNAL;
        break;
    case RUM_SYNC_QUERY: rights = RUM_RIGHT_QUERY; break;
    default: return -RUM_EINVAL;
    }
    struct kernel_object *header;
    rum_result_t result = object_handle_acquire(table, r->handle, r->type, rights, &header);
    if (result) return result;
    struct sync_object *object = (struct sync_object *)header;
    if (header->type != type) result = -RUM_EINVAL;
    else if (r->operation == RUM_SYNC_SIGNAL) {
        if (type == &mutex_type) result = release_mutex(object, false);
        else result = sync_signal_pinned(header, type == &event_type ? 1 : r->value);
    } else {
        uint32_t saved = cpu_interrupt_save();
        if (r->operation == RUM_SYNC_WAIT) result = wait_locked(object, timed, ticks);
        else if (r->operation == RUM_SYNC_RESET) object->value = 0;
        else {
            r->value = type == &mutex_type ? object->owner != 0 : object->value;
            r->maximum = object->maximum;
            r->flags = object->manual ? RUM_EVENT_MANUAL : 0;
        }
        cpu_interrupt_restore(saved);
    }
    if (object_release(header, OBJECT_OPERATION)) cpu_halt();
    return result;
}
