#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/io.h>
#include <rum/keyboard.h>
#include <rum/keyboard_decode.h>

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64
#define POLL_LIMIT 100000u
#define QUEUE_SIZE 128u

static struct keyboard_decoder decoder;
static volatile char queue[QUEUE_SIZE];
static volatile uint32_t head, tail, dropped;

static bool write_port(uint16_t port, uint8_t value)
{
    for (unsigned poll = 0; poll < POLL_LIMIT; ++poll) {
        if (!(inb(PS2_STATUS) & 2)) {
            outb(port, value);
            return true;
        }
        io_wait();
    }
    return false;
}

static bool read_reply(uint8_t *reply)
{
    for (unsigned poll = 0; poll < POLL_LIMIT; ++poll) {
        uint8_t status = inb(PS2_STATUS);
        if (status & 1) {
            uint8_t value = inb(PS2_DATA);
            if (!(status & 0xE0)) { /* Discard mouse, parity, or timeout data. */
                *reply = value;
                return true;
            }
        }
        io_wait();
    }
    return false;
}

static bool keyboard_command(uint8_t command)
{
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint8_t reply;
        if (!write_port(PS2_DATA, command) || !read_reply(&reply))
            return false;
        if (reply == 0xFA) return true; /* ACK */
        if (reply != 0xFE) return false; /* RESEND */
    }
    return false;
}

static void keyboard_interrupt(void)
{
    /* Bounded work; text output and editing belong to the foreground loop. */
    for (unsigned count = 0; count < 32; ++count) {
        uint8_t status = inb(PS2_STATUS);
        if (!(status & 1)) break;
        uint8_t scancode = inb(PS2_DATA);
        if (status & 0xE0) continue;
        char character = keyboard_decode(&decoder, scancode);
        if (!character) continue;
        uint32_t next = (head + 1) & (QUEUE_SIZE - 1);
        if (next == tail) {
            ++dropped; /* Drop newest; never overwrite unread characters. */
        } else {
            queue[head] = character;
            head = next;
        }
    }
}

bool keyboard_initialize(void)
{
    decoder = (struct keyboard_decoder){0};
    head = tail = dropped = 0;
    if (!write_port(PS2_COMMAND, 0xAD) || !write_port(PS2_COMMAND, 0xA7))
        return false; /* Disable both ports while configuring the controller. */
    for (unsigned count = 0; count < 256 && (inb(PS2_STATUS) & 1); ++count)
        (void)inb(PS2_DATA);
    uint8_t config;
    if (!write_port(PS2_COMMAND, 0x20) || !read_reply(&config))
        return false;
    config = (uint8_t)((config & ~0x03u) | 0x70u);
    if (!write_port(PS2_COMMAND, 0x60) || !write_port(PS2_DATA, config) ||
        !write_port(PS2_COMMAND, 0xAE))
        return false;
    /* Use device set 2 plus controller translation, giving set 1 to the CPU. */
    if (!keyboard_command(0xF5) || !keyboard_command(0xF0) ||
        !keyboard_command(0x02) || !keyboard_command(0xF4))
        return false;
    irq_register(1, keyboard_interrupt);
    /* First port enabled, IRQ1 and translation enabled, mouse port disabled. */
    config = (uint8_t)((config & ~0x13u) | 0x61u);
    return write_port(PS2_COMMAND, 0x60) && write_port(PS2_DATA, config);
}

bool keyboard_read(char *character)
{
    uint32_t flags = cpu_interrupt_save();
    bool available = tail != head;
    if (available) {
        *character = queue[tail];
        tail = (tail + 1) & (QUEUE_SIZE - 1);
    }
    cpu_interrupt_restore(flags);
    return available;
}

uint32_t keyboard_dropped(void)
{
    return dropped;
}
