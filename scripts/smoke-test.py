#!/usr/bin/env python3
"""Check normal boots, CPU tables, and real exceptions in isolated test ELFs."""
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


def cpu_tables_test(stream, symbols, artifacts, mode):
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
    if not flags or int(flags[1], 16) & 0x600:
        raise RuntimeError("Device interrupts were enabled or the C direction flag was set")
    gdt = dump_ram(stream, symbols["rum_gdt"], 24, artifacts / f"{mode}-gdt.bin")
    if gdt != struct.pack("<QQQ", 0, 0x00CF9B000000FFFF, 0x00CF93000000FFFF):
        raise RuntimeError("Unexpected GDT descriptors")
    idt = dump_ram(stream, symbols["idt"], 2048, artifacts / f"{mode}-idt.bin")
    for vector in range(32):
        low, selector, reserved, attributes, high = struct.unpack_from("<HHBBH", idt, vector * 8)
        if ((high << 16 | low) != symbols[f"exception_{vector}"]
                or (selector, reserved, attributes) != (8, 0, 0x8E)):
            raise RuntimeError(f"Wrong IDT gate {vector}")
    if any(idt[32 * 8:]):
        raise RuntimeError("Unexpected device gates before the IRQ milestone")
    return registers


FAULT_CASES = {
    "de": (0, 0, "Divide error"),
    "ud": (6, 0, "Invalid opcode"),
    "gp": (13, 0x18, "General protection fault"),
    "pf": (14, 0, "Page fault"),
}


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


def boot_test(qemu, project, mode, artifacts, fault=None):
    serial = artifacts / f"{mode}-serial.log"
    vga_dump = artifacts / f"{mode}-vga.bin"
    screenshot = artifacts / f"{mode}.ppm"
    serial.write_text("")
    image = project / (f"build/tests/fault-{fault}.elf" if fault else "build/rum.elf")
    symbols = elf_symbols(image)
    marker = "rum_panic_halted" if fault else "rum_boot_ok"
    with tempfile.TemporaryDirectory(prefix="rum-qmp-") as temporary:
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
                      if mode == "iso" else ["-kernel", str(image)])
        args = [qemu, "-machine", "pc", "-accel", "tcg", "-m", "64M", "-display", "none",
                "-serial", f"file:{serial}", "-qmp", monitor_spec,
                "-no-reboot", "-no-shutdown", *image_args]
        process = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            deadline = time.monotonic() + 20
            while marker not in serial.read_text(errors="replace"):
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
                    qmp_command(stream, "stop")
                    registers = cpu_tables_test(stream, symbols, artifacts, mode)
                    memory = dump_ram(stream, 0xB8000, 4000, vga_dump)
                    qmp_command(stream, "screendump", {"filename": str(screenshot)})
                    rows = [memory[y * 160:(y + 1) * 160:2].decode("ascii", errors="replace").rstrip()
                            for y in range(25)]
                    screen = "\n".join(rows)
                    (artifacts / f"{mode}-screen.txt").write_text(screen + "\n")
                    expected_text = (("rum kernel panic", FAULT_CASES[fault][2], "CPU halted.") if fault
                                     else ("rum OS v0.1.0", "Hello, kernel world!", "[ok] Multiboot handoff",
                                           "[ok] Kernel GDT and segments", "[ok] IDT and CPU exception handlers", "CPU idle."))
                    for expected in expected_text:
                        if expected not in screen:
                            raise RuntimeError(f"Missing VGA text {expected!r} ({mode})")
                    if fault:
                        fault_report_test(stream, symbols, artifacts, mode, fault,
                                          serial.read_text(), screen, registers)
                    qmp_command(stream, "quit")
            print(f"PASS: {mode}, GDT/IDT/segments, " +
                  ("real exception, saved registers, error code, EIP, stack, VGA/serial panic, halt" if fault
                   else "Multiboot handoff, serial log, and VGA output"))
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


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


if __name__ == "__main__":
    main()
