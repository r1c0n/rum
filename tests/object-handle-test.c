#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/object_handle.h>
#include <rum/task.h>

uint32_t handle_test_flags = 0x200;
static bool in_irq;
static unsigned destroyed;
struct sample { struct kernel_object object; bool fail, allocated; };
bool irq_in_handler(void) { return in_irq; }
bool task_blocking_allowed(void) { return !in_irq && (handle_test_flags & 0x200); }
static rum_result_t destroy(struct kernel_object *object)
{
    struct sample *sample = (void *)object;
    assert(task_blocking_allowed());
    if (sample->fail) return -RUM_EIO;
    ++destroyed;
    if (sample->allocated) free(sample);
    else *object = (struct kernel_object){0};
    return 0;
}
static const struct object_type types[] = {
    {OBJECT_INVALID, destroy}, {OBJECT_FILE, destroy}, {OBJECT_DIRECTORY, destroy},
    {OBJECT_PIPE_READER, destroy}, {OBJECT_PIPE_WRITER, destroy},
    {OBJECT_CONSOLE_INPUT, destroy}, {OBJECT_CONSOLE_OUTPUT, destroy}, {OBJECT_PROCESS, destroy}
};
static struct rum_object_request packet(unsigned action, rum_object_handle_t handle)
{
    return (struct rum_object_request){ .version = RUM_OBJECT_ABI_VERSION, .size = sizeof(struct rum_object_request),
        .operation = action, .handle = handle };
}
static rum_object_handle_t give(struct object_handles *table, struct sample *sample, unsigned kind,
                                uint32_t rights, unsigned flags)
{
    rum_object_handle_t handle = 0;
    assert(!object_initialize(&sample->object, &types[kind]) && !object_publish(&sample->object, OBJECT_OWNER));
    assert(!object_handle_install(table, &sample->object, object_rights_mask(kind), rights, flags, &handle));
    assert(handle & OBJECT_HANDLE_TAG); return handle;
}
static void fails(struct object_handles *table, struct rum_object_request request, rum_result_t error)
{
    struct rum_object_request original = request;
    struct object_statistics before = object_stats();
    assert(object_handle_request(table, &request) == error && !memcmp(&original, &request, sizeof request));
    struct object_statistics after = object_stats(); assert(!memcmp(&before, &after, sizeof after));
}
static void close_handle(struct object_handles *table, rum_object_handle_t handle)
{
    struct rum_object_request request = packet(RUM_OBJECT_CLOSE, handle);
    assert(!object_handle_request(table, &request));
}
static void empty(void)
{
    struct object_statistics stats = object_stats(), zero = {0};
    assert(!memcmp(&stats, &zero, sizeof stats) && handle_test_flags == 0x200);
}
static void rights_and_packets(void)
{
    struct object_handles table = {0}; struct sample sample = {0};
    assert(!object_initialize(&sample.object, &types[OBJECT_FILE]) && !object_publish(&sample.object, OBJECT_OWNER));
    rum_object_handle_t handle = 17;
    assert(object_handle_install(&table, &sample.object, RUM_RIGHT_READ, RUM_RIGHT_WRITE, 0, &handle) == -RUM_EACCES);
    assert(handle == 17 && sample.object.owners[OBJECT_OWNER] == 1);
    assert(object_handle_install(&table, &sample.object, RUM_RIGHT_FILE, RUM_RIGHT_TERMINATE, 0, &handle) == -RUM_EINVAL);
    assert(!object_handle_install(&table, &sample.object, RUM_RIGHT_FILE, RUM_RIGHT_COMMON | RUM_RIGHT_READ, 0, &handle));
    struct rum_object_request request = packet(RUM_OBJECT_QUERY, handle);
    assert(!object_handle_request(&table, &request) && request.type == RUM_OBJECT_FILE &&
           request.rights == (RUM_RIGHT_COMMON | RUM_RIGHT_READ) && !request.flags);
    request = packet(RUM_OBJECT_QUERY, handle); request.type = RUM_OBJECT_PROCESS; fails(&table, request, -RUM_EINVAL);
    request.type = 99; fails(&table, request, -RUM_EINVAL);
    request = packet(RUM_OBJECT_QUERY, handle); request.version++; fails(&table, request, -RUM_EINVAL);
    request = packet(RUM_OBJECT_QUERY, handle); request.size--; fails(&table, request, -RUM_EINVAL);
    request = packet(RUM_OBJECT_QUERY, handle); request.reserved[3] = 1; fails(&table, request, -RUM_EINVAL);
    request = packet(RUM_OBJECT_CLOSE, handle); request.rights = RUM_RIGHT_READ; fails(&table, request, -RUM_EINVAL);
    request = packet(RUM_OBJECT_QUERY, handle); request.flags = 1; fails(&table, request, -RUM_EINVAL);
    request = packet(RUM_OBJECT_QUERY, handle); request.result_handle = handle; fails(&table, request, -RUM_EINVAL);
    request = packet(99, handle); fails(&table, request, -RUM_EINVAL);
    request = packet(RUM_OBJECT_DUPLICATE, handle); request.rights = 0x80000000; fails(&table, request, -RUM_EINVAL);
    request.rights = RUM_RIGHT_TERMINATE; fails(&table, request, -RUM_EINVAL);
    request.rights = RUM_RIGHT_WRITE; fails(&table, request, -RUM_EACCES);
    request.rights = RUM_RIGHT_QUERY; request.flags = 2; fails(&table, request, -RUM_EINVAL);
    request.flags = 0; assert(!object_handle_request(&table, &request));
    rum_object_handle_t restricted = request.result_handle;
    request = packet(RUM_OBJECT_DUPLICATE, restricted); fails(&table, request, -RUM_EACCES);
    request.rights = 0x80000000u; fails(&table, request, -RUM_EINVAL);
    struct kernel_object *object = NULL;
    assert(object_handle_acquire(&table, restricted, RUM_OBJECT_FILE, RUM_RIGHT_READ, &object) == -RUM_EACCES && !object);
    assert(object_handle_acquire(&table, handle, RUM_OBJECT_PIPE_READER, RUM_RIGHT_READ, &object) == -RUM_EINVAL && !object);
    assert(!object_handle_acquire(&table, handle, RUM_OBJECT_FILE, RUM_RIGHT_READ, &object));
    close_handle(&table, handle); close_handle(&table, restricted);
    assert(object->references == 1 && object->owners[OBJECT_OPERATION] == 1);
    assert(!object_release(object, OBJECT_OPERATION)); empty();
    for (unsigned kind = OBJECT_FILE; kind < OBJECT_KIND_COUNT; ++kind) {
        struct sample item = {0}; handle = give(&table, &item, kind, object_rights_mask(kind), 0);
        request = packet(RUM_OBJECT_QUERY, handle); request.type = kind;
        assert(!object_handle_request(&table, &request) && request.rights == object_rights_mask(kind));
        close_handle(&table, handle);
    }
    empty();
}
static void identities_and_limits(void)
{
    struct object_handles table = {0}; struct sample sample = {0};
    rum_object_handle_t old = give(&table, &sample, OBJECT_FILE, RUM_RIGHT_FILE, 0);
    close_handle(&table, old);
    rum_object_handle_t current = give(&table, &sample, OBJECT_FILE, RUM_RIGHT_FILE, 0);
    assert(current != old && (current & 31) == (old & 31));
    fails(&table, packet(RUM_OBJECT_CLOSE, old), -RUM_EBADF);
    fails(&table, packet(RUM_OBJECT_QUERY, current ^ 0x20u), -RUM_EBADF);
    fails(&table, packet(RUM_OBJECT_QUERY, OBJECT_HANDLE_TAG), -RUM_EBADF);
    for (unsigned fd = 0; fd < 32; ++fd) fails(&table, packet(RUM_OBJECT_QUERY, fd), -RUM_EBADF);
    for (unsigned i = 1; i < RUM_OBJECT_HANDLE_LIMIT; ++i) {
        struct rum_object_request request = packet(RUM_OBJECT_DUPLICATE, current); request.rights = RUM_RIGHT_QUERY;
        assert(!object_handle_request(&table, &request));
    }
    assert(object_handles_count(&table) == RUM_OBJECT_HANDLE_LIMIT);
    fails(&table, packet(RUM_OBJECT_DUPLICATE, current), -RUM_EMFILE);
    assert(!object_handles_destroy(&table)); empty();
    /* Seed valid near-wrap private history instead of performing 2^26 opens. */
    for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT; ++i) table.entries[i].generation = OBJECT_HANDLE_GENERATION_MAX;
    table.entries[31].generation--;
    current = give(&table, &sample, OBJECT_FILE, RUM_RIGHT_QUERY, 0); assert(current == UINT32_MAX);
    close_handle(&table, current);
    assert(!object_initialize(&sample.object, &types[OBJECT_FILE]) && !object_publish(&sample.object, OBJECT_OWNER));
    rum_object_handle_t unchanged = 47;
    assert(object_handle_install(&table, &sample.object, RUM_RIGHT_FILE, 0, 0, &unchanged) == -RUM_EMFILE && unchanged == 47);
    assert(!object_release(&sample.object, OBJECT_OWNER)); empty();
}
static void inheritance(void)
{
    struct object_handles parent = {0}, child = {0}; struct sample a = {0}, b = {0};
    rum_object_handle_t inherited = give(&parent, &a, OBJECT_FILE, RUM_RIGHT_FILE, RUM_OBJECT_INHERIT);
    rum_object_handle_t private = give(&parent, &b, OBJECT_PROCESS, RUM_RIGHT_PROCESS, 0);
    assert(!object_handles_inherit(&child, &parent, RUM_RIGHT_QUERY));
    struct rum_object_request request = packet(RUM_OBJECT_QUERY, inherited);
    assert(!object_handle_request(&child, &request) && request.rights == RUM_RIGHT_QUERY);
    fails(&child, packet(RUM_OBJECT_QUERY, private), -RUM_EBADF);
    fails(&child, packet(RUM_OBJECT_DUPLICATE, inherited), -RUM_EACCES);
    assert(object_handles_inherit(&child, &parent, RUM_RIGHT_MASK) == -RUM_EINVAL);
    assert(!object_handles_destroy(&parent) && object_stats().total.objects == 1);
    assert(!object_handles_destroy(&child)); empty();

    parent = (struct object_handles){0}; child = (struct object_handles){0};
    (void)give(&parent, &a, OBJECT_FILE, RUM_RIGHT_FILE, RUM_OBJECT_INHERIT);
    (void)give(&parent, &b, OBJECT_FILE, RUM_RIGHT_FILE, RUM_OBJECT_INHERIT);
    for (unsigned i = 1; i < OBJECT_REFERENCE_LIMIT; ++i) assert(!object_acquire(&b.object, OBJECT_OWNER));
    struct object_statistics before = object_stats();
    assert(object_handles_inherit(&child, &parent, RUM_RIGHT_MASK) == -RUM_EOVERFLOW);
    struct object_statistics after = object_stats(); assert(!memcmp(&before, &after, sizeof before));
    struct object_handles zero = {0}; assert(!memcmp(&child, &zero, sizeof zero));
    for (unsigned i = 1; i < OBJECT_REFERENCE_LIMIT; ++i) assert(!object_release(&b.object, OBJECT_OWNER));
    assert(!object_handles_destroy(&parent)); empty();
}
static void selected_inheritance(void)
{
    for (unsigned kind = OBJECT_FILE; kind < OBJECT_KIND_COUNT; ++kind) {
        struct object_handles parent = {0}, child = {0}; struct sample a = {0}, b = {0};
        rum_object_handle_t handle = give(&parent, &a, kind, RUM_RIGHT_QUERY, RUM_OBJECT_INHERIT);
        rum_object_handle_t private = give(&parent, &b, kind, RUM_RIGHT_QUERY, 0);
        struct rum_object_inheritance grant = { .handle = handle, .type = kind, .rights = RUM_RIGHT_QUERY };
        struct object_handles zero = {0}; struct object_statistics before = object_stats();
        struct rum_object_inheritance invalid[] = {grant, grant};
        assert(object_handles_prepare_spawn(&child, &parent, invalid, 2) == -RUM_EINVAL);
        invalid[0].handle = private;
        assert(object_handles_prepare_spawn(&child, &parent, invalid, 1) == -RUM_EACCES);
        invalid[0] = grant; invalid[0].rights = RUM_RIGHT_DUPLICATE;
        assert(object_handles_prepare_spawn(&child, &parent, invalid, 1) == -RUM_EACCES);
        invalid[0] = grant; invalid[0].type = kind == OBJECT_FILE ? OBJECT_PROCESS : OBJECT_FILE;
        assert(object_handles_prepare_spawn(&child, &parent, invalid, 1) == -RUM_EINVAL);
        invalid[0] = grant; invalid[0].flags = 2;
        assert(object_handles_prepare_spawn(&child, &parent, invalid, 1) == -RUM_EINVAL);
        invalid[0] = grant; invalid[0].reserved[1] = 1;
        assert(object_handles_prepare_spawn(&child, &parent, invalid, 1) == -RUM_EINVAL);
        invalid[0] = grant; invalid[0].handle ^= 0x20;
        assert(object_handles_prepare_spawn(&child, &parent, invalid, 1) == -RUM_EBADF);
        struct object_statistics after = object_stats();
        assert(!memcmp(&before, &after, sizeof before) && !memcmp(&child, &zero, sizeof zero));
        parent.entries[31].generation = OBJECT_HANDLE_GENERATION_MAX;
        assert(!object_handles_prepare_spawn(&child, &parent, &grant, 1));
        assert(child.entries[31].generation == OBJECT_HANDLE_GENERATION_MAX);
        struct rum_object_request request = packet(RUM_OBJECT_QUERY, handle);
        assert(!object_handle_request(&child, &request) && request.rights == RUM_RIGHT_QUERY && !request.flags);
        fails(&child, packet(RUM_OBJECT_QUERY, private), -RUM_EBADF);
        assert(!object_handles_destroy(&parent) && object_stats().total.objects == 1);
        assert(!object_handles_destroy(&child)); empty();
    }
    struct object_handles parent = {0}, child = {0}, zero = {0}; struct sample samples[32] = {0};
    struct rum_object_inheritance grants[32];
    for (unsigned i = 0; i < 32; ++i) grants[i] = (struct rum_object_inheritance){
        .handle = give(&parent, &samples[i], OBJECT_FILE, RUM_RIGHT_FILE, RUM_OBJECT_INHERIT),
        .rights = RUM_RIGHT_READ, .flags = RUM_OBJECT_INHERIT };
    for (unsigned i = 1; i < OBJECT_REFERENCE_LIMIT; ++i) assert(!object_acquire(&samples[31].object, OBJECT_OWNER));
    struct object_statistics before = object_stats();
    assert(object_handles_prepare_spawn(&child, &parent, grants, 32) == -RUM_EOVERFLOW);
    struct object_statistics after = object_stats();
    assert(!memcmp(&before, &after, sizeof before) && !memcmp(&child, &zero, sizeof zero));
    for (unsigned i = 1; i < OBJECT_REFERENCE_LIMIT; ++i) assert(!object_release(&samples[31].object, OBJECT_OWNER));
    assert(!object_handles_prepare_spawn(&child, &parent, grants, 32) && object_handles_count(&child) == 32);
    assert(!object_handles_destroy(&child)); child = zero;
    assert(!object_handles_prepare_spawn(&child, &parent, NULL, 0) && !object_handles_count(&child) && child.entries[0].generation);
    assert(!object_handles_destroy(&parent)); empty();
}
static void cleanup_context(void)
{
    struct object_handles table = {0};
    struct sample *sample = calloc(1, sizeof *sample); assert(sample); sample->allocated = true;
    rum_object_handle_t handle = give(&table, sample, OBJECT_FILE, RUM_RIGHT_FILE, 0);
    uint32_t flags = cpu_interrupt_save();
    fails(&table, packet(RUM_OBJECT_CLOSE, handle), -RUM_EBUSY);
    cpu_interrupt_restore(flags);
    sample->fail = true; fails(&table, packet(RUM_OBJECT_CLOSE, handle), -RUM_EIO);
    sample->fail = false; in_irq = true;
    fails(&table, packet(RUM_OBJECT_QUERY, handle), -RUM_EBUSY);
    fails(&table, packet(RUM_OBJECT_CLOSE, handle), -RUM_EBUSY);
    in_irq = false; close_handle(&table, handle); empty();
}
int main(void)
{
    rights_and_packets(); identities_and_limits(); inheritance(); selected_inheritance(); cleanup_context();
    assert(destroyed);
    puts("PASS: typed handles, type/rights validation, generation retirement, capacity, inheritance and close rollback");
    return 0;
}
