#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/object.h>
#include <rum/task.h>

uint32_t handle_test_flags = 0x200;
static bool in_irq, held;
static unsigned attempts;
struct sample { struct kernel_object header; bool fail, allocated; unsigned payload; };
bool irq_in_handler(void) { return in_irq; }
bool task_blocking_allowed(void) { return !in_irq && !held && (handle_test_flags & 0x200); }

static rum_result_t destroy(struct kernel_object *object)
{
    struct sample *sample = (void *)object;
    assert(task_blocking_allowed() && sample->payload == 0x726d);
    struct object_statistics stats = object_stats();
    assert(stats.total.destroying && object->references == 1);
    assert(object_acquire(object, OBJECT_OWNER) == -RUM_EBADF);
    assert(object_transfer(object, OBJECT_OWNER, OBJECT_TABLE) == -RUM_EBADF);
    assert(object_publish(object, OBJECT_OWNER) == -RUM_EINVAL);
    assert(object_release(object, OBJECT_OWNER) == -RUM_EBADF);
    ++attempts;
    if (sample->fail) return -RUM_EIO;
    if (sample->allocated) free(sample);
    else *object = (struct kernel_object){0};
    return 0;
}
static const struct object_type file = { OBJECT_FILE, destroy };
static const struct object_type pipe = { OBJECT_PIPE_READER, destroy };
static void empty(void)
{
    struct object_statistics actual = object_stats(), zero = {0};
    assert(!memcmp(&actual, &zero, sizeof zero));
    assert(handle_test_flags == 0x200);
}
static void construction(void)
{
    struct sample sample = { .payload = 0x726d };
    const struct object_type invalid = { OBJECT_INVALID, destroy }, missing = { OBJECT_FILE, NULL };
    assert(object_initialize(NULL, &file) == -RUM_EINVAL);
    assert(object_initialize(&sample.header, NULL) == -RUM_EINVAL);
    assert(object_initialize(&sample.header, &invalid) == -RUM_EINVAL);
    assert(object_initialize(&sample.header, &missing) == -RUM_EINVAL);
    assert(!object_initialize(&sample.header, &file));
    assert(object_initialize(&sample.header, &file) == -RUM_EINVAL);
    struct object_statistics before = object_stats();
    assert(before.total.objects == 1 && before.total.constructing == 1 && !before.total.published);
    assert(object_acquire(&sample.header, OBJECT_TABLE) == -RUM_EBADF);
    assert(object_publish(&sample.header, OBJECT_REFERENCE_COUNT) == -RUM_EINVAL);
    assert(object_release(&sample.header, OBJECT_TABLE) == -RUM_EBADF);
    struct object_statistics after = object_stats(); assert(!memcmp(&before, &after, sizeof after));
    assert(!object_retype(&sample.header, &pipe));
    after = object_stats();
    assert(!after.kinds[OBJECT_FILE].objects && after.kinds[OBJECT_PIPE_READER].constructing == 1 &&
           after.total.objects == 1 && after.total.references[OBJECT_OWNER] == 1);
    assert(!object_release(&sample.header, OBJECT_OWNER)); /* Failed constructor rollback. */
    assert(object_release(&sample.header, OBJECT_OWNER) == -RUM_EBADF);
    empty();
}
static void ownership(void)
{
    struct sample sample = { .payload = 0x726d };
    unsigned baseline = attempts;
    assert(!object_initialize(&sample.header, &file));
    assert(!object_publish(&sample.header, OBJECT_TABLE));
    assert(object_retype(&sample.header, &pipe) == -RUM_EINVAL);
    assert(!object_acquire(&sample.header, OBJECT_TABLE));
    assert(!object_acquire(&sample.header, OBJECT_OPERATION));
    assert(!object_transfer(&sample.header, OBJECT_TABLE, OBJECT_OWNER));
    assert(!object_transfer(&sample.header, OBJECT_OWNER, OBJECT_OWNER));
    struct object_statistics stats = object_stats();
    assert(stats.total.objects == 1 && stats.total.published == 1 && !stats.total.constructing);
    for (unsigned i = 0; i < OBJECT_REFERENCE_COUNT; ++i)
        assert(stats.total.references[i] == 1 && stats.kinds[OBJECT_FILE].references[i] == 1);
    assert(!object_release(&sample.header, OBJECT_OWNER));
    assert(!object_release(&sample.header, OBJECT_TABLE));
    assert(attempts == baseline && sample.header.references == 1);
    assert(object_release(&sample.header, OBJECT_TABLE) == -RUM_EBADF);
    sample.fail = true;
    struct object_statistics before = object_stats();
    assert(object_release(&sample.header, OBJECT_OPERATION) == -RUM_EIO);
    struct object_statistics after = object_stats(); assert(!memcmp(&before, &after, sizeof after));
    assert(sample.header.state == OBJECT_PUBLISHED && sample.header.owners[OBJECT_OPERATION] == 1);
    sample.fail = false;
    assert(!object_release(&sample.header, OBJECT_OPERATION) && attempts == baseline + 2);
    empty();
    /* A successful destructor may actually free the containing allocation. */
    struct sample *allocated = calloc(1, sizeof *allocated); assert(allocated);
    allocated->payload = 0x726d; allocated->allocated = true;
    assert(!object_initialize(&allocated->header, &pipe));
    assert(!object_publish(&allocated->header, OBJECT_OWNER));
    assert(!object_release(&allocated->header, OBJECT_OWNER));
    empty();
}
static void contexts(void)
{
    struct sample sample = { .payload = 0x726d }, fresh = { .payload = 0x726d };
    assert(!object_initialize(&sample.header, &file));
    uint32_t saved = cpu_interrupt_save();
    assert(!object_publish(&sample.header, OBJECT_TABLE));
    assert(!object_acquire(&sample.header, OBJECT_OPERATION));
    assert(!object_transfer(&sample.header, OBJECT_TABLE, OBJECT_OWNER));
    assert(!object_release(&sample.header, OBJECT_OWNER));
    assert(object_release(&sample.header, OBJECT_OPERATION) == -RUM_EBUSY);
    assert(object_initialize(&fresh.header, &file) == -RUM_EBUSY);
    assert(!handle_test_flags); cpu_interrupt_restore(saved);
    held = true;
    assert(object_release(&sample.header, OBJECT_OPERATION) == -RUM_EBUSY);
    held = false; in_irq = true;
    struct object_statistics before = object_stats();
    assert(object_acquire(&sample.header, OBJECT_OWNER) == -RUM_EBUSY);
    assert(object_transfer(&sample.header, OBJECT_OPERATION, OBJECT_TABLE) == -RUM_EBUSY);
    assert(object_publish(&sample.header, OBJECT_OWNER) == -RUM_EBUSY);
    assert(object_release(&sample.header, OBJECT_OPERATION) == -RUM_EBUSY);
    assert(object_initialize(&fresh.header, &file) == -RUM_EBUSY);
    struct object_statistics after = object_stats(); assert(!memcmp(&before, &after, sizeof after));
    in_irq = false; assert(!object_release(&sample.header, OBJECT_OPERATION)); empty();
}
static void limits(void)
{
    struct sample sample = { .payload = 0x726d };
    assert(!object_initialize(&sample.header, &file) && !object_publish(&sample.header, OBJECT_OWNER));
    for (unsigned i = 1; i < OBJECT_REFERENCE_LIMIT; ++i) assert(!object_acquire(&sample.header, OBJECT_TABLE));
    struct object_statistics before = object_stats();
    assert(object_acquire(&sample.header, OBJECT_OPERATION) == -RUM_EOVERFLOW);
    struct object_statistics after = object_stats(); assert(!memcmp(&before, &after, sizeof after));
    assert(object_transfer(&sample.header, OBJECT_OPERATION, OBJECT_OWNER) == -RUM_EBADF);
    for (unsigned i = 1; i < OBJECT_REFERENCE_LIMIT; ++i) assert(!object_release(&sample.header, OBJECT_TABLE));
    assert(!object_release(&sample.header, OBJECT_OWNER)); empty();

    static struct sample pool[OBJECT_LIMIT + 1];
    for (unsigned i = 0; i < OBJECT_LIMIT; ++i) {
        pool[i].payload = 0x726d;
        assert(!object_initialize(&pool[i].header, i % 2 ? &pipe : &file));
    }
    before = object_stats(); assert(before.total.objects == OBJECT_LIMIT);
    assert(object_initialize(&pool[OBJECT_LIMIT].header, &file) == -RUM_ENOMEM);
    assert(!pool[OBJECT_LIMIT].header.references);
    after = object_stats(); assert(!memcmp(&before, &after, sizeof after));
    for (unsigned i = 0; i < OBJECT_LIMIT; ++i) assert(!object_release(&pool[i].header, OBJECT_OWNER));
    /* Failed capacity reservation must not strand a slot. */
    pool[OBJECT_LIMIT].payload = 0x726d;
    assert(!object_initialize(&pool[OBJECT_LIMIT].header, &file));
    assert(!object_release(&pool[OBJECT_LIMIT].header, OBJECT_OWNER)); empty();
}
int main(void)
{
    empty(); construction(); ownership(); contexts(); limits();
    puts("PASS: object construction, reference transfer/limits, retained operations, retryable destruction and context guards");
    return 0;
}
