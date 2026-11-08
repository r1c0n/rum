#!/usr/bin/env python3
"""Validate public terminal isolation and play the actual user Snake via PS/2 input."""
import argparse
from collections import deque
import importlib.util
from pathlib import Path
import re
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("terminal_shell", ROOT / "tests/userspace-shell-test.py")
shell = importlib.util.module_from_spec(spec); spec.loader.exec_module(shell)
shell.ARTIFACTS = ROOT / "build/test-artifacts/terminal"
images = shell.images


def contains(console, marker, start=0, timeout=15):
    deadline = time.monotonic() + timeout
    while marker.encode() not in console.serial.read_bytes()[start:]:
        assert time.monotonic() < deadline, console.serial.read_bytes()[-3000:]
        time.sleep(0.01)


def screen(console, path):
    data = shell.smoke.dump_ram(console.stream, 0xB8000, 4000, path)
    text = data[::2].decode("ascii", errors="replace")
    return data, [text[y * 80:(y + 1) * 80] for y in range(25)]


def wait_screen(console, path, predicate, timeout=5):
    deadline = time.monotonic() + timeout
    while True:
        data, rows = screen(console, path)
        if predicate(rows): return data, rows
        assert time.monotonic() < deadline, "\n".join(rows)
        time.sleep(0.01)


def board(rows):
    cells = {(x, y): rows[5 + y][20 + x] for y in range(16) for x in range(40)}
    head = next(pos for pos, c in cells.items() if c == "@")
    food = next(pos for pos, c in cells.items() if c == "*")
    blocked = {pos for pos, c in cells.items() if c == "o"}
    return head, food, blocked


def play(console, path):
    console.type("snake\n"); contains(console, "rum_snake_started\r\n")
    console.send("p"); contains(console, "rum_snake_paused\r\n")
    _, rows = wait_screen(console, path, lambda r: "Paused." in r[22])
    head, food, blocked = board(rows)
    pending = deque([(head, [])]); visited = {head}
    route = None
    while pending:
        position, steps = pending.popleft()
        if position == food: route = steps; break
        for key, dx, dy in (("w", 0, -1), ("d", 1, 0), ("s", 0, 1), ("a", -1, 0)):
            target = position[0] + dx, position[1] + dy
            if not steps and key == "a": continue  # Initial movement is right.
            if 0 <= target[0] < 40 and 0 <= target[1] < 16 and target not in blocked and target not in visited:
                visited.add(target); pending.append((target, steps + [(key, target)]))
    assert route
    shell.smoke.qmp_command(console.stream, "screendump", {"filename": str(shell.ARTIFACTS / "snake-paused.ppm")})
    console.send(route[0][0]); console.send("p")
    for i, (key, target) in enumerate(route):
        if i: console.send(key)
        wait_screen(console, path, lambda r: r[5 + target[1]][20 + target[0]] == "@", timeout=3)
    contains(console, "snake: score=1\r\n")
    console.send("q"); console.wait("rum_snake_quit\r\n/> ")


def run(qemu, disk, ram, iso, path):
    with shell.boot(qemu, ram, f"terminal-{ram}m", disk=disk, iso=iso) as console:
        for mode in ("validate", "nested", "exit", "fault", "partial"):
            expected = "terminal ABI validated" if mode == "validate" else "nested terminal restored" if mode == "nested" else \
                "Program stopped after a user fault." if mode == "fault" else "Program exited with status 1." if mode == "partial" else None
            console.command("term " + mode, expected)
            console.command("term defaults", "terminal defaults restored")
        console.type("term readsafe\n"); console.wait("terminal input ready\r\n")
        console.type("az"); console.wait("terminal input validated\r\n/> ")
        console.type("term display\n"); contains(console, "terminal display ready\r\n")
        data, rows = wait_screen(console, path, lambda r: r[0].startswith("TERMINAL") and r[23][79] == "@")
        assert data[1] == 0x0A and data[(23 * 80 + 79) * 2 + 1] == 0x1E
        assert "uptime:" in rows[24], rows[24]
        console.send("q"); console.wait("/> ")
        for _ in range(3):
            console.type("term cancel\n"); console.wait("terminal cancellation ready\r\n")
            console.send("ctrl", "c"); console.wait("Program cancelled.\r\n/> ")
            console.command("term defaults", "terminal defaults restored")
        console.command("start term denied", "started term.")
        contains(console, "background terminal denied\r\n")
        console.command("echo input intact", "input intact\r\n")
        console.command("snake > REDIRECT.TXT", "access denied.")
        # A CPU-bound program keeps running while the protected user game moves.
        output = console.command("start spin", "started spin.")
        spin = int(re.search(r"\[(\d+)\] started spin", output)[1])
        play(console, path)
        console.command("term defaults", "terminal defaults restored")
        console.command(f"kill {spin}", "Cancellation requested")
        console.type("snake\n"); contains(console, "rum_snake_started\r\n", console.serial.stat().st_size - 64)
        console.send("r"); contains(console, "rum_snake_restarted\r\n")
        console.type("wasdwasd"); console.send("ctrl", "c"); console.wait("Program cancelled.\r\n/> ")
        console.command("term defaults", "terminal defaults restored")
        console.command("recovery", "Kernel recovery shell.")
        diag = console.command("diag")
        for expected in ("Terminal: normal", "Processes: 0 | 0 user tables, 0 user pages",
                         "Jobs: 0 live, 0 running, 0 completed, 0 active waits",
                         "Handles: 0 descriptors, 0 open descriptions, 0 aliases"):
            assert expected in diag, diag
    assert images.file_bytes(disk, "/SNAKE.DAT") == b"1\n"
    assert images.file_bytes(disk, "/REDIRECT.TXT") == b""
    images.audit(disk)
    with shell.boot(qemu, ram, f"snake-reboot-{ram}m", disk=disk, iso=iso) as console:
        console.type("snake\n"); contains(console, "rum_snake_started\r\n")
        console.send("p"); contains(console, "rum_snake_paused\r\n")
        _, rows = wait_screen(console, path, lambda r: "Paused." in r[22])
        assert rows[2][42] == "1", rows[2]
        console.send("q"); console.wait("rum_snake_quit\r\n/> ")
    print(f"PASS: {ram} MiB terminal: invalid packets, input/canvas guards, fault/cancel restoration, user Snake under CPU load and rebooted score", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args(); shell.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rum-terminal-") as temporary:
        disk, path = Path(temporary) / "disk.raw", Path(temporary) / "vga.raw"
        images.build(disk); images.seed_system(disk, {"TERM.ELF": ROOT / "build/tests/user/terminal.elf"})
        original = disk.read_bytes()
        for ram, iso in ((16, False), (64, True)):
            disk.write_bytes(original); run(args.qemu, disk, ram, iso, path)
        # Saving a new best on a read-only volume must still restore the display.
        disk.write_bytes(original)
        score = Path(temporary) / "score"; score.write_bytes(b"0\n")
        images.command("mcopy", "-i", str(disk), str(score), "::/SNAKE.DAT")
        before = disk.read_bytes()
        with shell.boot(args.qemu, 16, "snake-readonly", disk=disk, options="rum.disk-readonly") as console:
            play(console, path)
            contains(console, "Best score could not be saved to disk.\r\n")
            console.command("term defaults", "terminal defaults restored")
        assert disk.read_bytes() == before
        with shell.boot(args.qemu, 16, "snake-diskless", marker="Kernel recovery shell.") as console:
            console.type("snake\n"); console.wait("rum_snake_started\r\n")
            console.send("p"); console.wait("rum_snake_paused\r\n")
            console.send("q"); console.wait("rum_snake_quit\r\n> ")
            assert "Terminal: normal" in console.command("diag")
    print("PASS: failed score saves restore the display; diskless recovery Snake needs no user ELF", flush=True)


if __name__ == "__main__":
    main()
