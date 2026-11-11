#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <rum/power.h>
#include <rum/task.h>
#include <rum/io.h>
#include <rum/cpu.h>

static jmp_buf halted;
static unsigned writes, enables, reads, flushes, closes, remaining, quiesced_after;
static uint16_t last_port, last_value, control;
static uint32_t ticks;
static bool user, blocking = true, enable_works = true;
static enum fs_error open_error, flush_error;
static char output[256];
static struct multiboot_info boot = { .flags = MULTIBOOT_MEMORY_MAP, .mmap_addr = 0x110000,
                                     .mmap_length = sizeof(struct multiboot_mmap_entry) };
uint32_t cpu_interrupt_save(void) { return 0; }
_Noreturn void cpu_halt(void) { longjmp(halted, 1); }
void outb(uint16_t port, uint8_t value) { assert(port == 0xB2 && value == 0xA1); ++enables; if (enable_works) control |= 1; }
uint8_t inb(uint16_t port) { (void)port; return 0; }
void outw(uint16_t port, uint16_t value) { ++writes; last_port = port; last_value = value; }
uint16_t inw(uint16_t port) { assert(port == 0x1234); ++reads; return control; }
void io_wait(void) {}
void terminal_writestring(const char *text) { assert(strlen(output) + strlen(text) < sizeof output); strcat(output, text); }
void serial_writestring(const char *text) { (void)text; }
bool task_current_is_process(void) { return user; }
bool task_blocking_allowed(void) { return blocking; }
uint32_t task_reap(void) { return 0; }
uint32_t task_process_list(struct rum_process_entry *entries, uint32_t capacity, uint32_t *total)
{ assert(!entries && !capacity); *total = remaining; return 0; }
bool task_yield(void) { ++ticks; if (quiesced_after && ticks == quiesced_after) remaining = 0; return true; }
uint32_t timer_ticks(void) { return ticks; }
enum fs_error fs_open(const struct fs_context *cwd, const char *path, unsigned flags, fs_reference *ref)
{ assert(!cwd && !strcmp(path, "/") && flags == FS_READ); *ref = 1; return open_error; }
enum fs_error fs_flush(fs_reference ref) { assert(ref == 1 && !remaining); ++flushes; return flush_error; }
enum fs_error fs_close(fs_reference ref) { assert(ref == 1); ++closes; return FS_OK; }

static void word(void *address, uint32_t value) { memcpy(address, &value, 4); }
static uint8_t *at(uint32_t address) { return (void *)(uintptr_t)address; }
static void seal(uint8_t *p, unsigned size, unsigned sum_offset)
{
    p[sum_offset] = 0;
    uint8_t sum = 0;
    for (unsigned i = 0; i < size; ++i) sum += p[i];
    p[sum_offset] = (uint8_t)-sum;
}
static void fixture(void)
{
    memset(at(0x80000), 0, 0xA0000);
    struct multiboot_mmap_entry entry = { .size = 20, .address = 0x100000, .length = 0x20000, .type = 2 };
    memcpy(at(0x110000), &entry, sizeof entry);
    memcpy(at(0xE0000), "RSD PTR ", 8); word(at(0xE0010), 0x100000); seal(at(0xE0000), 20, 8);
    memcpy(at(0x100000), "RSDT", 4); word(at(0x100004), 40); word(at(0x100024), 0x101000); seal(at(0x100000), 40, 9);
    memcpy(at(0x101000), "FACP", 4); word(at(0x101004), 116); word(at(0x101028), 0x102000);
    word(at(0x101030), 0xB2); at(0x101000)[52] = 0xA1;
    word(at(0x101040), 0x1234); at(0x101000)[89] = 2; seal(at(0x101000), 116, 9);
    memcpy(at(0x102000), "DSDT", 4); word(at(0x102004), 49);
    const uint8_t aml[] = {0x08, '\\', '_', 'S', '5', '_', 0x12, 6, 2, 0x0A, 5, 0x0A, 5};
    memcpy(at(0x102024), aml, sizeof aml); seal(at(0x102000), 49, 9);
    writes = enables = reads = 0; control = 1; output[0] = 0;
}
static void stop(void)
{
    if (!setjmp(halted)) power_machine(POWER_SHUTDOWN);
    assert(strstr(output, "safe to turn off"));
}
int main(void)
{
    assert(mmap(at(0x80000), 0xA0000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
    fixture(); power_initialize(&boot); stop();
    assert(writes == 1 && last_port == 0x1234 && last_value == 0x3401);
    fixture(); control = 0; power_initialize(&boot); stop(); assert(enables == 1 && writes == 1);
    fixture(); control = 0; enable_works = false; power_initialize(&boot); stop();
    assert(enables == 1 && !writes && reads < 100010); enable_works = true;
    fixture(); at(0xE0000)[8] ^= 1; power_initialize(&boot); stop(); assert(!writes && !reads);
    fixture(); word(at(0x100004), 0xFFFFFFFF); power_initialize(&boot); stop(); assert(!writes);
    fixture(); word(at(0x101028), 0xFFFFFFF0); seal(at(0x101000), 116, 9);
    power_initialize(&boot); stop(); assert(!writes);
    fixture(); at(0x102024)[7] = 63; seal(at(0x102000), 49, 9);
    power_initialize(&boot); stop(); assert(!writes); /* Truncated AML package. */
    fixture(); at(0x102024)[10] = 8; seal(at(0x102000), 49, 9);
    power_initialize(&boot); stop(); assert(!writes); /* Invalid sleep type. */
    fixture(); ((struct multiboot_mmap_entry *)at(0x110000))->length = 32;
    power_initialize(&boot); stop(); assert(!writes); /* Table outside described RAM. */
    fixture(); power_initialize(NULL); stop(); assert(!writes); /* Missing memory map. */
    fixture(); power_initialize(&boot);
    user = true; assert(system_power(POWER_SHUTDOWN) == FS_ACCESS && !flushes && !writes);
    user = false; blocking = false; assert(system_power(POWER_SHUTDOWN) == FS_ACCESS); blocking = true;
    assert(system_power((enum power_action)99) == FS_INVALID);
    flush_error = FS_IO_ERROR; assert(system_power(POWER_SHUTDOWN) == FS_IO_ERROR && flushes == 1 && closes == 1 && !writes);
    open_error = FS_IO_ERROR; assert(system_power(POWER_SHUTDOWN) == FS_IO_ERROR && flushes == 1);
    open_error = FS_OK; flush_error = FS_OK; remaining = 1;
    assert(system_power(POWER_REBOOT) == FS_BUSY && flushes == 1 && !writes);
    ticks = 0; quiesced_after = 3;
    if (!setjmp(halted)) (void)system_power(POWER_SHUTDOWN);
    assert(!remaining && ticks == 3 && flushes == 2 && closes == 2 && writes == 1);
    open_error = FS_UNAVAILABLE; output[0] = 0;
    if (!setjmp(halted)) (void)system_power(POWER_SHUTDOWN);
    assert(writes == 2 && flushes == 2);
    puts("PASS: bounded ACPI discovery, malformed firmware, authority, cleanup and flush failures");
}
