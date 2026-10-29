#ifndef RUM_MULTIBOOT_H
#define RUM_MULTIBOOT_H

#include <stddef.h>
#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u
#define MULTIBOOT_MEMORY_MAP (1u << 6)

/* Multiboot v1 wire layout: pointers are 32-bit physical addresses. */
struct multiboot_info {
    uint32_t flags, mem_lower, mem_upper, boot_device, cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t symbols[4];
    uint32_t mmap_length, mmap_addr, drives_length, drives_addr;
    uint32_t config_table, boot_loader_name, apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch, framebuffer_width, framebuffer_height;
    uint8_t framebuffer_bpp, framebuffer_type, framebuffer_color_info[6];
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint32_t size; /* Bytes following this field, at least 20; entries vary. */
    uint64_t address, length;
    uint32_t type;
} __attribute__((packed));

struct multiboot_module {
    uint32_t start, end, string, reserved;
};

_Static_assert(offsetof(struct multiboot_info, mmap_length) == 44, "Multiboot mmap offset");
_Static_assert(offsetof(struct multiboot_info, framebuffer_addr) == 88, "Multiboot framebuffer offset");
_Static_assert(sizeof(struct multiboot_info) == 116, "Multiboot information size");
_Static_assert(sizeof(struct multiboot_mmap_entry) == 24, "Multiboot mmap entry size");

#endif
