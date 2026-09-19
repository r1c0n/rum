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
    heap_index, stack_index = 256, 511
    populated = set(range(table_count)) | {heap_index, stack_index}
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
    idle = struct.unpack("<I", dump_ram(stream, symbols["task_idle_stack_base"], 4,
                                         artifacts / f"{mode}-idle-stack-base.bin"))[0]
    emergency = struct.unpack("<I", dump_ram(stream, symbols["task_emergency_stack_base"], 4,
                                              artifacts / f"{mode}-emergency-stack-base.bin"))[0]
    if idle != 0x7FC01000 or emergency != 0x7FC56000:
        raise RuntimeError("Invalid guarded idle/emergency stack slots")
    stack_table = struct.unpack("<1024I", dump_ram(stream, directory[stack_index] & 0xFFFFF000, 4096,
                                                   artifacts / f"{mode}-stack-table.bin"))
    if any(stack_table[((base >> 12) & 1023) - 1] for base in (idle, emergency)):
        raise RuntimeError("Idle or emergency stack guard is mapped")
    stack_frames = []
    stack_addresses = [*range(idle, idle + 16384, 4096),
                       *range(emergency, emergency + 16384, 4096)]
    for address in stack_addresses:
        entry = stack_table[(address >> 12) & 1023]
        frame = entry & 0xFFFFF000
        if entry & ~0x60 & 0xFFF != 3:
            raise RuntimeError("Idle stack is not supervisor/writable")
        if frame in frames or frame // 4096 not in pages:
            raise RuntimeError("Idle stack overlaps another owner or reserved RAM")
        frames.add(frame)
        stack_frames.append(frame)
    if any(entry for slot, entry in enumerate(stack_table)
           if entry and slot not in {(address >> 12) & 1023 for address in stack_addresses}):
        raise RuntimeError("Normal boot owns an unexpected kernel stack slot")
    for frame in frames: owned[frame // 4096 // 8] |= 1 << (frame // 4096 % 8)
    if allocated != owned or free != managed_count - len(frames):
        raise RuntimeError("Page table/heap/task stack physical frame accounting incorrect")
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
              "heap_pages": len(heap_frames), "idle_stack_pages": 4,
              "emergency_stack_pages": len(stack_frames) - 4,
              "heap_used_bytes": used, "files": file_count}
    (artifacts / f"{mode}-memory.json").write_text(json.dumps(report, indent=2) + "\n")


def user_address_space_test(stream, symbols, artifacts, mode, serial_text):
    """Walk the live page tables and physical ledger left by the kernel fixture."""
    if "rum_user_memory_ok" not in serial_text or "rum_paging_spaces_ok" not in serial_text:
        raise RuntimeError("Missing user-address-space kernel checks")

    def symbol_bytes(name, length):
        return dump_ram(stream, symbols[name], length, artifacts / f"{mode}-{name}.bin")

    ready = struct.unpack("<I", symbol_bytes("paging_user_test_ready", 4))[0]
    directories = struct.unpack("<2I", symbol_bytes("paging_user_test_directories", 8))
    program = struct.unpack("<I", symbol_bytes("paging_user_test_program", 4))[0]
    stack = struct.unpack("<I", symbol_bytes("paging_user_test_stack", 4))[0]
    stats = struct.unpack("<7I", symbol_bytes("paging_user_test_stats", 28))
    physical_stats = struct.unpack("<4I", symbol_bytes("paging_user_test_physical", 16))
    spaces, directory_pages, shared_pages, kernel_cr3, active_cr3, private_pages, user_pages = stats
    usable, managed_count, free_count, limit = physical_stats
    if (ready != 1 or spaces != 3 or directory_pages != 3 or private_pages != 6 or
            user_pages != 36 or active_cr3 != kernel_cr3 or
            len(set((*directories, kernel_cr3))) != 3):
        raise RuntimeError(f"Bad exported user-space ownership state: {stats}, {directories}")
    registers = qmp_command(stream, "human-monitor-command", {"command-line": "info registers"})
    hardware_cr3 = re.search(r"\bCR3=([0-9a-fA-F]+)", registers)
    if not hardware_cr3 or int(hardware_cr3[1], 16) != kernel_cr3:
        raise RuntimeError("User-space fixture did not return to the kernel CR3")

    bitmap_bytes = (1 << 30) // 4096 // 8
    managed = dump_ram(stream, symbols["managed"], bitmap_bytes,
                       artifacts / f"{mode}-user-managed.bin")
    allocated = dump_ram(stream, symbols["allocated"], bitmap_bytes,
                         artifacts / f"{mode}-user-allocated.bin")

    def bitmap_has(bitmap, frame):
        page = frame // 4096
        return frame % 4096 == 0 and page < len(bitmap) * 8 and bool(bitmap[page // 8] & (1 << (page % 8)))

    owners = {}

    def claim(frame, owner):
        if frame >= limit or not bitmap_has(managed, frame):
            raise RuntimeError(f"{owner} uses unmanaged frame {frame:#x}")
        if frame in owners:
            raise RuntimeError(f"Physical frame {frame:#x} is owned by both {owners[frame]} and {owner}")
        owners[frame] = owner

    def read_pages(frames, label):
        frames = sorted(set(frames))
        result = {}
        start = index = 0
        while start < len(frames):
            end = start + 1
            while end < len(frames) and frames[end] == frames[end - 1] + 4096:
                end += 1
            first, count = frames[start], end - start
            raw = dump_ram(stream, first, count * 4096,
                           artifacts / f"{mode}-{label}-{index}.bin")
            for offset, frame in enumerate(frames[start:end]):
                result[frame] = raw[offset * 4096:(offset + 1) * 4096]
            start, index = end, index + 1
        return result

    directories_data = read_pages((kernel_cr3, *directories), "user-directories")
    kernel = struct.unpack("<1024I", directories_data[kernel_cr3])
    if any(kernel[512:]):
        raise RuntimeError("Kernel directory contains a private/user mapping")
    shared_indices = [index for index, entry in enumerate(kernel[:512]) if entry & 1]
    if len(shared_indices) != shared_pages:
        raise RuntimeError("Shared table statistics disagree with the kernel directory")
    claim(kernel_cr3, "kernel directory")
    for number, frame in enumerate(directories):
        claim(frame, f"user directory {number}")

    shared_frames = []
    for index in shared_indices:
        entry = kernel[index]
        if entry & ~0x60 & 0xFFF != 3:
            raise RuntimeError(f"Kernel PDE {index} is not supervisor/writable: {entry:#x}")
        frame = entry & 0xFFFFF000
        claim(frame, f"shared table {index}")
        shared_frames.append(frame)
    shared_data = read_pages(shared_frames, "user-shared-tables")
    shared_tables = {}
    for index, frame in zip(shared_indices, shared_frames):
        table = struct.unpack("<1024I", shared_data[frame])
        if any(entry & 5 == 5 for entry in table):
            raise RuntimeError(f"Shared kernel table {index} exposes user-accessible pages")
        shared_tables[index] = table
    if not shared_tables.get(0) or shared_tables[0][0]:
        raise RuntimeError("Page zero is mapped in the shared kernel half")

    expected_addresses = {program, program + 4096, *(stack + offset for offset in range(0, 65536, 4096))}
    expected_indices = {address >> 22 for address in expected_addresses}
    mappings = []
    private_table_frames = []
    for number, directory_frame in enumerate(directories):
        entries = struct.unpack("<1024I", directories_data[directory_frame])
        for index in range(512):
            if (entries[index] & ~0x60) != (kernel[index] & ~0x60):
                raise RuntimeError(f"User directory {number} does not borrow kernel PDE {index}")
        populated = {index for index, entry in enumerate(entries[512:768], 512) if entry}
        if populated != expected_indices or any(entries[768:]):
            raise RuntimeError(f"Unexpected private PDEs in user directory {number}: {populated}")
        tables = {}
        for index in sorted(populated):
            entry = entries[index]
            if entry & ~0x60 & 0xFFF != 7:
                raise RuntimeError(f"Private PDE {index} has wrong permissions: {entry:#x}")
            frame = entry & 0xFFFFF000
            claim(frame, f"user {number} table {index}")
            private_table_frames.append(frame)
            tables[index] = frame
        mappings.append((entries, tables))

    if len(private_table_frames) != private_pages:
        raise RuntimeError("Private page-table statistics disagree with the live directories")
    private_data = read_pages(private_table_frames, "user-private-tables")
    virtual_maps = []
    user_frames = []
    for number, (_, tables) in enumerate(mappings):
        virtual_map = {}
        for index, frame in tables.items():
            table = struct.unpack("<1024I", private_data[frame])
            for slot, entry in enumerate(table):
                address = (index << 22) | (slot << 12)
                if address not in expected_addresses:
                    if entry:
                        raise RuntimeError(f"Unexpected user PTE at {address:#x} in space {number}")
                    continue
                if entry & ~0x60 & 0xFFF != 7:
                    raise RuntimeError(f"User PTE at {address:#x} has wrong permissions: {entry:#x}")
                physical = entry & 0xFFFFF000
                claim(physical, f"user {number} data {address:#x}")
                user_frames.append(physical)
                virtual_map[address] = physical
        if set(virtual_map) != expected_addresses:
            raise RuntimeError(f"User space {number} is missing expected private pages")
        virtual_maps.append(virtual_map)
    if len(user_frames) != user_pages or any(virtual_maps[0][address] == virtual_maps[1][address]
                                             for address in expected_addresses):
        raise RuntimeError("Same-address user mappings share frames or disagree with statistics")

    heap_mapped = struct.unpack("<I", dump_ram(stream, symbols["heap_mapped"], 4,
                                                artifacts / f"{mode}-user-heap-size.bin"))[0]
    if not heap_mapped or heap_mapped % 4096 or heap_mapped > 4 * 1024 * 1024 or 256 not in shared_tables:
        raise RuntimeError("Invalid heap mapping in user-space fixture")
    heap_table = shared_tables[256]
    heap_pages = heap_mapped // 4096
    for slot, entry in enumerate(heap_table):
        if slot >= heap_pages:
            if entry:
                raise RuntimeError("Heap table maps beyond committed heap pages")
            continue
        if entry & ~0x60 & 0xFFF != 3:
            raise RuntimeError(f"Heap PTE {slot} is not supervisor/writable")
        claim(entry & 0xFFFFF000, f"heap data {slot}")

    claimed = bytearray(bitmap_bytes)
    for frame in owners:
        page = frame // 4096
        claimed[page // 8] |= 1 << (page % 8)
    if allocated != claimed or free_count != managed_count - len(owners):
        raise RuntimeError("User spaces leaked, aliased, or omitted an allocated physical frame")
    if managed_count != sum(byte.bit_count() for byte in managed) or usable < managed_count:
        raise RuntimeError("Exported PMM statistics disagree with its managed bitmap")

    user_data = read_pages(user_frames, "user-data")

    def user_bytes(space, address, pages):
        return b"".join(user_data[virtual_maps[space][address + offset * 4096]]
                        for offset in range(pages))

    first_program = bytearray(8192)
    first_program[64:68] = b"rum!"
    cross_offset = 4096 - 17
    first_program[cross_offset:cross_offset + 96] = bytes(range(1, 97))
    first_program[4094:4101] = b"island\0"
    second_program = bytearray(8192)
    second_program[cross_offset:cross_offset + 96] = b"\xA5" * 96
    first_stack = bytearray(65536)
    first_stack[-1] = 0x5A
    if (user_bytes(0, program, 2) != first_program or
            user_bytes(1, program, 2) != second_program or
            user_bytes(0, stack, 16) != first_stack or
            any(user_bytes(1, stack, 16))):
        raise RuntimeError("User data isolation, zero-fill, padding, or stack contents are incorrect")

    report = {"spaces": spaces, "shared_table_pages": shared_pages,
              "private_table_pages": private_pages, "user_pages": user_pages,
              "heap_pages": heap_pages, "owned_physical_pages": len(owners),
              "free_physical_pages": free_count,
              "program_address": f"{program:#010x}", "stack_address": f"{stack:#010x}"}
    (artifacts / f"{mode}-user-address-spaces.json").write_text(json.dumps(report, indent=2) + "\n")


def cpu_tables_test(stream, symbols, artifacts, mode, live=False, user_abi=False, double_fault=False):
    registers = qmp_command(stream, "human-monitor-command", {"command-line": "info registers"})
    (artifacts / f"{mode}-cpu.txt").write_text(registers)
    for name, expected_base, expected_limit in (("GDT", symbols["rum_gdt"], 55),
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
    cr0 = re.search(r"\bCR0=([0-9a-fA-F]+)", registers)
    cr4 = re.search(r"\bCR4=([0-9a-fA-F]+)", registers)
    if not cr0 or not cr4 or int(cr0[1], 16) & 0xE != 0xE or int(cr4[1], 16) & 0x40600:
        raise RuntimeError("Integer-only CPU policy is not enforced")
    gdt = dump_ram(stream, symbols["rum_gdt"], 56, artifacts / f"{mode}-gdt.bin")
    if gdt[:40] != struct.pack("<QQQQQ", 0, 0x00CF9B000000FFFF, 0x00CF93000000FFFF,
                              0x00CFFB000000FFFF, 0x00CFF3000000FFFF):
        raise RuntimeError("Unexpected GDT descriptors")
    base = symbols["rum_tss"]
    expected_tss = struct.pack("<HHBBBB", 103, base & 0xFFFF, (base >> 16) & 0xFF,
                               0x8B, 0, base >> 24)
    double_base = symbols["rum_double_fault_tss"]
    expected_double = struct.pack("<HHBBBB", 103, double_base & 0xFFFF, (double_base >> 16) & 0xFF,
                                  0x8B if double_fault else 0x89, 0, double_base >> 24)
    if gdt[40:48] != expected_tss or gdt[48:] != expected_double:
        raise RuntimeError("Wrong TSS descriptor or missing hardware busy flag")
    task_register = re.search(r"\bTR\s*=\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", registers)
    expected_register = (0x30, double_base, 103) if double_fault else (0x28, base, 103)
    if not task_register or tuple(int(value, 16) for value in task_register.groups()) != expected_register:
        raise RuntimeError(f"Wrong task register: {registers}")
    tss = dump_ram(stream, base, 104, artifacts / f"{mode}-tss.bin")
    stack_top = symbols.get("cpu_test_stack_top", symbols["__boot_stack_top"])
    current_top = struct.unpack("<I", dump_ram(stream, symbols["task_current_stack_top"], 4,
                                                artifacts / f"{mode}-current-stack-top.bin"))[0]
    if current_top:
        stack_top = current_top
        if live:
            idle_base = struct.unpack("<I", dump_ram(stream, symbols["task_idle_stack_base"], 4,
                                                       artifacts / f"{mode}-idle-base.bin"))[0]
            if stack_top not in (symbols["__boot_stack_top"], idle_base + 16384):
                raise RuntimeError("Normal boot is on an unowned kernel stack")
            esp = re.search(r"\bESP=([0-9a-fA-F]+)", registers)
            if not esp or not stack_top - 16384 <= int(esp[1], 16) < stack_top:
                raise RuntimeError("CPU ESP is outside the current task's stack")
    if (struct.unpack_from("<I", tss, 4)[0] != stack_top
            or struct.unpack_from("<H", tss, 8)[0] != 0x10
            or struct.unpack_from("<H", tss, 102)[0] != 104):
        raise RuntimeError("Wrong TSS kernel stack or user I/O policy")
    idt = dump_ram(stream, symbols["idt"], 2048, artifacts / f"{mode}-idt.bin")
    for vector in range(48):
        low, selector, reserved, attributes, high = struct.unpack_from("<HHBBH", idt, vector * 8)
        if vector == 8:
            if (low, selector, reserved, attributes, high) != (0, 0x30, 0, 0x85, 0):
                raise RuntimeError("Wrong double-fault task gate")
            continue
        target = f"exception_{vector}" if vector < 32 else f"irq_{vector - 32}"
        if ((high << 16 | low) != symbols[target]
                or (selector, reserved, attributes) != (8, 0, 0x8E)):
            raise RuntimeError(f"Wrong IDT gate {vector}")
    low, selector, reserved, attributes, high = struct.unpack_from("<HHBBH", idt, 128 * 8)
    syscall_target = symbols["abi_test_syscall"] if user_abi else symbols["syscall_entry"]
    if ((high << 16 | low) != syscall_target or
            (selector, reserved, attributes) != (8, 0, 0xEE)):
        raise RuntimeError("Wrong ring-3 syscall gate")
    if any(idt[48 * 8:128 * 8]) or any(idt[129 * 8:]):
        raise RuntimeError("Unexpected IDT gates above the PIC range")
    return registers


def user_abi_memory_test(stream, symbols, artifacts, mode, registers, project, case):
    controls = {name: int(value, 16) for name, value in re.findall(r"\b(CR[034])=([0-9a-fA-F]+)", registers)}
    if controls.get("CR3") != symbols["abi_directory"] or controls.get("CR0", 0) & 0x80010000 != 0x80010000:
        raise RuntimeError("Wrong fixture user paging controls")
    directory = struct.unpack("<1024I", dump_ram(stream, symbols["abi_directory"], 4096,
                                                 artifacts / f"{mode}-directory.bin"))
    expected = {0: symbols["abi_kernel_table"] | 3, 512: symbols["abi_program_table"] | 7,
                767: symbols["abi_stack_table"] | 7}
    if any(entry & ~0x60 != expected.get(i, 0) for i, entry in enumerate(directory)):
        raise RuntimeError("Wrong fixture kernel/user directory separation")
    kernel = struct.unpack("<1024I", dump_ram(stream, symbols["abi_kernel_table"], 4096,
                                              artifacts / f"{mode}-kernel-table.bin"))
    if any(entry & ~0x60 != (i * 4096 | 3 if i else 0) for i, entry in enumerate(kernel)):
        raise RuntimeError("Fixture kernel mapping exposes user access or page zero")
    program = struct.unpack("<1024I", dump_ram(stream, symbols["abi_program_table"], 4096,
                                               artifacts / f"{mode}-program-table.bin"))
    stack = struct.unpack("<1024I", dump_ram(stream, symbols["abi_stack_table"], 4096,
                                             artifacts / f"{mode}-stack-table.bin"))
    if any(entry & ~0x60 != (symbols["abi_stack_pages"] + (i - 1008) * 4096 | 7 if i >= 1008 else 0)
           for i, entry in enumerate(stack)):
        raise RuntimeError("Wrong fixture stack pages or missing guard")
    image = (project / ("build/user/ramfs/hello.elf" if case == "hello" else "build/tests/user/abi-probe.elf")).read_bytes()
    phoff, phnum = struct.unpack_from("<I", image, 28)[0], struct.unpack_from("<H", image, 44)[0]
    mappings = {}
    for i in range(phnum):
        kind, offset, address, _, files, memory, flags, _ = struct.unpack_from("<8I", image, phoff + 32 * i)
        if kind != 1 or not memory:
            continue
        start = (address - 0x80000000) // 4096
        end = (address - 0x80000000 + memory + 4095) // 4096
        for slot in range(start, end):
            mappings[slot] = symbols["abi_program_pages"] + slot * 4096 | (7 if flags & 2 else 5)
        if not flags & 2:
            loaded = dump_ram(stream, symbols["abi_program_pages"] + address - 0x80000000, files,
                              artifacts / f"{mode}-segment-{i}.bin")
            if loaded != image[offset:offset + files]:
                raise RuntimeError("Executed user code/constants differ from separate ELF asset")
    if any(entry & ~0x60 != mappings.get(i, 0) for i, entry in enumerate(program)):
        raise RuntimeError("Wrong fixture user segment permissions or frames")


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
           "  diag         Show task and memory diagnostics.\r\n"
           "  snake        Play ASCII Snake.\r\n> ")
    type_text("about\n")
    expect("rum OS v0.2.0\r\n"
           "An island of our own. A hobby kernel in C and x86 assembly.\r\n"
           "32-bit x86 | GRUB Multiboot | PIC, PIT and PS/2\r\n> ")
    start = len(serial.read_bytes())
    type_text("diag x\ndiag\n")
    expect("borrowed\r\n> ")
    report = serial.read_bytes()[start:].decode()
    if "Usage: diag" not in report or "Task: 1 | CR3:" not in report or \
            "Tasks: 2 live, 4 owned stack pages, 4 emergency stack pages, 0 owned directories" not in report or \
            "Processes: 0 | 0 user tables, 0 user pages" not in report:
        raise RuntimeError(f"Bad shell diagnostics: {report}")
    cr3 = re.search(r"Task: 1 \| CR3: 0x([0-9a-f]{8})", report)
    esp0 = re.search(r"TSS.ESP0: 0x([0-9a-f]{8})", report)
    kesp = re.search(r"Kernel ESP: 0x([0-9a-f]{8})", report)
    if not cr3 or not esp0 or not kesp or int(esp0[1], 16) != symbols["__boot_stack_top"] or \
            not symbols["__boot_stack_bottom"] <= int(kesp[1], 16) < symbols["__boot_stack_top"]:
        raise RuntimeError("Shell diagnostics lost the boot task/TSS/stack")
    stop_at_idle(stream)
    registers = qmp_command(stream, "human-monitor-command", {"command-line": "info registers"})
    actual = re.search(r"\bCR3=([0-9a-fA-F]+)", registers)
    if not actual or int(actual[1], 16) != int(cr3[1], 16):
        raise RuntimeError("Shell diagnostics disagree with hardware CR3")
    qmp_command(stream, "cont")
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
    for text in ("Commands:", "echo <text>", "rum OS v0.2.0", "rum has a shell!", "uptime:"):
        if text not in screen:
            raise RuntimeError(f"Missing shell VGA text {text!r}")
    (artifacts / f"{mode}-shell-screen.txt").write_text(screen + "\n")
    qmp_command(stream, "screendump", {"filename": str(artifacts / f"{mode}-shell.ppm")})
    timer_test(stream, symbols, artifacts, mode)


FAULT_CASES = {
    "de": (0, 0, "Divide error"),
    "ud": (6, 0, "Invalid opcode"),
    "gp": (13, 0x30, "General protection fault"),
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
    if case in ("null", "text", "rodata") and "rum_paging_fault_space_ok" not in serial_text:
        raise RuntimeError("Protected-page fault did not run under a child CR3")
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
        expected["eax"] = 0x11220030
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


def panic_diagnostics_test(stream, symbols, artifacts, mode, serial_text, registers, owned=False):
    fields = {name: int(value, 16) for name, value in
              re.findall(r"\bdiag_([a-z0-9]+)=0x([0-9a-f]{8})", serial_text)}
    controls = {name: int(value, 16) for name, value in re.findall(r"\b(CR[03])=([0-9a-fA-F]+)", registers)}
    tss = dump_ram(stream, symbols["rum_tss"], 104, artifacts / f"{mode}-panic-tss.bin")
    if fields.get("cr3") != controls.get("CR3") or fields.get("esp0") != struct.unpack_from("<I", tss, 4)[0]:
        raise RuntimeError("Panic diagnostics disagree with hardware CR3/TSS")
    if not owned:
        return
    raw = dump_ram(stream, symbols["task_fault_expected"], 48, artifacts / f"{mode}-expected-owners.bin")
    names = ("task", "cr3", "esp0", "base", "top", "free", "managed", "dirs", "tables", "stacks", "heapalloc", "heapused")
    expected = dict(zip(names, struct.unpack("<12I", raw)))
    for name, value in expected.items():
        if fields.get(name) != value:
            raise RuntimeError(f"Panic changed owned resource {name}: {fields.get(name)} != {value}")
    if not expected["base"] <= fields["kesp"] < expected["top"] or \
            expected["top"] != expected["esp0"] or expected["top"] - expected["base"] != 16384 or \
            fields["recordcr3"] != fields["cr3"] or fields["activecr3"] != fields["cr3"]:
        raise RuntimeError("Worker panic lost the active stack, task record or registered CR3")
    for name, value in {"task": 3, "live": 3, "stacks": 8, "emergencystacks": 4,
                        "owneddirs": 1, "dirs": 2,
                        "created": 1, "exited": 0, "reaped": 0, "switches": 1}.items():
        if fields.get(name) != value:
            raise RuntimeError(f"Unexpected worker ownership/lifecycle {name}: {fields.get(name)}")
    allocated = dump_ram(stream, symbols["allocated"], 32768, artifacts / f"{mode}-panic-allocated.bin")
    managed = dump_ram(stream, symbols["managed"], 32768, artifacts / f"{mode}-panic-managed.bin")
    kernel_cr3 = fields["kernelcr3"]
    directory = struct.unpack("<1024I", dump_ram(stream, fields["cr3"], 4096, artifacts / f"{mode}-worker-directory.bin"))
    kernel = struct.unpack("<1024I", dump_ram(stream, kernel_cr3, 4096, artifacts / f"{mode}-kernel-directory.bin"))
    if fields["cr3"] == kernel_cr3 or any(directory[512:]) or any(kernel[512:]) or \
            any((left & ~0x60) != (right & ~0x60) for left, right in zip(directory, kernel)):
        raise RuntimeError("Worker directory lost private ownership or shared supervisor mappings")
    tables = {entry & 0xFFFFF000 for entry in kernel if entry & 1}
    if len(tables) != fields["tables"] or any(entry & ~0x60 & 0xFFF != 3 for entry in kernel if entry & 1):
        raise RuntimeError("Shared table count/permissions disagree with diagnostics")
    frames = {fields["cr3"], kernel_cr3, *tables}
    if len(frames) != len(tables) + 2:
        raise RuntimeError("Private/shared page structures overlap")
    heap = struct.unpack("<1024I", dump_ram(stream, kernel[256] & 0xFFFFF000, 4096,
                                           artifacts / f"{mode}-panic-heap-table.bin"))
    heap_frames = [entry & 0xFFFFF000 for entry in heap if entry & 1]
    if any(entry & ~0x60 & 0xFFF != 3 for entry in heap if entry & 1):
        raise RuntimeError("Panic heap mappings lost supervisor/write permissions")
    idle = struct.unpack("<I", dump_ram(stream, symbols["task_idle_stack_base"], 4,
                                        artifacts / f"{mode}-panic-idle-stack.bin"))[0]
    emergency = struct.unpack("<I", dump_ram(stream, symbols["task_emergency_stack_base"], 4,
                                              artifacts / f"{mode}-panic-emergency-stack.bin"))[0]
    stack_table = struct.unpack("<1024I", dump_ram(stream, directory[511] & 0xFFFFF000, 4096,
                                                   artifacts / f"{mode}-panic-stack-table.bin"))
    stack_virtual = [*range(idle, idle + 16384, 4096),
                     *range(emergency, emergency + 16384, 4096),
                     *range(expected["base"], expected["top"], 4096)]
    if any(stack_table[((base >> 12) & 1023) - 1]
           for base in (idle, emergency, expected["base"])):
        raise RuntimeError("Panic stack guard is mapped")
    stack_frames = []
    for address in stack_virtual:
        entry = stack_table[(address >> 12) & 1023]
        if entry & ~0x60 & 0xFFF != 3:
            raise RuntimeError("Panic stack mapping is not supervisor/writable")
        stack_frames.append(entry & 0xFFFFF000)
    private = [*heap_frames, *stack_frames]
    for frame in private:
        if frame in frames:
            raise RuntimeError("Panic owners overlap")
        frames.add(frame)
    claimed = bytearray(32768)
    for frame in frames:
        page = frame // 4096
        if frame % 4096 or not managed[page // 8] & (1 << (page % 8)):
            raise RuntimeError("Panic owner uses an unmanaged/unaligned frame")
        claimed[page // 8] |= 1 << (page % 8)
    if allocated != claimed or fields["free"] != fields["managed"] - len(frames):
        raise RuntimeError("Panic physical ledger leaked or freed an owned frame")
    # Inspect real PTEs under the worker's CR3, including mutable CPU storage
    # and protected kernel sections. All these resources stay supervisor-only.
    protected = [(address, True) for address in frames]
    protected += [(symbols[name], True) for name in ("rum_gdt", "rum_tss", "idt", "__boot_stack_bottom")]
    protected += [(symbols[name], False) for name in ("__text_start", "__rodata_start")]
    cached = {}
    for address, writable in protected:
        index = address >> 22
        if index not in cached:
            cached[index] = struct.unpack("<1024I", dump_ram(stream, directory[index] & 0xFFFFF000, 4096,
                                                           artifacts / f"{mode}-identity-table-{index}.bin"))
        entry = cached[index][(address >> 12) & 1023]
        if entry & ~0x60 != (address & 0xFFFFF000) | (3 if writable else 1):
            raise RuntimeError(f"Worker resource mapping has incorrect permissions: {address:#x}, {entry:#x}")
    (artifacts / f"{mode}-panic-ownership.json").write_text(json.dumps(fields, indent=2) + "\n")


def double_fault_test(stream, symbols, artifacts, mode, serial_text, registers):
    if "rum_double_fault_test_ready" not in serial_text or "rum_double_fault_entry" not in serial_text:
        raise RuntimeError("Guarded stack overflow did not reach the double-fault task")
    fields = {name: int(value, 16) for name, value in
              re.findall(r"\b(?:diag_)?([a-z0-9]+)=0x([0-9a-f]{8})", serial_text)}
    raw = dump_ram(stream, symbols["task_double_fault_expected"], 40,
                   artifacts / f"{mode}-expected.bin")
    names = ("task", "slot", "base", "top", "emergency", "kernelcr3",
             "workercr3", "free", "stacks", "emergencystacks")
    expected = dict(zip(names, struct.unpack("<10I", raw)))
    for name, value in {"vector": 8, "error": 0, "eip": symbols["task_stack_fault_instruction"],
                        "esp": expected["base"], "task": expected["task"],
                        "recordcr3": expected["workercr3"], "activecr3": expected["workercr3"],
                        "kernelcr3": expected["kernelcr3"], "stacks": expected["stacks"],
                        "emergencystacks": expected["emergencystacks"]}.items():
        if fields.get(name) != value:
            raise RuntimeError(f"Double-fault report has wrong {name}: {fields.get(name)} != {value}")
    if fields.get("cr3") != expected["kernelcr3"] or not expected["emergency"] <= fields.get("kesp", 0) < expected["emergency"] + 16384:
        raise RuntimeError("Double fault did not run on the emergency stack and kernel CR3")

    controls = {name: int(value, 16) for name, value in re.findall(r"\b(CR[03])=([0-9a-fA-F]+)", registers)}
    esp = re.search(r"\bESP=([0-9a-fA-F]+)", registers)
    eip = re.search(r"\bEIP=([0-9a-fA-F]+)", registers)
    if (controls.get("CR3") != expected["kernelcr3"] or not esp or
            not expected["emergency"] <= int(esp[1], 16) < expected["emergency"] + 16384 or
            not eip or not symbols["cpu_halt"] <= int(eip[1], 16) < symbols["cpu_halt"] + 4):
        raise RuntimeError("Hardware did not halt on the double-fault emergency context")
    tss = dump_ram(stream, symbols["rum_tss"], 104, artifacts / f"{mode}-saved-tss.bin")
    saved_cr3, saved_eip, saved_esp = (struct.unpack_from("<I", tss, offset)[0]
                                      for offset in (28, 32, 56))
    if (saved_cr3, saved_eip, saved_esp) != (expected["workercr3"],
                                             symbols["task_stack_fault_instruction"], expected["base"]):
        raise RuntimeError("Hardware TSS did not preserve the failed worker context")

    directory = struct.unpack("<1024I", dump_ram(stream, expected["kernelcr3"], 4096,
                                                  artifacts / f"{mode}-kernel-directory.bin"))
    table = struct.unpack("<1024I", dump_ram(stream, directory[511] & 0xFFFFF000, 4096,
                                              artifacts / f"{mode}-stack-table.bin"))
    for base in (0x7FC01000, expected["base"], expected["emergency"]):
        if table[((base >> 12) & 1023) - 1]:
            raise RuntimeError("Double-fault fixture mapped a kernel stack guard")
        for address in range(base, base + 16384, 4096):
            if table[(address >> 12) & 1023] & ~0x60 & 0xFFF != 3:
                raise RuntimeError("Double-fault stack page is not supervisor/writable")
    report = {**expected, "saved_eip": f"{saved_eip:#010x}",
              "hardware_cr3": f"{controls['CR3']:#010x}"}
    (artifacts / f"{mode}-double-fault.json").write_text(json.dumps(report, indent=2) + "\n")


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


def boot_test(qemu, project, mode, artifacts, fault=None, irq_test=False, paging=None, ram=64, interactive=True, iso=False, storage=False, cpu=None, tasks=False, user_abi=None, task_fault=False, double_fault=False, process_fault=None):
    if task_fault:
        fault = "ud"
    serial_artifact = artifacts / f"{mode}-serial.log"
    vga_dump = artifacts / f"{mode}-vga.bin"
    screenshot = artifacts / f"{mode}.ppm"
    image = project / (f"build/tests/process-fault-{process_fault}.elf" if process_fault else "build/tests/task-double-fault.elf" if double_fault else "build/tests/task-fault.elf" if task_fault else f"build/tests/abi-{user_abi}.elf" if user_abi else "build/tests/task.elf" if tasks else f"build/tests/cpu-{cpu}.elf" if cpu else "build/tests/storage.elf" if storage else f"build/tests/paging-{paging}.elf" if paging else "build/tests/irq.elf" if irq_test else
                       f"build/tests/fault-{fault}.elf" if fault else "build/rum.elf")
    symbols = elf_symbols(image)
    boot_layout_test(symbols)
    marker = ("rum_process_irq_ready" if process_fault == "irq" else "rum_process_fault_test_ok" if process_fault else "rum_panic_halted" if double_fault else "rum_abi_test_ok" if user_abi else "rum_task_test_ok" if tasks else "rum_cpu_test_ok" if cpu else "rum_storage_test_ok" if storage else "rum_paging_test_ok" if paging == "ok" else "rum_panic_halted" if paging else
              "rum_irq_test_ok" if irq_test else "rum_panic_halted" if fault else "rum_boot_ok")
    normal = not process_fault and not fault and not irq_test and not paging and not storage and not cpu and not tasks and not user_abi and not double_fault
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
                if any(marker in serial.read_text(errors="replace") for marker in ("rum_process_fault_test_failed", "rum_paging_test_failed", "rum_storage_test_failed", "rum_cpu_test_failed", "rum_task_test_failed", "rum_abi_test_failed")):
                    raise RuntimeError(serial.read_text(errors="replace"))
                if process.poll() is not None:
                    raise RuntimeError(f"QEMU exited: {process.stderr.read().decode(errors='replace')}")
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"Boot timed out ({mode}). Serial output:\n{serial.read_text(errors='replace')}")
                time.sleep(0.1)
            family = socket.AF_INET if os.name == "nt" else socket.AF_UNIX
            with socket.socket(family, socket.SOCK_STREAM) as connection:
                # Dumps and screenshots write to the host filesystem; DrvFs
                # can take longer than an ordinary monitor/register request.
                connection.settimeout(20)
                connection.connect(("127.0.0.1", port) if os.name == "nt" else monitor)
                with connection.makefile("rwb") as stream:
                    greeting = json.loads(stream.readline())
                    if "QMP" not in greeting:
                        raise RuntimeError("Invalid QMP greeting")
                    qmp_command(stream, "qmp_capabilities")
                    status = qmp_command(stream, "query-status")
                    if not status["running"]:
                        raise RuntimeError(f"Guest unexpectedly stopped: {status}")
                    if process_fault == "irq":
                        send, _, _ = guest_keyboard(stream, serial)
                        send("x")
                        deadline = time.monotonic() + 5
                        while "rum_process_fault_test_ok" not in serial.read_text(errors="replace"):
                            if "rum_process_fault_test_failed" in serial.read_text(errors="replace"):
                                raise RuntimeError(serial.read_text(errors="replace"))
                            if time.monotonic() >= deadline:
                                raise RuntimeError(f"User IRQ return timed out ({mode}). Serial output:\n{serial.read_text(errors='replace')}")
                            time.sleep(0.02)
                    if normal:
                        stop_at_idle(stream)
                    else:
                        qmp_command(stream, "stop")
                    registers = cpu_tables_test(stream, symbols, artifacts, mode, live=normal,
                                                user_abi=bool(user_abi), double_fault=double_fault)
                    if user_abi:
                        user_abi_memory_test(stream, symbols, artifacts, mode, registers, project, user_abi)
                    if normal:
                        physical_memory_test(stream, symbols, artifacts, mode, registers, serial.read_text(), project)
                        timer_test(stream, symbols, artifacts, mode)
                    memory = dump_ram(stream, 0xB8000, 4000, vga_dump)
                    qmp_command(stream, "screendump", {"filename": str(screenshot)})
                    rows = [memory[y * 160:(y + 1) * 160:2].decode("ascii", errors="replace").rstrip()
                            for y in range(25)]
                    screen = "\n".join(rows)
                    (artifacts / f"{mode}-screen.txt").write_text(screen + "\n")
                    expected_text = (("rum user fault recovery tests passed.",) if process_fault else
                                     ("rum kernel panic", "Double fault", "CPU halted.") if double_fault else
                                     ("rum user ABI and startup tests passed.",) if user_abi else
                                     ("rum kernel contexts and waiting tests passed.",) if tasks else
                                     ("rum user CPU entry and policy tests passed.",) if cpu else
                                     ("rum heap and RAM filesystem tests passed.",) if storage else
                                     ("rum physical allocator and paging tests passed.",) if paging == "ok" else
                                     ("rum kernel panic", "Page fault", "CPU halted.") if paging else
                                     ("rum IRQ return and spurious interrupt tests passed.",) if irq_test else
                                     ("rum kernel panic", FAULT_CASES[fault][2], "CPU halted.") if fault
                                     else ("rum OS v0.2.0", "Hello, kernel world!", "[ok] Multiboot handoff",
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
                    if double_fault:
                        double_fault_test(stream, symbols, artifacts, mode, serial.read_text(), registers)
                    elif paging and paging != "ok":
                        paging_fault_test(symbols, paging, serial.read_text(), registers)
                        panic_diagnostics_test(stream, symbols, artifacts, mode, serial.read_text(), registers)
                    elif paging == "ok":
                        if "rum_paging_spaces_ok" not in serial.read_text():
                            raise RuntimeError("Missing production paging-context checks")
                        user_address_space_test(stream, symbols, artifacts, mode, serial.read_text())
                    elif fault:
                        fault_report_test(stream, symbols, artifacts, mode, fault,
                                          serial.read_text(), screen, registers)
                        panic_diagnostics_test(stream, symbols, artifacts, mode, serial.read_text(), registers, owned=task_fault)
                    elif normal and interactive:
                        keyboard_test(stream, symbols, artifacts, mode, serial)
                        shell_test(stream, symbols, artifacts, mode, serial)
                        storage_shell_test(stream, symbols, artifacts, mode, serial, project)
                        snake_test(stream, symbols, artifacts, mode, serial)
                    qmp_command(stream, "quit")
            print(f"PASS: {mode}, GDT/IDT/segments, " +
                  ("production ring-3 process entry, isolated fault recovery, parent/CR3/TSS restore, cleanup" if process_fault else
                   "hardware task gate, independent guarded stack, saved failed TSS, controlled panic" if double_fault else
                   "separate user ELF, real ring-3 startup/int 0x80, arguments/BSS/return, segment permissions" if user_abi else
                   "guarded stacks, atomic process records, exit status, context/CR3 switches, waiting/IRQ cleanup" if tasks else
                   "real ring-3 PIT/IRET, TSS stack, user registers/segments/DF, alignment, integer/I/O policy" if cpu else
                   "production heap/RAM files, alignment, reuse, realloc, limits, physical OOM rollback" if storage else
                   "owned user pages, checked copies, isolation, rollback, page-table/PMM ledger" if paging == "ok"
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
    for case in ("irq", "x87", "mmx", "sse", "io"):
        boot_test(args.qemu, project, f"cpu-{case}", artifacts, cpu=case)
    for case in ("null", "kernel", "readonly", "ud2", "privileged", "io", "irq"):
        boot_test(args.qemu, project, f"process-fault-{case}", artifacts, process_fault=case)
    for ram in (16, 64):
        boot_test(args.qemu, project, f"tasks-{ram}", artifacts, tasks=True, ram=ram)
        boot_test(args.qemu, project, f"task-fault-{ram}", artifacts, task_fault=True, ram=ram)
        boot_test(args.qemu, project, f"double-fault-{ram}", artifacts, double_fault=True, ram=ram)
    for case in ("args", "limits", "hello"):
        boot_test(args.qemu, project, f"abi-{case}", artifacts, user_abi=case)
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
