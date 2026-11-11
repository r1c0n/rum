#include <rum/object.h>
#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/task.h>

_Static_assert((uint64_t)OBJECT_LIMIT * OBJECT_REFERENCE_LIMIT <= UINT32_MAX,
               "bounded reference accounting fits counters");
static struct object_statistics statistics;

static bool reference_valid(enum object_reference reference)
{
    return (unsigned)reference < OBJECT_REFERENCE_COUNT;
}
static bool live(const struct kernel_object *object)
{
    return object && object->state == OBJECT_PUBLISHED;
}
static void reference_add(enum object_kind kind, enum object_reference reference)
{
    ++statistics.total.references[reference];
    ++statistics.kinds[kind].references[reference];
}
static void reference_remove(enum object_kind kind, enum object_reference reference)
{
    --statistics.total.references[reference];
    --statistics.kinds[kind].references[reference];
}
static void state_remove(enum object_kind kind, enum object_state state)
{
    if (state == OBJECT_CONSTRUCTING) {
        --statistics.total.constructing; --statistics.kinds[kind].constructing;
    } else if (state == OBJECT_PUBLISHED) {
        --statistics.total.published; --statistics.kinds[kind].published;
    } else {
        --statistics.total.destroying; --statistics.kinds[kind].destroying;
    }
}
static void state_add(enum object_kind kind, enum object_state state)
{
    if (state == OBJECT_CONSTRUCTING) {
        ++statistics.total.constructing; ++statistics.kinds[kind].constructing;
    } else if (state == OBJECT_PUBLISHED) {
        ++statistics.total.published; ++statistics.kinds[kind].published;
    } else {
        ++statistics.total.destroying; ++statistics.kinds[kind].destroying;
    }
}
rum_result_t object_initialize(struct kernel_object *object, const struct object_type *type)
{
    if (!object || !type || !type->destroy || type->kind <= OBJECT_INVALID || type->kind >= OBJECT_KIND_COUNT)
        return -RUM_EINVAL;
    if (!task_blocking_allowed()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    rum_result_t error = object->state || object->type || object->references ? -RUM_EINVAL : 0;
    for (unsigned i = 0; !error && i < OBJECT_REFERENCE_COUNT; ++i)
        if (object->owners[i]) error = -RUM_EINVAL;
    if (!error && statistics.total.objects == OBJECT_LIMIT) error = -RUM_ENOMEM;
    if (!error) {
        *object = (struct kernel_object){ .type = type, .state = OBJECT_CONSTRUCTING,
            .references = 1, .owners = {1, 0, 0} };
        ++statistics.total.objects; ++statistics.kinds[type->kind].objects;
        state_add(type->kind, OBJECT_CONSTRUCTING); reference_add(type->kind, OBJECT_OWNER);
    }
    cpu_interrupt_restore(saved); return error;
}
rum_result_t object_retype(struct kernel_object *object, const struct object_type *type)
{
    if (!object || !type || !type->destroy || type->kind <= OBJECT_INVALID || type->kind >= OBJECT_KIND_COUNT)
        return -RUM_EINVAL;
    if (irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    rum_result_t error = object->state != OBJECT_CONSTRUCTING ? -RUM_EINVAL : 0;
    if (!error) {
        enum object_kind old = object->type->kind;
        --statistics.kinds[old].objects; --statistics.kinds[old].constructing;
        --statistics.kinds[old].references[OBJECT_OWNER];
        ++statistics.kinds[type->kind].objects; ++statistics.kinds[type->kind].constructing;
        ++statistics.kinds[type->kind].references[OBJECT_OWNER];
        object->type = type;
    }
    cpu_interrupt_restore(saved); return error;
}
rum_result_t object_publish(struct kernel_object *object, enum object_reference reference)
{
    if (!object || !reference_valid(reference)) return -RUM_EINVAL;
    if (irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    rum_result_t error = object->state != OBJECT_CONSTRUCTING ? -RUM_EINVAL : 0;
    if (!error) {
        enum object_kind kind = object->type->kind;
        --object->owners[OBJECT_OWNER]; ++object->owners[reference];
        reference_remove(kind, OBJECT_OWNER); reference_add(kind, reference);
        state_remove(kind, OBJECT_CONSTRUCTING); state_add(kind, OBJECT_PUBLISHED);
        object->state = OBJECT_PUBLISHED;
    }
    cpu_interrupt_restore(saved); return error;
}
rum_result_t object_acquire(struct kernel_object *object, enum object_reference reference)
{
    if (!reference_valid(reference)) return -RUM_EINVAL;
    if (irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    rum_result_t error = !live(object) ? -RUM_EBADF :
        object->references == OBJECT_REFERENCE_LIMIT ? -RUM_EOVERFLOW : 0;
    if (!error) {
        ++object->references; ++object->owners[reference]; reference_add(object->type->kind, reference);
    }
    cpu_interrupt_restore(saved); return error;
}
rum_result_t object_transfer(struct kernel_object *object, enum object_reference from, enum object_reference to)
{
    if (!reference_valid(from) || !reference_valid(to)) return -RUM_EINVAL;
    if (irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    rum_result_t error = !live(object) || !object->owners[from] ? -RUM_EBADF : 0;
    if (!error) {
        --object->owners[from]; ++object->owners[to];
        reference_remove(object->type->kind, from); reference_add(object->type->kind, to);
    }
    cpu_interrupt_restore(saved); return error;
}
rum_result_t object_release(struct kernel_object *object, enum object_reference reference)
{
    if (!reference_valid(reference)) return -RUM_EINVAL;
    if (irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    if (!object || (object->state != OBJECT_CONSTRUCTING && object->state != OBJECT_PUBLISHED) ||
        !object->owners[reference]) { cpu_interrupt_restore(saved); return -RUM_EBADF; }
    enum object_kind kind = object->type->kind;
    if (object->references > 1) {
        --object->references; --object->owners[reference]; reference_remove(kind, reference);
        cpu_interrupt_restore(saved); return 0;
    }
    /* Check the caller's original IF, not the temporary protected update. */
    cpu_interrupt_restore(saved);
    if (!task_blocking_allowed()) return -RUM_EBUSY;
    saved = cpu_interrupt_save();
    /* No scheduling boundary exists between preflight and this state change.
       Kernel execution is cooperative; IRQ paths cannot mutate ownership. */
    enum object_state previous = object->state;
    rum_result_t (*destroy)(struct kernel_object *) = object->type->destroy;
    object->state = OBJECT_DESTROYING;
    state_remove(kind, previous); state_add(kind, OBJECT_DESTROYING);
    cpu_interrupt_restore(saved);
    rum_result_t result = destroy(object);
    saved = cpu_interrupt_save();
    state_remove(kind, OBJECT_DESTROYING);
    if (result) {
        /* A failed destructor promises that its header/storage is still live. */
        object->state = previous; state_add(kind, previous);
    } else {
        /* Successful callbacks may have freed object. Use saved values only. */
        reference_remove(kind, reference);
        --statistics.total.objects; --statistics.kinds[kind].objects;
    }
    cpu_interrupt_restore(saved); return result > 0 ? -RUM_EIO : result;
}
struct object_statistics object_stats(void)
{
    uint32_t saved = cpu_interrupt_save();
    struct object_statistics result = statistics;
    cpu_interrupt_restore(saved); return result;
}
