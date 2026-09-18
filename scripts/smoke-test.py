#!/usr/bin/env python3
"""Check boots, memory/storage, CPU faults, IRQs, PS/2 input and commands."""
import argparse
import json
import os
import re
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time


def qmp_command(stream, command, arguments=None):
    request = {"execute": command}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write((json.dumps(request) + "\n").encode())
    stream.flush()
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError("QEMU closed its monitor")
        response = json.loads(line)
        if "error" in response:
            raise RuntimeError(str(response["error"]))
        if "return" in response:
            return response["return"]


def elf_symbols(path):
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x01":
        raise RuntimeError(f"Expected a little-endian ELF32 kernel: {path}")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    sections = [struct.unpack_from("<10I", data, header[6] + i * header[11])
                for i in range(header[12])]
    symbols = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, address, _, _, _, _ = struct.unpack_from("<IIIBBH", data, offset)
            end = names.find(b"\0", name)
            if end > name:
                symbols[names[name:end].decode()] = address
    return symbols


def boot_layout_test(symbols):
    # Check the actual linked result against rum's unchanged boot contract.
    start, end = symbols["__kernel_start"], symbols["__kernel_end"]
    bottom, top = symbols["__boot_stack_bottom"], symbols["__boot_stack_top"]
    if start != 0x200000 or end % 4096 or not start < end <= 0x40000000:
        raise RuntimeError("Kernel exceeds its boot/identity layout")
    if not start <= bottom < top <= end or top - bottom != 16384 or bottom % 16 or top % 16:
        raise RuntimeError("Boot stack differs from its size/alignment/reservation contract")


def dump_ram(stream, address, length, path):
    response = qmp_command(stream, "human-monitor-command", {
        "command-line": f'pmemsave {address:#x} {length} "{path.as_posix()}"'
    })
    if response.strip():
        raise RuntimeError(f"Physical memory dump failed: {response}")
    data = path.read_bytes()
    if len(data) != length:
        raise RuntimeError(f"Incomplete memory dump: {path}")
    return data


def dump_virtual(stream, address, length, path):
    response = qmp_command(stream, "human-monitor-command", {
        "command-line": f'memsave {address:#x} {length} "{path.as_posix()}"'
    })
    if response.strip():
        raise RuntimeError(f"Virtual memory dump failed: {response}")
    data = path.read_bytes()
    if len(data) != length:
        raise RuntimeError("Incomplete virtual memory dump")
    return data


def physical_memory_test(stream, symbols, artifacts, mode, registers, serial_text, project):
    match = re.search(r"rum_memory_ok info=(\d+) usable=(\d+) managed=(\d+) free=(\d+) limit=(\d+) directory=(\d+)", serial_text)
    if not match:
        raise RuntimeError("Missing physical memory boot report")
    info_address, usable, managed_count, free, limit, directory_address = map(int, match.groups())
    cap, page_size = 1 << 30, 4096
    if not 0 < limit <= cap or limit % page_size:
        raise RuntimeError("Invalid identity window limit")
    controls = {name: int(value, 16) for name, value in re.findall(r"\b(CR[034])=([0-9a-fA-F]+)", registers)}
    if (controls.get("CR0", 0) & 0x80010000 != 0x80010000 or
            controls.get("CR3") != directory_address or controls.get("CR4", 0) & 0xB0):
        raise RuntimeError(f"Paging control registers incorrect: {controls}")
    # Derive allocator eligibility from the real bootloader map and metadata.
    info = dump_ram(stream, info_address, 116, artifacts / f"{mode}-multiboot.bin")
    words = struct.unpack_from("<22I", info)
    flags, map_length, map_address = words[0], words[11], words[12]
    if not flags & 0x40:
        raise RuntimeError("Bootloader did not provide a memory map")
    raw = dump_ram(stream, map_address, map_length, artifacts / f"{mode}-memory-map.bin")
    entries, offset = [], 0
    while offset < len(raw):
        size, address, length, kind = struct.unpack_from("<IQQI", raw, offset)
        if size < 20 or offset + size + 4 > len(raw):
            raise RuntimeError("Malformed QEMU memory map")
        entries.append((address, length, kind))
        offset += size + 4
    pages = set()
    for address, length, kind in entries:
        if kind == 1:
            pages.update(range((address + 4095) // 4096, min(address + length, cap) // 4096))

    def block(address, length):
        if length:
            pages.difference_update(range(address // 4096, (min(address + length, cap) + 4095) // 4096))

    for address, length, kind in entries:
        if kind != 1:
            block(address, length)
    if usable != len(pages) or limit != (max(pages) + 1) * 4096:
        raise RuntimeError("Usable RAM count/limit disagrees with Multiboot map")
    block(0, 0x100000)
    block(symbols["__kernel_start"], symbols["__kernel_end"] - symbols["__kernel_start"])
    block(info_address, 116)
    block(map_address, map_length)

    def string(address):
        if not address:
            return
        data = dump_ram(stream, address, 4096, artifacts / f"{mode}-boot-string.bin")
        end = data.find(b"\0")
        if end < 0:
            raise RuntimeError("Unterminated boot string")
        block(address, end + 1)

    if flags & 4: string(words[4])
    if flags & 0x200: string(words[16])
    if flags & 8 and words[5]:
        block(words[6], words[5] * 16)
        modules = dump_ram(stream, words[6], words[5] * 16, artifacts / f"{mode}-modules.bin")
        for start, end, name, _ in struct.iter_unpack("<4I", modules):
            block(start, end - start)
            string(name)
    if flags & 0x10: block(words[9], words[7] + words[8])
    if flags & 0x20 and words[7]:
        block(words[9], words[7] * words[8])
        sections = dump_ram(stream, words[9], words[7] * words[8], artifacts / f"{mode}-elf-sections.bin")
        for offset in range(0, len(sections), words[8]):
            address, size = struct.unpack_from("<I", sections, offset + 12)[0], struct.unpack_from("<I", sections, offset + 20)[0]
            if address: block(address, size)
    if flags & 0x80: block(words[14], words[13])
    if flags & 0x100 and words[15]:
        data = dump_ram(stream, words[15], 2, artifacts / f"{mode}-bios-config.bin")
        block(words[15], struct.unpack("<H", data)[0] + 2)
    if flags & 0x400: block(words[17], 20)
    if flags & 0x800:
        block(words[18], 512)
        block(words[19], 256)
        _, segment, offset, length = struct.unpack_from("<4H", info, 80)
        block(segment * 16 + offset, length)
    if flags & 0x1000:
        address, pitch, _, height = struct.unpack_from("<QIII", info, 88)
        block(address, pitch * height)
        if info[109] == 0:
            palette, colors = struct.unpack_from("<IH", info, 110)
            block(palette, colors * 3)
    expected = bytearray(cap // page_size // 8)
    for page in pages: expected[page // 8] |= 1 << (page % 8)
    actual = dump_ram(stream, symbols["managed"], len(expected), artifacts / f"{mode}-managed-pages.bin")
    allocated = dump_ram(stream, symbols["allocated"], len(expected), artifacts / f"{mode}-allocated-pages.bin")
    if actual != expected or managed_count != len(pages):
        raise RuntimeError("Physical allocator failed to reserve boot/kernel memory")
    directory = struct.unpack("<1024I", dump_ram(stream, directory_address, 4096,
                                                artifacts / f"{mode}-page-directory.bin"))
    table_count = (limit + (1 << 22) - 1) >> 22
    storage = re.search(r"rum_storage_ok mapped=(\d+) used=(\d+) allocations=(\d+) files=(\d+) bytes=(\d+)", serial_text)
    if not storage:
        raise RuntimeError("Missing heap/filesystem boot report")
    heap_mapped, heap_used, heap_allocations, file_count, file_bytes = map(int, storage.groups())
    if not 0 < heap_mapped <= 4 * 1024 * 1024 or heap_mapped % 4096:
        raise RuntimeError("Invalid heap virtual window")
    heap_index = 256
    populated = set(range(table_count)) | {heap_index}
    if (any(entry for index, entry in enumerate(directory) if index not in populated) or
            any(directory[index] & ~0x20 & 0xFFF != 3 for index in populated)):
        raise RuntimeError("Unexpected page directory entries")
    frames = {directory_address, *(directory[index] & 0xFFFFF000 for index in populated)}
    if len(frames) != len(populated) + 1 or any(frame // 4096 not in pages for frame in frames):
        raise RuntimeError("Page tables overlap or use reserved frames")
    structure_count = len(frames)
    heap_table = struct.unpack("<1024I", dump_ram(stream, directory[heap_index] & 0xFFFFF000, 4096,
                                                 artifacts / f"{mode}-heap-table.bin"))
    heap_frames = []
    heap_dump = bytearray()
    for slot, entry in enumerate(heap_table):
        if slot >= heap_mapped // 4096:
            if entry:
                raise RuntimeError("Heap maps pages beyond its committed size")
            continue
        physical = entry & 0xFFFFF000
        if entry & ~0x60 & 0xFFF != 3 or physical in frames or physical // 4096 not in pages:
            raise RuntimeError("Heap mappings overlap, use reserved RAM, or have wrong permissions")
        frames.add(physical)
        heap_frames.append(physical)
        heap_dump.extend(dump_ram(stream, physical, 4096, artifacts / f"{mode}-heap-page-{slot}.bin"))
    owned = bytearray(len(expected))
    for frame in frames: owned[frame // 4096 // 8] |= 1 << (frame // 4096 % 8)
    if allocated != owned or free != managed_count - len(frames):
        raise RuntimeError("Page table/heap physical frame accounting incorrect")
    # Read the tables in one contiguous span rather than hundreds of monitor calls.
    table_frames = [entry & 0xFFFFF000 for entry in directory[:table_count]]
    low, high = min(table_frames), max(table_frames) + 4096
    raw_tables = dump_ram(stream, low, high - low, artifacts / f"{mode}-page-tables.bin")
    for index, frame in enumerate(table_frames):
        table = struct.unpack_from("<1024I", raw_tables, frame - low)
        for slot, entry in enumerate(table):
            physical = (index * 1024 + slot) * 4096
            if physical == 0 or physical >= limit:
                if entry:
                    raise RuntimeError("Null guard or end of RAM is mapped")
                continue
            readonly = any(symbols[start] <= physical < symbols[end] for start, end in
                           (("__text_start", "__text_end"), ("__rodata_start", "__rodata_end")))
            expected_entry = physical | (1 if readonly else 3) | (0x10 if 0xA0000 <= physical < 0x100000 else 0)
            if entry & ~0x60 != expected_entry:
                raise RuntimeError(f"Wrong mapping/protection for {physical:#x}: {entry:#x}")
    # Independently walk real i386 heap headers and RAM file nodes.
    heap_base, offset, previous = 0x40000000, 0, 0
    live_blocks, used = {}, 0
    previous_free = False
    while offset < len(heap_dump):
        size, prev, following = struct.unpack_from("<III", heap_dump, offset)
        is_free = heap_dump[offset + 12]
        end = offset + 16 + size
        if (size % 16 or end > len(heap_dump) or prev != previous or is_free not in (0, 1) or
                following != (heap_base + end if end < len(heap_dump) else 0) or
                (is_free and previous_free)):
            raise RuntimeError("Heap headers, alignment or coalescing are inconsistent")
        if not is_free:
            live_blocks[heap_base + offset + 16] = size
            used += size
        previous, previous_free, offset = heap_base + offset, is_free, end
    if used != heap_used or len(live_blocks) != heap_allocations:
        raise RuntimeError("Heap accounting disagrees with its live blocks")

    def heap_bytes(address, length):
        offset = address - heap_base
        if offset < 0 or offset + length > len(heap_dump):
            raise RuntimeError("RAM file points outside the heap")
        return heap_dump[offset:offset + length]

    node = struct.unpack("<I", dump_ram(stream, symbols["ramfs_first"], 4,
                                       artifacts / f"{mode}-ramfs-root.bin"))[0]
    loaded, owners = {}, set()
    while node:
        if node in owners or live_blocks.get(node, 0) < 76:
            raise RuntimeError("RAM filesystem list is invalid")
        owners.add(node)
        following, size, address = struct.unpack_from("<III", heap_bytes(node, 76))
        name = bytes(heap_bytes(node + 12, 64)).split(b"\0", 1)[0].decode("ascii")
        if name in loaded:
            raise RuntimeError("Duplicate RAM filename")
        if size:
            if address in owners or live_blocks.get(address, 0) < size:
                raise RuntimeError("RAM file payload has no live heap allocation")
            owners.add(address)
        elif address:
            raise RuntimeError("Empty RAM file owns unexpected data")
        loaded[name] = bytes(heap_bytes(address, size)) if size else b""
        node = following
    assets = {path.name: path.read_bytes() for path in (project / "assets/ramfs").iterdir()}
    if (loaded != assets or len(loaded) != file_count or sum(map(len, loaded.values())) != file_bytes or
            owners != set(live_blocks)):
        raise RuntimeError("Embedded RAM files differ from assets or leak heap allocations")
    report = {"usable_pages": usable, "managed_pages": managed_count, "free_pages": free,
              "identity_limit": limit, "page_table_frames": structure_count,
              "heap_pages": len(heap_frames), "heap_used_bytes": used, "files": file_count}
    (artifacts / f"{mode}-memory.json").write_text(json.dumps(report, indent=2) + "\n")


def cpu_tables_test(stream, symbols, artifacts, mode, live=False):
    registers = qmp_command(stream, "human-monitor-command", {"command-line": "info registers"})
    (artifacts / f"{mode}-cpu.txt").write_text(registers)
    for name, expected_base, expected_limit in (("GDT", symbols["rum_gdt"], 23),
                                               ("IDT", symbols["idt"], 2047)):
        match = re.search(rf"\b{name}=\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", registers)
        if not match or tuple(int(value, 16) for value in match.groups()) != (expected_base, expected_limit):
            raise RuntimeError(f"Wrong {name}:\n{registers}")
    for name, selector in (("CS", 8), ("DS", 16), ("ES", 16), ("SS", 16), ("FS", 16), ("GS", 16)):
        match = re.search(rf"\b{name}\s*=\s*([0-9a-fA-F]+)", registers)
        if not match or int(match[1], 16) != selector:
            raise RuntimeError(f"Wrong {name} selector: {registers}")
    flags = re.search(r"\bEFL=([0-9a-fA-F]+)", registers)
    if not flags or int(flags[1], 16) & 0x400 or bool(int(flags[1], 16) & 0x200) != live:
        raise RuntimeError("Wrong CPU interrupt/direction flags")
    gdt = dump_ram(stream, symbols["rum_gdt"], 24, artifacts / f"{mode}-gdt.bin")
    if gdt != struct.pack("<QQQ", 0, 0x00CF9B000000FFFF, 0x00CF93000000FFFF):
        raise RuntimeError("Unexpected GDT descriptors")
    idt = dump_ram(stream, symbols["idt"], 2048, artifacts / f"{mode}-idt.bin")
    for vector in range(48):
        low, selector, reserved, attributes, high = struct.unpack_from("<HHBBH", idt, vector * 8)
        target = f"exception_{vector}" if vector < 32 else f"irq_{vector - 32}"
        if ((high << 16 | low) != symbols[target]
                or (selector, reserved, attributes) != (8, 0, 0x8E)):
            raise RuntimeError(f"Wrong IDT gate {vector}")
    if any(idt[48 * 8:]):
        raise RuntimeError("Unexpected IDT gates above the PIC range")
    return registers


def timer_test(stream, symbols, artifacts, mode):
    def count():
        raw = dump_ram(stream, symbols["ticks"], 4, artifacts / f"{mode}-ticks.bin")
        return struct.unpack("<I", raw)[0]
    first = count()
    qmp_command(stream, "cont")
    time.sleep(0.35)
    qmp_command(stream, "stop")
    second = count()
    if not 2 <= ((second - first) & 0xFFFFFFFF) < 200:
        raise RuntimeError(f"PIT interrupts stopped or ran too fast: {first} -> {second}")


def stop_at_idle(stream):
    # Sample the idle loop, rather than a transient CLI region or an ISR.
    for _ in range(20):
        qmp_command(stream, "stop")
        registers = qmp_command(stream, "human-monitor-command", {"command-line": "info registers"})
        if "HLT=1" in registers:
            return
        qmp_command(stream, "cont")
        time.sleep(0.02)
    raise RuntimeError("Kernel did not reach interrupt-enabled HLT")


def guest_keyboard(stream, serial):
    def send(*keys):
        qmp_command(stream, "send-key", {
            "keys": [{"type": "qcode", "data": key} for key in keys], "hold-time": 20
        })
        time.sleep(0.07)  # Release modifiers before the next input.

    def expect(suffix):
        deadline = time.monotonic() + 3
        while not serial.read_bytes().endswith(suffix.encode()):
            if time.monotonic() > deadline:
                raise RuntimeError(f"PS/2 echo mismatch: expected {suffix!r}, got {serial.read_bytes()[-200:]!r}")
            time.sleep(0.02)

    def type_text(text):
        special = {" ": "spc", "\n": "ret", "\t": "tab", "\b": "backspace", ".": "dot", "/": "slash", "-": "minus"}
        for character in text:
            if character in special:
                send(special[character])
            elif character == "!":
                send("shift", "1")
            elif character == "_":
                send("shift", "minus")
            elif "A" <= character <= "Z":
                send("shift", character.lower())
            elif "a" <= character <= "z" or character.isdigit():
                send(character)
            else:
                raise ValueError(f"Unsupported test input {character!r}")
    return send, expect, type_text


def keyboard_test(stream, symbols, artifacts, mode, serial):
    qmp_command(stream, "cont")
    send, expect, _ = guest_keyboard(stream, serial)

    send("backspace")  # Cannot erase the prompt at the start of a line.
    expect("rum_boot_ok\r\n")
    for key in ("r", "u", "m"):
        send(key)
    expect("rum")
    send("shift", "a")
    send("shift_r", "1")
    send("caps_lock")
    send("b")
    send("shift", "c")
    send("caps_lock")
    send("x")
    send("backspace")
    send("d")
    expect("rumA!Bcx\b \bd")
    # Navigation, Print Screen, Pause and Ctrl/Alt must not leak text or shifts.
    for keys in (("up",), ("print",), ("pause",), ("ctrl", "a"), ("alt", "b")):
        send(*keys)
    send("e")
    send("tab")
    send("f")
    expect("rumA!Bcx\b \bde    f")
    send("ret")
    for key in ("r", "u", "m"):
        send(key)
    send("spc")
    for key in ("i", "s", "spc", "a", "l", "i", "v", "e"):
        send(key)
    send("shift", "1")
    expect("\r\n> rum is alive!")
    stop_at_idle(stream)
    memory = dump_ram(stream, 0xB8000, 4000, artifacts / f"{mode}-keyboard-vga.bin")
    screen = "\n".join(memory[y*160:(y+1)*160:2].decode("ascii") for y in range(25))
    for text in ("> rumA!Bcde    f", "> rum is alive!", "uptime:"):
        if text not in screen:
            raise RuntimeError(f"Missing edited keyboard text {text!r}:\n{screen}")
    qmp_command(stream, "screendump", {"filename": str(artifacts / f"{mode}-keyboard.ppm")})
    # Exercise scrolling and repeated EOIs with empty command lines.
    qmp_command(stream, "cont")
    for _ in range(28):
        send("ret")
    send("o")
    send("k")
    expect("\r\n> ok")
    stop_at_idle(stream)
    memory = dump_ram(stream, 0xB8000, 4000, artifacts / f"{mode}-scroll-vga.bin")
    if b"> ok" not in memory[:24*160:2] or not memory[24*160::2].startswith(b"uptime: "):
        raise RuntimeError("Keyboard scrolling damaged the prompt or timer status row")
    counts = struct.unpack("<16I", dump_ram(stream, symbols["irq_counts"], 64,
                                          artifacts / f"{mode}-irq-counts.bin"))
    if counts[0] < 100 or counts[1] < 50 or any(counts[2:]):
        raise RuntimeError(f"Unexpected hardware IRQ delivery: {counts}")
    dropped = struct.unpack("<I", dump_ram(stream, symbols["dropped"], 4,
                                         artifacts / f"{mode}-keyboard-dropped.bin"))[0]
    if dropped:
        raise RuntimeError(f"Keyboard dropped {dropped} characters")
    pic = qmp_command(stream, "human-monitor-command", {"command-line": "info pic"})
    (artifacts / f"{mode}-pic.txt").write_text(pic)
    if not re.search(r"pic0:.*imr=fc", pic) or not re.search(r"pic1:.*imr=ff", pic):
        raise RuntimeError(f"Unexpected PIC interrupt masks: {pic}")
    timer_test(stream, symbols, artifacts, mode)  # Timer still runs after keyboard traffic.


def shell_test(stream, symbols, artifacts, mode, serial):
    qmp_command(stream, "cont")
    _, expect, type_text = guest_keyboard(stream, serial)
    type_text("\b\b")  # Remove the keyboard test's unsubmitted 'ok'.
    type_text("help\n")
    expect("Commands:\r\n"
           "  help         Show this list.\r\n"
           "  clear        Clear the console.\r\n"
           "  about        About rum.\r\n"
           "  echo <text>  Print text.\r\n"
           "  ls           List RAM files.\r\n"
           "  cat <name>   Read a file.\r\n"
           "  write <name> [text]  Create or replace a file.\r\n"
           "  rm <name>    Remove a file.\r\n"
           "  mem          Show heap and file usage.\r\n"
           "  snake        Play ASCII Snake.\r\n> ")
    type_text("about\n")
    expect("rum OS v0.1.0\r\n"
           "An island of our own. A hobby kernel in C and x86 assembly.\r\n"
           "32-bit x86 | GRUB Multiboot | PIC, PIT and PS/2\r\n> ")
    type_text("echx\bo rum is alive!\n")
    expect("\r\nrum is alive!\r\n> ")
    type_text("   echo   two  spaces\n")
    expect("\r\ntwo  spaces\r\n> ")
    before = serial.read_bytes()
    type_text(" \t\n")
    expect("     \r\n> ")
    if b"Unknown command" in serial.read_bytes()[len(before):]:
        raise RuntimeError("Whitespace-only input dispatched a command")
    type_text("echo\n")
    expect("echo\r\n\r\n> ")
    type_text("nope\n")
    expect("Unknown command: nope. Type 'help'.\r\n> ")
    type_text("clear x\n")
    expect("Usage: clear\r\n> ")
    stop_at_idle(stream)
    memory = dump_ram(stream, 0xB8000, 4000, artifacts / f"{mode}-shell-errors-vga.bin")
    if b"Usage: clear" not in memory[:24*160:2]:
        raise RuntimeError("Command usage error missing from VGA")
    qmp_command(stream, "cont")
    type_text("clear\n")
    expect("clear\r\n\x1b[2J\x1b[H> ")
    stop_at_idle(stream)
    memory = dump_ram(stream, 0xB8000, 4000, artifacts / f"{mode}-clear-vga.bin")
    text = memory[:24*160:2]
    if text != b"> " + b" " * (24*80 - 2):
        raise RuntimeError("Clear did not reset the console and prompt")
    if not memory[24*160::2].startswith(b"uptime: "):
        raise RuntimeError("Clear erased the uptime row")
    # A clean demonstration also checks commands still run after clear.
    qmp_command(stream, "cont")
    type_text("help\nabout\necho rum has a shell!\n")
    expect("\r\nrum has a shell!\r\n> ")
    stop_at_idle(stream)
    memory = dump_ram(stream, 0xB8000, 4000, artifacts / f"{mode}-shell-vga.bin")
    rows = [memory[y*160:(y+1)*160:2].decode("ascii").rstrip() for y in range(25)]
    screen = "\n".join(rows)
    for text in ("Commands:", "echo <text>", "rum OS v0.1.0", "rum has a shell!", "uptime:"):
        if text not in screen:
            raise RuntimeError(f"Missing shell VGA text {text!r}")
    (artifacts / f"{mode}-shell-screen.txt").write_text(screen + "\n")
    qmp_command(stream, "screendump", {"filename": str(artifacts / f"{mode}-shell.ppm")})
    timer_test(stream, symbols, artifacts, mode)


FAULT_CASES = {
    "de": (0, 0, "Divide error"),
    "ud": (6, 0, "Invalid opcode"),
    "gp": (13, 0x18, "General protection fault"),
    "pf": (14, 0, "Page fault"),
}

PAGING_FAULTS = {
    "null": (0, 0, "paging_null_instruction"),
    "text": (3, "__text_start", "paging_text_instruction"),
    "rodata": (3, "paging_readonly_word", "paging_rodata_instruction"),
    "unmapped": (0, 0x40000000, "paging_unmapped_instruction"),
    "readonly": (3, 0x40000000, "paging_readonly_instruction"),
}


def paging_fault_test(symbols, case, serial_text, registers):
    fields = {name: int(value, 16) for name, value in re.findall(r"\b([a-z0-9]+)=0x([0-9a-f]{8})", serial_text)}
    error, address, instruction = PAGING_FAULTS[case]
    if isinstance(address, str): address = symbols[address]
    for name, expected in (("vector", 14), ("error", error), ("cr2", address), ("eip", symbols[instruction])):
        if fields.get(name) != expected:
            raise RuntimeError(f"Production paging fault has incorrect {name}: {fields.get(name)}, expected {expected:#x}")
    cr0 = re.search(r"\bCR0=([0-9a-fA-F]+)", registers)
    if not cr0 or int(cr0[1], 16) & 0x80010000 != 0x80010000:
        raise RuntimeError("Production paging fault lost PG/WP")
    eip = re.search(r"\bEIP=([0-9a-fA-F]+)", registers)
    if not eip or not symbols["cpu_halt"] <= int(eip[1], 16) < symbols["cpu_halt"] + 4:
        raise RuntimeError("Paging fault did not halt")


def fault_report_test(stream, symbols, artifacts, mode, fault, serial_text, screen, registers):
    fields = {name: int(value, 16) for name, value in re.findall(r"\b([a-z0-9]+)=0x([0-9a-f]{8})", serial_text)}
    vector, error, _ = FAULT_CASES[fault]
    expected = {"vector": vector, "error": error, "eip": symbols[f"fault_{fault}_instruction"],
                "cs": 8, "ds": 16, "es": 16, "fs": 16, "gs": 16,
                "eax": 0x11223344, "ebx": 0x55667788, "ecx": 0x99AABBCC, "edx": 0xDDEEFF00,
                "esi": 0x13579BDF, "edi": 0x2468ACE0, "ebp": 0x0BADF00D}
    if fault == "de":
        expected.update(ecx=0, edx=0)
    if fault == "gp":
        expected["eax"] = 0x11220018
    if fault == "pf":
        expected["cr2"] = 0x400000
        if "page not present, read, supervisor" not in screen:
            raise RuntimeError("Missing page fault explanation")
    saved = dump_ram(stream, symbols["fault_expected_esp"], 4, artifacts / f"{mode}-stack.bin")
    expected["esp"] = struct.unpack("<I", saved)[0]
    for name, value in expected.items():
        if fields.get(name) != value or f"{name}=0x{value:08x}" not in screen:
            raise RuntimeError(f"Wrong panic field {name}: expected {value:#x}, got {fields.get(name)}")
    if "eflags" not in fields or bool(fields["eflags"] & 0x400) != (fault == "ud"):
        raise RuntimeError("Interrupted direction flag was not preserved")
    eip = re.search(r"\bEIP=([0-9a-fA-F]+)", registers)
    if not eip or not symbols["cpu_halt"] <= int(eip[1], 16) < symbols["cpu_halt"] + 4:
        raise RuntimeError("Fault did not reach cpu_halt")


def storage_shell_test(stream, symbols, artifacts, mode, serial, project):
    qmp_command(stream, "cont")
    _, expect, type_text = guest_keyboard(stream, serial)
    assets = {path.name: path.read_bytes() for path in sorted((project / "assets/ramfs").iterdir())}
    start = len(serial.read_bytes())
    type_text("ls\n")
    expect("\r\n> ")
    for name in assets:
        if (name + "  ").encode() not in serial.read_bytes()[start:]:
            raise RuntimeError(f"Embedded file missing from ls output: {name}")
    if assets:
        name = "welcome.txt" if "welcome.txt" in assets else next(iter(assets))
        type_text(f"cat {name}\n")
        text = bytes(byte if byte in (10, 9) or 32 <= byte <= 126 else 46 for byte in assets[name])
        if not text.endswith(b"\n"): text += b"\n"
        expect(text.replace(b"\n", b"\r\n").decode() + "> ")
    # Make space for two temporary files even with a full embedded filesystem.
    for name in list(assets)[:max(0, len(assets) - 62)]:
        type_text(f"rm {name}\n")
        expect("\r\n> ")
        del assets[name]
    def unused_name(base):
        name, index = base, 0
        while name in assets:
            index += 1
            name = f"{base}-{index}"
        return name
    notes, empty = unused_name("notes.txt"), unused_name("empty")
    type_text(f"write {notes} hello from rum\ncat {notes}\n")
    expect("\r\nhello from rum\r\n> ")
    type_text(f"write {notes} changed\ncat /{notes}\n")
    expect("\r\nchanged\r\n> ")
    type_text(f"write {empty}\ncat {empty}\n")
    expect(f"cat {empty}\r\n\r\n> ")
    type_text(f"rm {notes}\ncat {notes}\n")
    expect(f"File not found: {notes}\r\n> ")
    type_text(f"rm {empty}\n")
    type_text("write bad/name no\n")
    expect("Cannot write file: invalid name, limit reached, or out of memory.\r\n> ")
    type_text("cat\nrm\nls x\nmem x\n")
    expect("Usage: mem\r\n> ")
    for expected in ("Usage: cat <name>", "Usage: rm <name>", "Usage: ls"):
        if expected.encode() not in serial.read_bytes():
            raise RuntimeError(f"Missing file command usage error: {expected}")
    # Leave a readable storage demonstration on the VGA console.
    type_text(f"clear\nls\nwrite {notes} hello from rum\ncat {notes}\nmem\n")
    expect("\r\n> ")
    count = len(assets) + 1
    if f"RAM files: {count} files, ".encode() not in serial.read_bytes()[-300:]:
        raise RuntimeError("Filesystem usage failed to count newly written file")
    stop_at_idle(stream)
    memory = dump_ram(stream, 0xB8000, 4000, artifacts / f"{mode}-storage-vga.bin")
    screen = "\n".join(memory[y*160:(y+1)*160:2].decode("ascii").rstrip() for y in range(25))
    for expected in ("hello from rum", "Heap:", f"RAM files: {count} files", "uptime:"):
        if expected not in screen:
            raise RuntimeError(f"Missing storage VGA text: {expected}")
    (artifacts / f"{mode}-storage-screen.txt").write_text(screen + "\n")
    qmp_command(stream, "screendump", {"filename": str(artifacts / f"{mode}-storage.ppm")})
    timer_test(stream, symbols, artifacts, mode)


def snake_test(stream, symbols, artifacts, mode, serial):
    qmp_command(stream, "cont")
    send, expect, type_text = guest_keyboard(stream, serial)
    type_text("snake x\n")
    expect("Usage: snake\r\n> ")
    type_text("write snake.score 0\n")
    expect("\r\n> ")
    stop_at_idle(stream)

    def allocations(label):
        mapped = struct.unpack("<I", dump_ram(stream, symbols["heap_mapped"], 4,
                               artifacts / f"{mode}-snake-heap-size.bin"))[0]
        heap = dump_virtual(stream, 0x40000000, mapped, artifacts / f"{mode}-snake-heap-{label}.bin")
        offset, count = 0, 0
        while offset < mapped:
            size = struct.unpack_from("<I", heap, offset)[0]
            if heap[offset + 12] == 0: count += 1
            end = offset + 16 + size
            if end > mapped or end <= offset: raise RuntimeError("Invalid game heap headers")
            offset = end
        return count

    def board():
        memory = dump_ram(stream, 0xB8000, 4000, artifacts / f"{mode}-snake-vga.bin")
        rows = [memory[y*160:(y+1)*160:2].decode("ascii") for y in range(25)]
        if not rows[24].startswith("uptime: "):
            raise RuntimeError("Snake overwrote uptime")
        if not all(rows[y][19:61] == "#" * 42 for y in (4, 21)):
            raise RuntimeError("Snake horizontal borders are broken")
        if not all(rows[y][19] == rows[y][60] == "#" for y in range(5, 21)):
            raise RuntimeError("Snake vertical borders are broken")
        grid = [row[20:60] for row in rows[5:21]]
        def locate(character):
            return [(x, y) for y, row in enumerate(grid) for x, cell in enumerate(row) if cell == character]
        heads, food, body = locate("@"), locate("*"), locate("o")
        match = re.search(r"score:\s+(\d+)\s+best:\s+(\d+)", rows[2])
        if not match or len(heads) != 1 or len(food) != 1:
            raise RuntimeError("Snake head/food/score is missing")
        score, best = map(int, match.groups())
        if len(body) + 1 != score + 3 or set(heads + body) & set(food):
            raise RuntimeError("Snake length or food placement is inconsistent")
        return heads[0], food[0], score, best, rows, memory

    baseline = allocations("before")
    qmp_command(stream, "cont")
    type_text("snake\np")
    expect("rum_snake_paused\r\n")
    stop_at_idle(stream)
    head, target, initial_score, _, rows, frozen = board()
    if "Paused." not in rows[22] or allocations("playing") != baseline + 1:
        raise RuntimeError("Snake pause or state allocation failed")
    timer_test(stream, symbols, artifacts, mode)
    stop_at_idle(stream)
    if board()[-1][:24*160] != frozen[:24*160]:
        raise RuntimeError("Paused Snake moved while PIT ticks advanced")

    def move(key, expected):
        qmp_command(stream, "cont")
        send(key)  # Queue a direction while paused, then take one real PIT step.
        send("p")
        time.sleep(0.11)
        send("p")
        expect("rum_snake_paused\r\n")
        stop_at_idle(stream)
        actual = board()[0]
        if actual != expected:
            raise RuntimeError(f"Snake keyboard/timer move mismatch: {actual}, expected {expected}")
        return actual

    # Approach food vertically first to avoid the initial body to the left.
    x, y = head
    if target[1] == y and target[0] < x:
        detour = -1 if y else 1
        x, y = move("w" if detour < 0 else "s", (x, y + detour))
        while x != target[0]: x, y = move("a", (x - 1, y))
        x, y = move("s" if detour < 0 else "w", target)
    else:
        while y != target[1]:
            delta = -1 if target[1] < y else 1
            x, y = move("w" if delta < 0 else "s", (x, y + delta))
        while x != target[0]:
            delta = -1 if target[0] < x else 1
            x, y = move("a" if delta < 0 else "d", (x + delta, y))
    _, _, score, best, rows, _ = board()
    if score != initial_score + 1 or best != score:
        raise RuntimeError("Eating food did not grow the snake/update best score")
    (artifacts / f"{mode}-snake-screen.txt").write_text("\n".join(row.rstrip() for row in rows) + "\n")
    qmp_command(stream, "screendump", {"filename": str(artifacts / f"{mode}-snake.ppm")})
    qmp_command(stream, "cont")
    send("q")
    expect("rum_snake_quit\r\n> ")
    type_text("cat snake.score\n")
    expect(f"cat snake.score\r\n{score}\r\n> ")
    stop_at_idle(stream)
    if allocations("after") != baseline:
        raise RuntimeError("Snake quit leaked a heap allocation")

    # A wall collision stays in the game; restart/quit must recover normally.
    qmp_command(stream, "cont")
    type_text("snake\n")
    deadline = time.monotonic() + 6
    while not serial.read_bytes().endswith(b"rum_snake_game_over\r\n"):
        if time.monotonic() > deadline: raise RuntimeError("Snake never collided with the wall")
        time.sleep(0.05)
    stop_at_idle(stream)
    if "Game over." not in board()[4][22]: raise RuntimeError("Game-over message missing")
    qmp_command(stream, "cont")
    send("r")
    send("p")
    expect("rum_snake_paused\r\n")
    stop_at_idle(stream)
    if board()[2] != 0: raise RuntimeError("Restart did not reset Snake score")
    qmp_command(stream, "cont")
    send("q")
    expect("rum_snake_quit\r\n> ")
    type_text("echo back on rum\n")
    expect("\r\nback on rum\r\n> ")
    stop_at_idle(stream)
    if allocations("restarted-quit") != baseline: raise RuntimeError("Restart/quit leaked allocations")
    timer_test(stream, symbols, artifacts, mode)


def boot_test(qemu, project, mode, artifacts, fault=None, irq_test=False, paging=None, ram=64, interactive=True, iso=False, storage=False):
    serial_artifact = artifacts / f"{mode}-serial.log"
    vga_dump = artifacts / f"{mode}-vga.bin"
    screenshot = artifacts / f"{mode}.ppm"
    image = project / ("build/tests/storage.elf" if storage else f"build/tests/paging-{paging}.elf" if paging else "build/tests/irq.elf" if irq_test else
                       f"build/tests/fault-{fault}.elf" if fault else "build/rum.elf")
    symbols = elf_symbols(image)
    boot_layout_test(symbols)
    marker = ("rum_storage_test_ok" if storage else "rum_paging_test_ok" if paging == "ok" else "rum_panic_halted" if paging else
              "rum_irq_test_ok" if irq_test else "rum_panic_halted" if fault else "rum_boot_ok")
    normal = not fault and not irq_test and not paging and not storage
    with tempfile.TemporaryDirectory(prefix="rum-qmp-") as temporary:
        # Keep the live writer/readers on one filesystem. DrvFs can return
        # ENODATA when a WSL guest creates/truncates a log on the Windows drive.
        serial = Path(temporary) / "serial.log"
        serial.write_text("")
        monitor = str(Path(temporary) / "qmp.sock")
        # Windows QEMU uses a loopback TCP monitor; Linux can use a Unix socket.
        port = None
        if os.name == "nt":
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
                reservation.bind(("127.0.0.1", 0))
                port = reservation.getsockname()[1]
            monitor_spec = f"tcp:127.0.0.1:{port},server=on,wait=off"
        else:
            monitor_spec = f"unix:{monitor},server=on,wait=off"
        image_args = (["-boot", "d", "-cdrom", str(project / "build/rum.iso")]
                      if mode == "iso" or iso else ["-kernel", str(image)])
        args = [qemu, "-machine", "pc", "-accel", "tcg", "-m", f"{ram}M", "-display", "none",
                "-serial", f"file:{serial}", "-qmp", monitor_spec,
                "-no-reboot", "-no-shutdown", *image_args]
        process = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            deadline = time.monotonic() + 20
            while marker not in serial.read_text(errors="replace"):
                if any(marker in serial.read_text(errors="replace") for marker in ("rum_paging_test_failed", "rum_storage_test_failed")):
                    raise RuntimeError(serial.read_text(errors="replace"))
                if process.poll() is not None:
                    raise RuntimeError(f"QEMU exited: {process.stderr.read().decode(errors='replace')}")
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"Boot timed out ({mode}). Serial output:\n{serial.read_text(errors='replace')}")
                time.sleep(0.1)
            family = socket.AF_INET if os.name == "nt" else socket.AF_UNIX
            with socket.socket(family, socket.SOCK_STREAM) as connection:
                connection.settimeout(5)
                connection.connect(("127.0.0.1", port) if os.name == "nt" else monitor)
                with connection.makefile("rwb") as stream:
                    greeting = json.loads(stream.readline())
                    if "QMP" not in greeting:
                        raise RuntimeError("Invalid QMP greeting")
                    qmp_command(stream, "qmp_capabilities")
                    status = qmp_command(stream, "query-status")
                    if not status["running"]:
                        raise RuntimeError(f"Guest unexpectedly stopped: {status}")
                    if normal:
                        stop_at_idle(stream)
                    else:
                        qmp_command(stream, "stop")
                    registers = cpu_tables_test(stream, symbols, artifacts, mode, live=normal)
                    if normal:
                        physical_memory_test(stream, symbols, artifacts, mode, registers, serial.read_text(), project)
                        timer_test(stream, symbols, artifacts, mode)
                    memory = dump_ram(stream, 0xB8000, 4000, vga_dump)
                    qmp_command(stream, "screendump", {"filename": str(screenshot)})
                    rows = [memory[y * 160:(y + 1) * 160:2].decode("ascii", errors="replace").rstrip()
                            for y in range(25)]
                    screen = "\n".join(rows)
                    (artifacts / f"{mode}-screen.txt").write_text(screen + "\n")
                    expected_text = (("rum heap and RAM filesystem tests passed.",) if storage else
                                     ("rum physical allocator and paging tests passed.",) if paging == "ok" else
                                     ("rum kernel panic", "Page fault", "CPU halted.") if paging else
                                     ("rum IRQ return and spurious interrupt tests passed.",) if irq_test else
                                     ("rum kernel panic", FAULT_CASES[fault][2], "CPU halted.") if fault
                                     else ("rum OS v0.1.0", "Hello, kernel world!", "[ok] Multiboot handoff",
                                           "[ok] Kernel GDT and segments", "[ok] IDT and CPU exception handlers",
                                           "[ok] PIT timer at 100 Hz", "[ok] PS/2 keyboard (US layout)",
                                           "[ok] Multiboot memory map", "[ok] Physical page allocator",
                                           "[ok] Paging (4 KiB pages, null guard)",
                                           "[ok] Kernel heap (16-byte alignment)",
                                           "[ok] RAM filesystem and embedded files",
                                           "uptime:", "Close QEMU to return"))
                    for expected in expected_text:
                        if expected not in screen:
                            raise RuntimeError(f"Missing VGA text {expected!r} ({mode})")
                    if paging and paging != "ok":
                        paging_fault_test(symbols, paging, serial.read_text(), registers)
                    elif fault:
                        fault_report_test(stream, symbols, artifacts, mode, fault,
                                          serial.read_text(), screen, registers)
                    elif normal and interactive:
                        keyboard_test(stream, symbols, artifacts, mode, serial)
                        shell_test(stream, symbols, artifacts, mode, serial)
                        storage_shell_test(stream, symbols, artifacts, mode, serial, project)
                        snake_test(stream, symbols, artifacts, mode, serial)
                    qmp_command(stream, "quit")
            print(f"PASS: {mode}, GDT/IDT/segments, " +
                  ("production heap/RAM files, alignment, reuse, realloc, limits, physical OOM rollback" if storage else
                   "production paging, aliases/remap, frame accounting, init/runtime OOM recovery" if paging == "ok"
                   else "production page tables, real #PF, error/EIP/CR2, PG/WP, panic/halt" if paging
                   else "real exception, saved registers, error code, EIP, stack, VGA/serial panic, halt" if fault
                   else "IRQ return, saved registers/flags, spurious IRQ7/IRQ15" if irq_test
                   else f"{ram} MiB RAM, Multiboot/physical/paging/heap/embedded files, PIT/PS2/PIC/shell/Snake"))
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            serial_artifact.write_bytes(serial.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    artifacts = project / "build/test-artifacts"
    artifacts.mkdir(parents=True, exist_ok=True)
    for mode in ("iso", "elf"):
        boot_test(args.qemu, project, mode, artifacts)
    for fault in FAULT_CASES:
        boot_test(args.qemu, project, f"fault-{fault}", artifacts, fault=fault)
    boot_test(args.qemu, project, "irq", artifacts, irq_test=True)
    boot_test(args.qemu, project, "paging-ok", artifacts, paging="ok")
    for case in PAGING_FAULTS:
        boot_test(args.qemu, project, f"paging-{case}", artifacts, paging=case)
    for ram in (16, 64):
        boot_test(args.qemu, project, f"storage-{ram}", artifacts, storage=True, ram=ram)
    for ram in (16, 256, 1152):
        boot_test(args.qemu, project, f"ram-{ram}", artifacts, ram=ram, interactive=False)
    boot_test(args.qemu, project, "iso-ram-16", artifacts, ram=16, interactive=False, iso=True)


if __name__ == "__main__":
    main()
