#!/usr/bin/env python3
"""Drive concurrent shell commands and completion notification through PS/2."""
import argparse
import importlib.util
from pathlib import Path
import re
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("tools_shell", ROOT / "tests/userspace-shell-test.py")
shell = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shell)
shell.ARTIFACTS = ROOT / "build/test-artifacts/process-tools"


class Console(shell.Console):
    def command(self, text, expected=None):
        start = self.serial.stat().st_size
        self.type(text + "\n")
        deadline = time.monotonic() + 20
        while True:
            output = self.serial.read_bytes()[start:].decode(errors="replace")
            if text + "\r\n" in output and output.endswith("> "):
                break
            assert "rum kernel panic" not in output and time.monotonic() < deadline, (text, output)
            time.sleep(0.02)
        output = re.sub(r"(?:/[^\r\n> ]*)?> $", "> ", output)
        if expected is not None:
            assert expected in output, (text, expected, output)
        return output


def launch(console, text):
    output = console.command("start " + text)
    match = re.search(r"\[(\d+)\] started ", output)
    assert match, output
    return int(match[1])


def listing(console):
    output = console.command("ps", "PID  PARENT  STATE  NAME")
    assert "0x" not in output
    return {int(pid): (int(parent), state, name) for pid, parent, state, name in
            re.findall(r"^(\d+)  (\d+)  (\w+)  ([^\r\n]+)", output, re.M)}


def exercise(console):
    first, second = launch(console, "spin"), launch(console, "spin")
    rows = listing(console)
    assert rows[first][2] == rows[second][2] == "spin.elf", rows
    parent = rows[first][0]
    assert parent == rows[second][0] and rows[parent][2] == "shell.elf", rows
    console.command("echo still responsive", "still responsive\r\n")
    console.command(f"kill {first}", f"Cancellation requested for {first}.")
    rows = listing(console)
    assert first not in rows and second in rows, rows
    console.command(f"kill {parent}", "access denied.")
    console.command("kill 2147483647", "process does not exist.")
    for invalid in ("0", "-1", "2147483648", "99999999999999999", "abc"):
        console.command("kill " + invalid, "invalid path, name or arguments.")
    for invalid in ("-1", "86400001", "4294967296", "abc"):
        console.command("sleep " + invalid, "invalid path, name or arguments.")
    console.command("sleep 0")
    console.type("sleep 86400000\n"); time.sleep(0.15); console.send("ctrl", "c")
    console.wait("/> ")
    # A completion must arrive with no keyboard activity, and redraw a partial
    # line intact rather than losing bytes or making it a kernel command.
    sleeper = launch(console, "sleep 1500")
    console.type("echo preserved")
    console.wait(f"[{sleeper}] sleep: exited with status 0.\r\n/> echo preserved", timeout=8)
    console.type("\n"); console.wait("preserved\r\n/> ")
    fault = launch(console, "fault")
    deadline = time.monotonic() + 5
    while f"[{fault}] fault: faulted." not in console.serial.read_text():
        assert time.monotonic() < deadline; time.sleep(0.02)
    console.wait("/> ")
    rapid = [launch(console, "hello") for _ in range(6)]
    console.command("echo drained", "drained\r\n")
    deadline = time.monotonic() + 5
    while any(f"[{pid}] hello: exited with status 0." not in console.serial.read_text() for pid in rapid):
        assert time.monotonic() < deadline; time.sleep(0.02)
    assert all(console.serial.read_text().count(f"[{pid}] hello: exited with status 0.") == 1 for pid in rapid)
    rows = listing(console)
    assert len(rows) == 3 and parent in rows and second in rows, rows
    console.command("start absent", "file or directory not found.")
    console.command("start", "Usage: start")
    # Fill background capacity. One slot must remain for foreground control
    # tools, so exhaustion never strands the user without a working kill.
    console.command(f"kill {second}", f"Cancellation requested for {second}.")
    started = []
    for _ in range(20):
        output = console.command("start spin")
        match = re.search(r"\[(\d+)\] started ", output)
        if not match:
            assert "not enough memory." in output, output
            break
        started.append(int(match[1]))
    assert len(started) >= 10
    rows = listing(console)
    assert all(pid in rows for pid in started), rows
    console.send("ctrl", "c"); console.wait("^C\r\n/> ")
    console.command(f"kill {started[0]}", "Cancellation requested")
    assert started[0] not in listing(console)
    replacement = launch(console, "spin")
    assert replacement > started[-1]
    console.command("exit")
    console.wait("/> ")
    assert len(listing(console)) == 2, "old session children survived restart"
    console.command("echo restarted", "restarted\r\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    shell.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    for iso in (False, True):
        for ram in ((64,) if iso else (16,)):
            with shell.boot(args.qemu, ram, f"tools-{'iso' if iso else 'elf'}-{ram}m", iso=iso) as console:
                exercise(Console(console.stream, console.serial))
            print(f"PASS: process tools, {'iso' if iso else 'elf'}, {ram} MiB: concurrent launch, listing, scoped kill, sleep and notifications", flush=True)


if __name__ == "__main__":
    main()
