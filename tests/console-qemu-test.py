#!/usr/bin/env python3
"""Exercise console sessions through real PS/2 input and the normal supervisor."""
import argparse
from contextlib import contextmanager
import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("console_shell", ROOT / "tests/userspace-shell-test.py")
shell = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shell)
ARTIFACTS = ROOT / "build/test-artifacts/console-sessions"


@contextmanager
def boot(qemu, case, kind, ram):
    label = f"{case}-{kind}-{ram}m"
    with tempfile.TemporaryDirectory(prefix="rum-console-") as temporary:
        serial, monitor = Path(temporary) / "serial", Path(temporary) / "qmp"
        if os.name == "nt":
            family = socket.AF_INET
            with socket.socket(family, socket.SOCK_STREAM) as reservation:
                reservation.bind(("127.0.0.1", 0))
                address = reservation.getsockname()
            qmp = f"tcp:127.0.0.1:{address[1]},server=on,wait=off"
        else:
            family, address = socket.AF_UNIX, str(monitor)
            qmp = f"unix:{monitor},server=on,wait=off"
        if kind == "iso":
            image = "console" if case == "ownership" else "console-supervisor"
            arguments = ["-boot", "d", "-cdrom", str(ROOT / f"build/tests/{image}.iso")]
        elif case == "ownership":
            arguments = ["-kernel", str(ROOT / "build/tests/console.elf")]
        else:
            arguments = ["-kernel", str(ROOT / "build/rum.elf"), "-append", "rum.shell=/rum/console.elf"]
        if case == "supervision":
            disk = Path(temporary) / "system.raw"
            shell.images.build(disk)
            shell.images.seed_system(disk, {"CONSOLE.ELF": ROOT / "build/tests/user/console.elf"})
            # Use the production IDE layout rather than QEMU's secondary CD-ROM.
            image_path = ROOT / ("build/tests/console-supervisor.iso" if kind == "iso" else "build/rum.elf")
            arguments = shell.launcher.boot_arguments(image_path, kernel=kind == "elf", disk=disk)
            if kind == "elf": arguments += ["-append", "rum.shell=/rum/console.elf"]
        process = subprocess.Popen([qemu, "-machine", "pc", "-accel", "tcg", "-m", f"{ram}M", "-display", "none",
            "-serial", f"file:{serial}", "-qmp", qmp, "-no-reboot", "-no-shutdown", *arguments],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            deadline = time.monotonic() + 10
            while True:
                connection = socket.socket(family, socket.SOCK_STREAM)
                try:
                    connection.connect(address)
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    connection.close()
                    assert time.monotonic() < deadline, f"{label}: no QMP connection"
                    time.sleep(0.02)
            with connection:
                connection.settimeout(10)
                with connection.makefile("rwb") as stream:
                    assert "QMP" in json.loads(stream.readline())
                    shell.smoke.qmp_command(stream, "qmp_capabilities")
                    yield shell.Console(stream, serial), process
                    shell.smoke.qmp_command(stream, "quit")
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.kill(); process.wait(timeout=5)
            if serial.exists():
                (ARTIFACTS / f"{label}.serial.log").write_bytes(serial.read_bytes())
            (ARTIFACTS / f"{label}.qemu.log").write_bytes(process.stderr.read())


def ownership(console, process):
    actions = [
        ("rum_console_compete_ready", lambda: console.send("x")),
        ("rum_console_shell_control_ready", lambda: console.send("ctrl", "c")),
        ("rum_console_fault_input_ready", lambda: console.type("xy")),
        ("rum_console_nested_ready", lambda: console.send("ctrl", "c")),
        ("rum_console_parent_resumed", lambda: console.send("p")),
        ("rum_console_load_failure_ready", lambda: console.send("f")),
        ("rum_console_constructor_ready", lambda: console.send("ctrl", "c")),
        ("rum_console_io_ready", lambda: console.send("ctrl", "c")),
    ]
    sent = 0
    deadline = time.monotonic() + 90
    while True:
        text = console.serial.read_text(errors="replace") if console.serial.exists() else ""
        if "rum_console_ok" in text:
            assert sent == len(actions), "not every keyboard case ran"
            for name in "ab":
                line = f"chunk-{name}:" + name.upper() * 55
                assert text.splitlines().count(line) == 16, "a serialized output chunk was lost or interleaved"
            return
        if "_failed:" in text or "rum kernel panic" in text or process.poll() is not None or time.monotonic() > deadline:
            raise AssertionError(text[-7000:])
        if sent < len(actions) and actions[sent][0] in text:
            actions[sent][1](); sent += 1
        time.sleep(0.02)


def supervision(console, process):
    deadline = time.monotonic() + 45
    while True:
        text = console.serial.read_text(errors="replace") if console.serial.exists() else ""
        if "The shell stopped repeatedly." in text and text.endswith("> "):
            break
        if "rum kernel panic" in text or process.poll() is not None or time.monotonic() > deadline:
            raise AssertionError(text[-7000:])
        time.sleep(0.02)
    assert text.count("rum_console_orphan_ready") == 3, "shell did not leave a live child in each generation"
    assert text.count("Restarting the shell.") == 2, "restart limit changed"
    console.command("echo console restored", "console restored\r\n> ")
    diagnostic = console.command("diag")
    assert "Tasks: 2 live" in diagnostic and "Processes: 0" in diagnostic, diagnostic
    assert "Sessions: 0 live, 0 closing, 0 members | 0 session refs, 0 process refs" in diagnostic, diagnostic
    console.command("cat /readme.txt", "rum")
    console.type("snake\n"); console.wait("rum_snake_started\r\n")
    console.send("q"); console.wait("rum_snake_quit\r\n> ")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    for case, exercise in (("ownership", ownership), ("supervision", supervision)):
        for kind, ram in (("elf", 16), ("iso", 64)):
            with boot(args.qemu, case, kind, ram) as (console, process):
                exercise(console, process)
            print(f"PASS: console {case}, {kind}, {ram} MiB: ownership, cancellation and reference cleanup", flush=True)


if __name__ == "__main__":
    main()
