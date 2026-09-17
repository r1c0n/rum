#!/usr/bin/env python3
"""Boot the actual GRUB ISO and ELF, then check serial output and VGA RAM."""
import argparse
import json
import os
from pathlib import Path
import socket
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


def boot_test(qemu, project, mode, artifacts):
    serial = artifacts / f"{mode}-serial.log"
    vga_dump = artifacts / f"{mode}-vga.bin"
    screenshot = artifacts / f"{mode}.ppm"
    serial.write_text("")
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
                      if mode == "iso" else ["-kernel", str(project / "build/rum.elf")])
        args = [qemu, "-machine", "pc", "-accel", "tcg", "-m", "64M", "-display", "none",
                "-serial", f"file:{serial}", "-qmp", monitor_spec,
                "-no-reboot", "-no-shutdown", *image_args]
        process = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            deadline = time.monotonic() + 20
            while "rum_boot_ok" not in serial.read_text(errors="replace"):
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
                    response = qmp_command(stream, "human-monitor-command", {
                        "command-line": f'pmemsave 0xb8000 4000 "{vga_dump.as_posix()}"'
                    })
                    if response.strip():
                        raise RuntimeError(f"VGA memory dump failed: {response}")
                    qmp_command(stream, "screendump", {"filename": str(screenshot)})
                    memory = vga_dump.read_bytes()
                    if len(memory) != 4000:
                        raise RuntimeError("Incomplete VGA dump")
                    rows = [memory[y * 160:(y + 1) * 160:2].decode("ascii", errors="replace").rstrip()
                            for y in range(25)]
                    screen = "\n".join(rows)
                    (artifacts / f"{mode}-screen.txt").write_text(screen + "\n")
                    for expected in ("rum", "rum OS v0.1.0", "Hello, kernel world!",
                                     "[ok] Multiboot handoff", "CPU idle."):
                        if expected not in screen:
                            raise RuntimeError(f"Missing VGA text {expected!r} ({mode})")
                    qmp_command(stream, "quit")
            print(f"PASS: {mode} boot, Multiboot handoff, serial log, and VGA output")
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


if __name__ == "__main__":
    main()
