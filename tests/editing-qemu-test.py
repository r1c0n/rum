#!/usr/bin/env python3
"""Check quoted argv, real line editing/completion and notice redraw on disposable disks."""
import argparse
import importlib.util
from pathlib import Path
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("editing_terminal", ROOT / "tests/terminal-qemu-test.py")
terminal = importlib.util.module_from_spec(spec); spec.loader.exec_module(terminal)
shell = terminal.shell
shell.ARTIFACTS = ROOT / "build/test-artifacts/editing"
images = shell.images


def submit(console, expected):
    start = console.serial.stat().st_size
    console.send("ret")
    terminal.contains(console, expected, start)
    console.wait("/> ")


def run(qemu, disk, ram, iso, screen):
    with shell.boot(qemu, ram, f"editing-{ram}m", disk=disk, iso=iso) as c:
        args = '''edit argv "" 'a  b' "x|y" x\\'y x\\"y'''
        c.command(args, "parsed argv verified\r\n")
        c.command(args + " | cat", "parsed argv verified\r\n")
        c.command('run echo "" "a  b"', ' a  b\r\n')
        c.command('echo a\\ b \\| \\& \\> \\<', 'a b | & > <\r\n')
        c.command('echo "a  b" "" z > QUOTED.TXT')
        c.command('write "NOTE.TXT" "a  b" "" z')
        c.command('cat <"NOTE.TXT" | cat > COPY.TXT')
        for malformed in ('echo no >INVALID.TXT "unfinished', "echo no >INVALID.TXT trailing\\", "echo no >INVALID.TXT |"):
            c.command(malformed, "invalid or oversized command.")
        c.command("cat INVALID.TXT", "file or directory not found.")
        c.command("edit inline", "inline ABI verified\r\n")
        c.type("edit keys\n"); terminal.contains(c, "navigation ready\r\n")
        for key in ("left", "right", "home", "end", "delete", "up", "down"): c.send(key)
        c.wait("navigation ABI verified\r\n/> ")
        c.type("echo ac"); c.send("left"); c.type("b"); c.send("end"); submit(c, "abc\r\n")
        c.type("xecho fixed"); c.send("home"); c.send("delete"); c.send("end"); submit(c, "fixed\r\n")
        c.type("echo !"); c.send("backspace"); c.type("ok"); submit(c, "ok\r\n")
        c.send("up"); submit(c, "ok\r\n")
        # Down restores the unsubmitted line and its middle cursor, not history's end.
        c.type("echo drft"); c.send("left"); c.send("left"); c.send("up"); c.send("down"); c.type("a"); submit(c, "draft\r\n")
        c.type("discard this"); c.send("ctrl", "c"); c.wait("^C\r\n/> ")
        c.type("hell"); c.send("tab"); submit(c, "Hello from rum userspace!\r\n")
        c.type("he"); c.send("tab"); c.send("tab")
        terminal.contains(c, "HELLO\r\n")
        terminal.contains(c, "HELP\r\n")
        c.send("ctrl", "c"); c.wait("^C\r\n/> ")
        c.type("cat /DO"); c.send("tab"); c.type("'ne"); c.send("tab"); submit(c, "nested completion\r\n")
        c.type("cat sp"); c.send("tab"); submit(c, "literal filename\r\n")
        c.type("cat no"); c.send("tab"); c.send("tab")
        terminal.contains(c, "NOTE.TXT\r\n")
        terminal.contains(c, "NORTH.TXT\r\n")
        c.send("ctrl", "c"); c.wait("^C\r\n/> ")
        # Exercise wrapping/scrolling at the bottom; navigation cannot edit the prompt.
        c.command("clear")
        text = "w" * 240
        c.type("echo " + text); c.send("home"); c.send("right"); c.send("delete"); c.type("c")
        c.send("end"); submit(c, text + "\r\n")
        _, rows = terminal.screen(c, screen); assert "uptime:" in rows[24], rows[24]
        # Direct background writes and completion notices preserve a middle cursor.
        for command in ("start edit notice", "edit notice &"):
            start = c.serial.stat().st_size
            c.command(command, "started edit")
            c.type("echo ac"); c.send("left")
            terminal.contains(c, "asynchronous output\r\n", start)
            terminal.contains(c, "exited with status 0.", start)
            terminal.wait_screen(c, screen, lambda r: any(row.rstrip() == "/> echo ac" for row in r[:24]))
            time.sleep(0.05); c.type("b"); c.send("end"); submit(c, "abc\r\n")
        c.command("wait", "Program exited with status 0.")
        # Duplicate/evicted history is model-tested; whole-session cleanup is real.
        c.command("recovery", "Kernel recovery shell.")
        diag = c.command("diag")
        for expected in ("Terminal: normal", "Processes: 0 | 0 user tables, 0 user pages",
                         "Handles: 0 descriptors, 0 open descriptions, 0 aliases"):
            assert expected in diag, diag
    assert images.file_bytes(disk, "/NOTE.TXT") == b"a  b  z"
    assert images.file_bytes(disk, "/COPY.TXT") == b"a  b  z"
    assert images.file_bytes(disk, "/QUOTED.TXT") == b"a  b  z\n"
    images.audit(disk)
    print(f"PASS: {ram} MiB shell editing: quoted argv, parse-before-I/O, navigation ABI, history, completion, wrapped lines, asynchronous redraw and cleanup", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args(); shell.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rum-editing-") as temporary:
        disk, source, screen = (Path(temporary) / n for n in ("disk.raw", "source", "vga.raw"))
        images.build(disk); images.seed_system(disk, {"EDIT.ELF": ROOT / "build/tests/user/editing.elf"})
        images.command("mmd", "-i", str(disk), "::/DOCS")
        for path, data in (("/DOCS/NEST.TXT", b"nested completion\n"), ("/SP_CE.TXT", b"literal filename\n"),
                           ("/SP&CE.TXT", b"unsupported FAT spelling\n"),
                           ("/NORTH.TXT", b"other\n")):
            source.write_bytes(data); images.command("mcopy", "-i", str(disk), str(source), "::" + path)
        original = disk.read_bytes()
        for ram, iso in ((16, False), (64, True)):
            disk.write_bytes(original); run(args.qemu, disk, ram, iso, screen)


if __name__ == "__main__": main()
