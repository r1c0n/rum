#include <rum/power.h>
#include <rum/cpu.h>
#include <rum/io.h>
#include <rum/memory.h>
#include <rum/memory_layout.h>
#include <rum/serial.h>
#include <rum/terminal.h>

/* Legacy PC ACPI: checksum-validated RSDP/RSDT/FADT/DSDT, System I/O PM1
   controls, and a static Name(_S5, Package(...)). No general AML execution. */
#define TABLE_LIMIT (256u * 1024u)
#define POLL_LIMIT 100000u
static const struct multiboot_info *boot_memory;
static uint32_t smi_command;
static uint16_t pm1a, pm1b, sleep_a, sleep_b;
static uint8_t acpi_enable;

static uint32_t word32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static const uint8_t *physical(uint32_t address, uint32_t size)
{
    if (address < 4096 || address >= RUM_IDENTITY_END || size > RUM_IDENTITY_END - address) return NULL;
    if (address >= 0x80000 && address < 0x100000 && size <= 0x100000 - address)
        return (const void *)(uintptr_t)address;
    /* Firmware may store ACPI tables in reserved RAM beyond the allocator's
       available-RAM limit. Read only ranges described by the boot memory map. */
    const uint8_t *map = (const void *)(uintptr_t)boot_memory->mmap_addr;
    uint32_t offset = 0;
    for (unsigned i = 0; i < 4096 && offset < boot_memory->mmap_length; ++i) {
        if (boot_memory->mmap_length - offset < sizeof(struct multiboot_mmap_entry)) return NULL;
        struct multiboot_mmap_entry entry;
        memcpy(&entry, map + offset, sizeof entry);
        uint64_t stride = (uint64_t)entry.size + 4;
        if (entry.size < 20 || stride > boot_memory->mmap_length - offset) return NULL;
        if (entry.type >= 1 && entry.type <= 4 && address >= entry.address &&
            (uint64_t)address - entry.address <= entry.length && size <= entry.length - ((uint64_t)address - entry.address))
            return (const void *)(uintptr_t)address;
        offset += (uint32_t)stride;
    }
    return NULL;
}
static bool checksum(const uint8_t *p, uint32_t bytes)
{
    uint8_t sum = 0;
    for (uint32_t i = 0; i < bytes; ++i) sum += p[i];
    return sum == 0;
}
static const uint8_t *table(uint32_t address, const char *signature)
{
    const uint8_t *p = physical(address, 36);
    if (!p || memcmp(p, signature, 4)) return NULL;
    uint32_t bytes = word32(p + 4);
    return bytes >= 36 && bytes <= TABLE_LIMIT && physical(address, bytes) && checksum(p, bytes) ? p : NULL;
}
static const uint8_t *rsdp(uint32_t start, uint32_t bytes)
{
    for (uint32_t offset = 0; offset + 20 <= bytes; offset += 16) {
        const uint8_t *p = physical(start + offset, 20);
        if (p && !memcmp(p, "RSD PTR ", 8) && checksum(p, 20)) return p;
    }
    return NULL;
}
static bool integer(const uint8_t **cursor, const uint8_t *end, uint32_t *value)
{
    if (*cursor == end) return false;
    uint8_t op = *(*cursor)++;
    if (op <= 1) { *value = op; return true; }
    unsigned bytes = op == 0x0A ? 1 : op == 0x0B ? 2 : op == 0x0C ? 4 : 0;
    if (!bytes || (size_t)(end - *cursor) < bytes) return false;
    *value = 0;
    for (unsigned i = 0; i < bytes; ++i) *value |= (uint32_t)*(*cursor)++ << (8 * i);
    return true;
}
static bool sleep_types(const uint8_t *dsdt)
{
    const uint8_t *end = dsdt + word32(dsdt + 4);
    for (const uint8_t *p = dsdt + 36; end - p >= 7; ++p) {
        if (*p != 0x08) continue; /* NameOp; optional root prefix. */
        const uint8_t *name = p + 1;
        if (*name == '\\') ++name;
        if (end - name < 6 || memcmp(name, "_S5_", 4) || name[4] != 0x12) continue;
        const uint8_t *length = name + 5, *cursor = length;
        uint8_t lead = *cursor++;
        unsigned extra = lead >> 6;
        if (extra && (lead & 0x30)) continue;
        if ((size_t)(end - cursor) < extra) continue;
        uint32_t bytes = lead & (extra ? 15u : 63u);
        for (unsigned i = 0; i < extra; ++i) bytes |= (uint32_t)*cursor++ << (4 + 8 * i);
        if (bytes > (uint32_t)(end - length) || bytes <= (uint32_t)(cursor - length)) continue;
        const uint8_t *package_end = length + bytes;
        if (*cursor++ < 2) continue;
        uint32_t a, b;
        if (!integer(&cursor, package_end, &a) || !integer(&cursor, package_end, &b) || a > 7 || b > 7) continue;
        sleep_a = (uint16_t)(a << 10); sleep_b = (uint16_t)(b << 10); return true;
    }
    return false;
}

void power_initialize(const struct multiboot_info *info)
{
    boot_memory = info;
    pm1a = pm1b = 0;
    if (!info || !(info->flags & MULTIBOOT_MEMORY_MAP) || !info->mmap_addr ||
        info->mmap_length > UINT32_MAX - info->mmap_addr) return;
    /* BDA itself lies on guarded page zero; the conventional EBDA search starts
       at 0x80000 and is bounded to the last 128 KiB of base memory. */
    const uint8_t *root = rsdp(0x80000, 0x20000);
    if (!root) root = rsdp(0xE0000, 0x20000);
    const uint8_t *rsdt = root ? table(word32(root + 16), "RSDT") : NULL;
    if (!rsdt || (word32(rsdt + 4) - 36) % 4) return;
    for (uint32_t offset = 36; offset < word32(rsdt + 4); offset += 4) {
        const uint8_t *fadt = table(word32(rsdt + offset), "FACP");
        if (!fadt || word32(fadt + 4) < 90) continue;
        uint32_t a = word32(fadt + 64), b = word32(fadt + 68), smi = word32(fadt + 48);
        if (!a || a > 65534 || b > 65534 || smi > 65535 || fadt[89] < 2) continue;
        const uint8_t *dsdt = table(word32(fadt + 40), "DSDT");
        if (!dsdt || !sleep_types(dsdt)) continue;
        pm1a = (uint16_t)a; pm1b = (uint16_t)b;
        smi_command = smi; acpi_enable = fadt[52]; return;
    }
}

static void message(const char *text)
{
    terminal_writestring(text); serial_writestring(text);
}
_Noreturn void power_machine(enum power_action action)
{
    (void)cpu_interrupt_save();
    if (action == POWER_REBOOT) {
        message("Rebooting...\n");
        for (unsigned i = 0; i < POLL_LIMIT; ++i) {
            if (!(inb(0x64) & 2)) { outb(0x64, 0xFE); break; }
            io_wait();
        }
        for (unsigned i = 0; i < 1000; ++i) io_wait();
        /* No reset response from the keyboard controller: force a CPU reset. */
        struct { uint16_t limit; uint32_t base; } __attribute__((packed)) empty = {0, 0};
        __asm__ volatile ("lidt %0; int $3" : : "m"(empty) : "memory");
    } else {
        message("Shutting down...\n");
        if (pm1a) {
            if (!(inw(pm1a) & 1) && smi_command && acpi_enable) {
                outb((uint16_t)smi_command, acpi_enable);
                for (unsigned i = 0; i < POLL_LIMIT && !(inw(pm1a) & 1); ++i) io_wait();
            }
            if (inw(pm1a) & 1) {
                if (pm1b) outw(pm1b, (uint16_t)((inw(pm1b) & ~0x3C00u) | sleep_b | 0x2000u));
                outw(pm1a, (uint16_t)((inw(pm1a) & ~0x3C00u) | sleep_a | 0x2000u));
            }
        }
        message("System halted. It is safe to turn off the computer.\n");
    }
    cpu_halt();
}
