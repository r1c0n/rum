#include <rum/abi/error.h>
#include <rum/block.h>
#include <rum/cpu.h>
#include <rum/diagnostics.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/object.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include "fs-backend.h"

extern const char __kernel_start[], __kernel_end[];
void kernel_main(uint32_t magic, uint32_t information);
static struct task_event irq_event;
static unsigned irq_checks, callbacks;
static bool paused, finished;
struct sample { struct kernel_object object; fs_reference file; bool fail, pause; };
static struct sample guard;

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_objects_failed: "); serial_writestring(name);
        serial_writestring("\n"); cpu_halt();
    }
}
static rum_result_t destroy(struct kernel_object *object)
{
    struct sample *sample = (void *)object;
    check(task_blocking_allowed() && object_stats().total.destroying == 1,
          "destructor runs outside protected updates");
    ++callbacks;
    if (sample->fail) return -RUM_EIO;
    if (sample->pause) { paused = true; check(task_yield(), "destructor may yield"); }
    if (sample->file) check(fs_close(sample->file) == FS_OK, "destructor releases backend reference");
    if (sample != &guard) check(kfree(sample), "destructor frees containing allocation");
    else *object = (struct kernel_object){0};
    return 0;
}
static const struct object_type type = { OBJECT_FILE, destroy, NULL };
static void refuse_final(void)
{
    unsigned before = callbacks;
    check(!task_blocking_allowed() && object_release(&guard.object, OBJECT_OWNER) == -RUM_EBUSY &&
          callbacks == before && guard.object.references == 1, "protected final release retains ownership");
}
static enum fs_error guarded_stat(void *context, uint64_t id, struct fs_node *node)
{
    check(fs_operation_active(), "filesystem guard is active"); refuse_final();
    return fs_test_backend()->operations->stat(context, id, node);
}
static struct block_result guarded_read(struct block_device *device, uint64_t lba, uint64_t count, void *buffer)
{
    (void)device; (void)lba; (void)buffer;
    check(block_request_active(), "block guard is active"); refuse_final();
    return (struct block_result){ .completed = count };
}
static void tick(void)
{
    check(irq_in_handler() && !task_blocking_allowed(), "real PIT IRQ context");
    unsigned before = callbacks;
    uint32_t pages = pmm_stats().free_pages;
    check(object_release(&guard.object, OBJECT_OWNER) == -RUM_EBUSY &&
          object_acquire(&guard.object, OBJECT_OPERATION) == -RUM_EBUSY &&
          object_transfer(&guard.object, OBJECT_OWNER, OBJECT_TABLE) == -RUM_EBUSY,
          "IRQ cannot change object ownership");
    struct object_statistics stats = object_stats();
    check(stats.total.objects == 1 && stats.total.references[OBJECT_OWNER] == 1 &&
          callbacks == before && pmm_stats().free_pages == pages, "IRQ snapshot allocates nothing");
    ++irq_checks; task_event_signal(&irq_event);
}
static struct sample *open_sample(void)
{
    struct sample *sample = kcalloc(1, sizeof *sample);
    check(sample && !object_initialize(&sample->object, &type), "construct private file owner");
    check(fs_open(NULL, "/ROOT.TXT", FS_READ, &sample->file) == FS_OK, "retain real filesystem identity");
    return sample;
}
static void operation(void *argument)
{
    struct sample *sample = argument;
    char data[4];
    struct fs_io_result result = fs_read(sample->file, 0, data, sizeof data);
    check(result.error == FS_OK && result.transferred == sizeof data && !memcmp(data, "root", sizeof data),
          "operation survives final table close");
    check(!object_release(&sample->object, OBJECT_OPERATION), "worker releases final operation reference");
    finished = true;
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize();
    check(!task_blocking_allowed(), "uninitialized task context cannot destroy objects");
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && task_initialize(), "memory and task startup");
    check(!task_blocking_allowed(), "IF-clear boot context cannot destroy objects");
    cpu_interrupt_enable();
    check(!object_initialize(&guard.object, &type) && !object_publish(&guard.object, OBJECT_OWNER), "guard owner");
    uint32_t saved = cpu_interrupt_save(); refuse_final(); cpu_interrupt_restore(saved);
    check(task_switch_hold(), "enter short scheduling scope"); refuse_final();
    check(task_switch_release(), "leave scheduling scope");

    fs_test_backend_initialize();
    struct fs_operations operations = *fs_test_backend()->operations;
    operations.stat = guarded_stat;
    struct fs_backend backend = *fs_test_backend(); backend.operations = &operations;
    check(fs_initialize() && fs_mount_disk(&backend) == FS_OK, "mount private backend");
    const struct block_operations block_ops = { .read = guarded_read };
    struct block_device device = { .sector_count = 1, .sector_size = 512, .online = true, .operations = &block_ops };
    char buffer[512]; check(block_read(&device, 0, 1, buffer, sizeof buffer).error == BLOCK_OK, "guarded block request");

    timer_initialize(); irq_register(0, tick); pic_unmask(0);
    uint32_t sequence = task_event_sequence(&irq_event);
    check(task_wait(&irq_event, sequence) && irq_checks, "idle wakes from real IRQ");
    pic_mask(0); /* The bounded IRQ probe is complete; workers now run cooperatively. */

    unsigned references = fs_stats().references, pins = fs_test_backend_pins();
    struct heap_statistics heap = heap_stats();
    uint32_t pages = pmm_stats().free_pages;
    struct sample *sample = open_sample(); sample->fail = true;
    check(object_release(&sample->object, OBJECT_OWNER) == -RUM_EIO &&
          sample->object.state == OBJECT_CONSTRUCTING && fs_stats().references == references + 1,
          "failed constructor cleanup preserves retryable owner");
    sample->fail = false; check(!object_release(&sample->object, OBJECT_OWNER), "retry unpublished cleanup");

    sample = open_sample(); sample->pause = true;
    check(!object_publish(&sample->object, OBJECT_TABLE) && !object_acquire(&sample->object, OBJECT_OPERATION),
          "publish file with independent operation reference");
    check(task_create(operation, sample, NULL), "transfer operation to worker");
    check(!object_release(&sample->object, OBJECT_TABLE) && fs_remove(NULL, "/ROOT.TXT") == FS_BUSY,
          "closed table leaves operation and backend pinned");
    check(task_yield() && paused && !finished, "destructor yields while closing");
    struct kernel_diagnostics snapshot;
    check(diagnostics_capture(&snapshot) && snapshot.objects.total.objects == 2 &&
          snapshot.objects.total.destroying == 1 && snapshot.objects.total.references[OBJECT_OPERATION] == 1,
          "consistent diagnostics retain destroying objects");
    check(object_acquire(&sample->object, OBJECT_OWNER) == -RUM_EBADF &&
          fs_remove(NULL, "/ROOT.TXT") == FS_BUSY, "closing object cannot be resurrected or removed");
    while (!finished) check(task_yield(), "finish worker destruction");
    (void)task_reap();
    check(fs_stats().references == references && fs_test_backend_pins() == pins &&
          heap_stats().used_bytes == heap.used_bytes && heap_stats().allocations == heap.allocations &&
          (uint64_t)pmm_stats().free_pages + heap_stats().mapped_bytes / PAGE_SIZE ==
          (uint64_t)pages + heap.mapped_bytes / PAGE_SIZE, "final close restores backend, heap and page ledgers");
    check(fs_remove(NULL, "/ROOT.TXT") == FS_OK && fs_unmount_disk() == FS_OK, "final backend release permits removal");
    check(!object_release(&guard.object, OBJECT_OWNER) && !object_stats().total.objects, "all objects reclaimed");
    serial_writestring("rum_objects_ok\n"); cpu_halt();
}
