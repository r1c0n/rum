/* Scripted port model: execute the production driver with no privileged I/O. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rum/ata.h>
#include <rum/cpu.h>
#include <rum/io.h>

enum failure { NONE, MISSING, FLOATING, STUCK_BUSY, NO_DRQ, FAULT, ERROR, WRITE_PROTECTED, FLUSH_ERROR };
static enum failure failure;
static uint8_t registers[8], status, opcode;
static uint16_t identify[256];
static unsigned position, commands, polls;
static uint32_t selected_lba, fail_lba;
static unsigned char media[32][512];
static bool interrupt_enabled, test_reentry;

uint32_t cpu_interrupt_save(void)
{
    uint32_t flags = interrupt_enabled ? 0x200 : 0;
    interrupt_enabled = false;
    return flags;
}
void cpu_interrupt_restore(uint32_t flags) { interrupt_enabled = (flags & 0x200) != 0; }

uint8_t inb(uint16_t port)
{
    if (port == 0x376 || port == 0x177) { ++polls; return status; }
    if (port == 0x171) return 4;
    assert(port >= 0x172 && port <= 0x176);
    return registers[port - 0x170];
}

void outb(uint16_t port, uint8_t value)
{
    if (port == 0x376) { assert(value == 2); return; }
    assert(port >= 0x171 && port <= 0x177);
    registers[port - 0x170] = value;
    if (port != 0x177) return;
    ++commands; opcode = value; position = 0;
    selected_lba = registers[3] | ((uint32_t)registers[4] << 8) |
                   ((uint32_t)registers[5] << 16) | ((uint32_t)(registers[6] & 15) << 24);
    if (opcode == 0xec) { status = 0x58; return; }
    assert(opcode == 0x20 || opcode == 0x30 || opcode == 0xe7);
    if (opcode != 0xe7) assert(registers[2] == 1 && (registers[6] & 0xf0) == 0xe0);
    status = opcode == 0xe7 ? 0x50 : 0x58;
    if (selected_lba != fail_lba && failure != FLUSH_ERROR) return;
    switch (failure) {
    case STUCK_BUSY: status = 0x81; break; /* ERR must be ignored while BSY. */
    case NO_DRQ: status = 0x50; break;
    case FAULT: status = 0x60; break;
    case ERROR: status = 0x51; break;
    case WRITE_PROTECTED: if (opcode == 0x30) status = 0x51; break;
    case FLUSH_ERROR: if (opcode == 0xe7) status = 0x51; break;
    default: break;
    }
}

uint16_t inw(uint16_t port)
{
    assert(port == 0x170 && status == 0x58 && position < 256);
    if (test_reentry && opcode == 0x20) {
        test_reentry = false;
        assert(interrupt_enabled);
        assert(block_flush(ata_device()).error == BLOCK_BUSY);
        assert(interrupt_enabled);
    }
    uint16_t value;
    if (opcode == 0xec) value = identify[position];
    else {
        assert(opcode == 0x20);
        unsigned char *data = media[selected_lba % 32];
        value = data[position * 2] | ((uint16_t)data[position * 2 + 1] << 8);
    }
    if (++position == 256) status = 0x50;
    return value;
}

void outw(uint16_t port, uint16_t value)
{
    assert(port == 0x170 && opcode == 0x30 && status == 0x58 && position < 256);
    unsigned char *data = media[selected_lba % 32];
    data[position * 2] = (uint8_t)value; data[position * 2 + 1] = (uint8_t)(value >> 8);
    if (++position == 256) status = 0x50;
}

static void reset(enum failure mode)
{
    failure = mode; status = mode == MISSING ? 0 : mode == FLOATING ? 0xff : 0x50;
    memset(registers, 0, sizeof(registers)); memset(identify, 0, sizeof(identify));
    identify[0] = 0x40; identify[49] = 1u << 9; identify[60] = 32;
    identify[83] = 0x5000; identify[85] = 1u << 5;
    commands = polls = 0; fail_lba = 0; test_reentry = false; interrupt_enabled = true;
    for (unsigned sector = 0; sector < 32; ++sector)
        for (unsigned byte = 0; byte < 512; ++byte)
            media[sector][byte] = (unsigned char)(sector * 13 + byte * 7);
}

int main(void)
{
    unsigned char storage[1537], original[32][512];
    unsigned char *buffer = storage + 1; /* Unaligned buffers are supported. */
    reset(NONE);
    assert(ata_initialize().error == BLOCK_OK && ata_device()->sector_count == 32);
    test_reentry = true;
    assert(block_read(ata_device(), 0, 3, buffer, 1536).completed == 3);
    assert(memcmp(buffer, media[0], 1536) == 0);
    memcpy(original, media, sizeof(media));
    memset(buffer, 0xab, 1536);
    assert(block_write(ata_device(), 5, 3, buffer, 1536).completed == 3);
    assert(block_flush(ata_device()).error == BLOCK_OK && opcode == 0xe7);
    assert(memcmp(media[5], buffer, 1536) == 0);
    assert(memcmp(media[0], original[0], 5 * 512) == 0);
    assert(memcmp(media[8], original[8], 24 * 512) == 0);
    unsigned before = commands;
    assert(block_read(ata_device(), 32, 0, NULL, 0).error == BLOCK_OK);
    assert(block_write(ata_device(), 32, 1, buffer, 512).error == BLOCK_RANGE);
    assert(block_read(ata_device(), UINT64_MAX, 2, buffer, 1024).error == BLOCK_RANGE);
    assert(block_write(ata_device(), 1, UINT64_MAX, buffer, 1536).error == BLOCK_RANGE);
    assert(commands == before && interrupt_enabled);
    reset(NONE); identify[60] = 0; identify[61] = 0x1000;
    assert(ata_initialize().error == BLOCK_OK && ata_device()->sector_count == (UINT64_C(1) << 28));
    assert(block_read(ata_device(), (UINT64_C(1) << 28) - 1, 1, buffer, 512).error == BLOCK_OK);
    assert(selected_lba == 0x0fffffff);
    reset(NONE); identify[49] = 0;
    assert(ata_initialize().error == BLOCK_UNSUPPORTED && !ata_device()->online);
    reset(NONE); identify[106] = 0x5000; identify[117] = 2048;
    assert(ata_initialize().error == BLOCK_UNSUPPORTED);
    reset(NONE); identify[83] = 0x4000;
    assert(ata_initialize().error == BLOCK_UNSUPPORTED); /* No durable flush for enabled cache. */
    reset(NONE); identify[83] = 0x4000; identify[85] = 0;
    assert(ata_initialize().error == BLOCK_OK);
    before = commands;
    assert(block_flush(ata_device()).error == BLOCK_OK && commands == before);
    reset(MISSING); assert(ata_initialize().error == BLOCK_NO_DEVICE && polls < 20);
    reset(FLOATING); assert(ata_initialize().error == BLOCK_NO_DEVICE && polls < 20);
    for (enum failure mode = STUCK_BUSY; mode <= FLUSH_ERROR; ++mode) {
        reset(mode); assert(ata_initialize().error == BLOCK_OK);
        memcpy(original, media, sizeof(media));
        polls = 0;
        struct block_result result = mode == FLUSH_ERROR ? block_flush(ata_device()) :
                                     block_write(ata_device(), 0, 1, buffer, 512);
        enum block_error expected = mode == STUCK_BUSY || mode == NO_DRQ ? BLOCK_TIMEOUT :
                                    mode == FAULT ? BLOCK_DEVICE_FAULT : BLOCK_IO_ERROR;
        assert(result.error == expected && result.completed == 0);
        assert(memcmp(original, media, sizeof(media)) == 0);
        if (expected == BLOCK_TIMEOUT) {
            assert(polls >= 1000000 && polls < 1000020);
            before = commands;
            assert(block_read(ata_device(), 0, 1, buffer, 512).error == BLOCK_NO_DEVICE);
            assert(commands == before);
        } else assert(result.device_error == 4);
        assert(interrupt_enabled && !ata_device()->busy);
    }
    reset(ERROR); fail_lba = 6;
    assert(ata_initialize().error == BLOCK_OK);
    memcpy(original, media, sizeof(media));
    struct block_result partial = block_write(ata_device(), 5, 3, buffer, 1536);
    assert(partial.error == BLOCK_IO_ERROR && partial.completed == 1);
    assert(memcmp(media[5], buffer, 512) == 0 && memcmp(media[6], original[6], 26 * 512) == 0);
    failure = NONE;
    assert(block_read(ata_device(), 0, 1, buffer, 512).error == BLOCK_OK); /* stale ERR */
    puts("ATA port-model tests passed");
}
