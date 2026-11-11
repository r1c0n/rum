#include <rum/object_handle.h>
#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>

_Static_assert(RUM_OBJECT_HANDLE_LIMIT == 32, "five-bit slot identity");
_Static_assert(OBJECT_FILE == RUM_OBJECT_FILE && OBJECT_DIRECTORY == RUM_OBJECT_DIRECTORY &&
               OBJECT_PIPE_READER == RUM_OBJECT_PIPE_READER && OBJECT_PIPE_WRITER == RUM_OBJECT_PIPE_WRITER &&
               OBJECT_CONSOLE_INPUT == RUM_OBJECT_CONSOLE_INPUT && OBJECT_CONSOLE_OUTPUT == RUM_OBJECT_CONSOLE_OUTPUT &&
               OBJECT_PROCESS == RUM_OBJECT_PROCESS, "private/public object type mapping");

uint32_t object_rights_mask(uint32_t type)
{
    switch (type) {
    case RUM_OBJECT_FILE: return RUM_RIGHT_FILE;
    case RUM_OBJECT_DIRECTORY: return RUM_RIGHT_DIRECTORY;
    case RUM_OBJECT_PIPE_READER: case RUM_OBJECT_CONSOLE_INPUT: return RUM_RIGHT_INPUT;
    case RUM_OBJECT_PIPE_WRITER: case RUM_OBJECT_CONSOLE_OUTPUT: return RUM_RIGHT_OUTPUT;
    case RUM_OBJECT_PROCESS: return RUM_RIGHT_PROCESS;
    default: return 0;
    }
}
static rum_object_handle_t token(const struct object_handle_entry *entry, unsigned slot)
{
    return OBJECT_HANDLE_TAG | (entry->generation << 5) | slot;
}
static struct object_handle_entry *lookup(struct object_handles *table, rum_object_handle_t handle)
{
    if (!table || !(handle & OBJECT_HANDLE_TAG)) return NULL;
    struct object_handle_entry *entry = &table->entries[handle & 31u];
    return entry->object && handle == token(entry, handle & 31u) ? entry : NULL;
}
static rum_result_t validate(struct object_handle_entry *entry, uint32_t type, uint32_t required)
{
    if ((type != RUM_OBJECT_ANY && !object_rights_mask(type)) || (required & ~RUM_RIGHT_MASK)) return -RUM_EINVAL;
    if (!entry) return -RUM_EBADF;
    if (entry->closing) return -RUM_EBUSY;
    uint32_t actual = entry->object->type->kind;
    if ((type && type != actual) || (required & ~object_rights_mask(actual))) return -RUM_EINVAL;
    return required & ~entry->rights ? -RUM_EACCES : 0;
}
static int available(const struct object_handles *table)
{
    for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT; ++i)
        if (!table->entries[i].object && table->entries[i].generation < OBJECT_HANDLE_GENERATION_MAX) return (int)i;
    return -1;
}
static void install(struct object_handles *table, unsigned slot, struct kernel_object *object,
                     uint32_t rights, uint32_t flags, rum_object_handle_t *output)
{
    struct object_handle_entry *entry = &table->entries[slot];
    ++entry->generation;
    entry->object = object; entry->rights = rights; entry->flags = flags; entry->closing = false;
    *output = token(entry, slot);
}
rum_result_t object_handle_install(struct object_handles *table, struct kernel_object *object,
                                  uint32_t authorized, uint32_t granted, uint32_t flags, rum_object_handle_t *output)
{
    if (!table || !object || !output || !object->type || !object_rights_mask(object->type->kind) ||
        (authorized & ~object_rights_mask(object->type->kind)) || (granted & ~object_rights_mask(object->type->kind)) ||
        (flags & ~RUM_OBJECT_INHERIT)) return -RUM_EINVAL;
    if (granted & ~authorized) return -RUM_EACCES;
    if (irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    int slot = available(table);
    rum_result_t result = slot < 0 ? -RUM_EMFILE : object_transfer(object, OBJECT_OWNER, OBJECT_TABLE);
    if (!result) install(table, (unsigned)slot, object, granted, flags, output);
    cpu_interrupt_restore(saved); return result;
}
rum_result_t object_handle_acquire(struct object_handles *table, rum_object_handle_t handle, uint32_t type,
                                  uint32_t required, struct kernel_object **output)
{
    if (!output) return -RUM_EINVAL;
    if (irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    struct object_handle_entry *entry = lookup(table, handle);
    rum_result_t result = validate(entry, type, required);
    if (!result) result = object_acquire(entry->object, OBJECT_OPERATION);
    if (!result) *output = entry->object;
    cpu_interrupt_restore(saved); return result;
}
static rum_result_t close_entry(struct object_handle_entry *entry)
{
    struct kernel_object *object = entry->object;
    /* Caller drops its update scope before a destructor may run. */
    rum_result_t result = object_release(object, OBJECT_TABLE);
    uint32_t saved = cpu_interrupt_save();
    if (!result) { entry->object = NULL; entry->rights = entry->flags = 0; }
    entry->closing = false;
    cpu_interrupt_restore(saved); return result;
}
rum_result_t object_request_validate(const struct rum_object_request *request)
{
    if (!request || request->version != RUM_OBJECT_ABI_VERSION || request->size != sizeof *request ||
        request->result_handle || request->reserved[0] || request->reserved[1] || request->reserved[2] || request->reserved[3] ||
        request->operation < RUM_OBJECT_QUERY || request->operation > RUM_OBJECT_FROM_DESCRIPTOR ||
        (request->type != RUM_OBJECT_ANY && !object_rights_mask(request->type)) ||
        (request->rights & ~RUM_RIGHT_MASK) ||
        ((request->operation == RUM_OBJECT_QUERY || request->operation == RUM_OBJECT_CLOSE) && (request->rights || request->flags)) ||
        (request->flags & ~RUM_OBJECT_INHERIT)) return -RUM_EINVAL;
    return 0;
}
rum_result_t object_handle_request(struct object_handles *table, struct rum_object_request *request)
{
    rum_result_t error = object_request_validate(request);
    if (error) return error;
    if (!table) return -RUM_EINVAL;
    if (request->operation == RUM_OBJECT_FROM_DESCRIPTOR) return -RUM_ENOSYS; /* Resource-service boundary. */
    if (irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    struct object_handle_entry *entry = lookup(table, request->handle);
    uint32_t needed = request->operation == RUM_OBJECT_QUERY ? RUM_RIGHT_QUERY :
                      request->operation == RUM_OBJECT_DUPLICATE ? RUM_RIGHT_DUPLICATE : 0;
    rum_result_t result = validate(entry, request->type, needed);
    if (!result && request->operation == RUM_OBJECT_DUPLICATE) {
        if (request->rights & ~object_rights_mask(entry->object->type->kind)) result = -RUM_EINVAL;
        else if (request->rights & ~entry->rights) result = -RUM_EACCES;
        int slot = available(table);
        if (!result && slot < 0) result = -RUM_EMFILE;
        if (!result) result = object_acquire(entry->object, OBJECT_TABLE);
        if (!result) install(table, (unsigned)slot, entry->object, request->rights, request->flags, &request->result_handle);
    } else if (!result && request->operation == RUM_OBJECT_QUERY) {
        request->type = entry->object->type->kind; request->rights = entry->rights; request->flags = entry->flags;
    } else if (!result) {
        entry->closing = true;
        cpu_interrupt_restore(saved);
        return close_entry(entry);
    }
    cpu_interrupt_restore(saved); return result;
}
rum_result_t object_handles_inherit(struct object_handles *destination, const struct object_handles *source,
                                   uint32_t ceiling)
{
    if (!destination || !source || destination == source || (ceiling & ~RUM_RIGHT_MASK)) return -RUM_EINVAL;
    if (irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT; ++i)
        if (destination->entries[i].object || destination->entries[i].generation ||
            destination->entries[i].rights || destination->entries[i].flags || destination->entries[i].closing) {
            cpu_interrupt_restore(saved); return -RUM_EINVAL;
        }
    unsigned acquired = 0;
    rum_result_t result = 0;
    for (; acquired < RUM_OBJECT_HANDLE_LIMIT; ++acquired) {
        const struct object_handle_entry *entry = &source->entries[acquired];
        if (!entry->object || !(entry->flags & RUM_OBJECT_INHERIT)) continue;
        if (entry->closing) { result = -RUM_EBUSY; break; }
        result = object_acquire(entry->object, OBJECT_TABLE);
        if (result) break;
    }
    if (result) {
        /* Source entries still own references under this same protected scope,
           so rollback cannot invoke a final destructor. */
        for (unsigned i = 0; i < acquired; ++i)
            if (source->entries[i].object && (source->entries[i].flags & RUM_OBJECT_INHERIT))
                (void)object_release(source->entries[i].object, OBJECT_TABLE);
    } else for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT; ++i) {
        destination->entries[i].generation = source->entries[i].generation;
        if (source->entries[i].object && (source->entries[i].flags & RUM_OBJECT_INHERIT)) {
            destination->entries[i] = source->entries[i];
            destination->entries[i].rights &= ceiling;
        }
    }
    cpu_interrupt_restore(saved); return result;
}
rum_result_t object_handles_destroy(struct object_handles *table)
{
    if (!table) return -RUM_EINVAL;
    rum_result_t result = 0;
    for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT; ++i) if (table->entries[i].object) {
        struct rum_object_request request = { .version = RUM_OBJECT_ABI_VERSION, .size = sizeof request,
            .operation = RUM_OBJECT_CLOSE, .handle = token(&table->entries[i], i) };
        rum_result_t error = object_handle_request(table, &request);
        if (error && !result) result = error;
    }
    return result;
}
unsigned object_handles_count(const struct object_handles *table)
{
    unsigned count = 0;
    if (table) for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT; ++i) count += table->entries[i].object != NULL;
    return count;
}
