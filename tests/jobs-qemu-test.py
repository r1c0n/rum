#!/usr/bin/env python3
"""Exercise session jobs and real keyboard foreground control on disposable FAT disks."""
import argparse
import importlib.util
from pathlib import Path
import re
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("jobs_shell", ROOT / "tests/userspace-shell-test.py")
shell = importlib.util.module_from_spec(spec); spec.loader.exec_module(shell)
shell.ARTIFACTS = ROOT / "build/test-artifacts/jobs"
images = shell.images
PAYLOAD = bytes((i * 37 ^ (i >> 3)) & 255 for i in range(3 * 4096 + 173))


def launch(console, command, early_fault=False):
    start = console.serial.stat().st_size
    console.type(command + " &\n")
    deadline = time.monotonic() + 20
    while True:
        output = console.serial.read_bytes()[start:].decode()
        match = re.search(r"\[(\d+)\] started [^\r\n]+\r\n", output)
        # A fast job can finish between its start announcement and the prompt.
        if match and output[match.end():].endswith("/> "): break
        if early_fault and output.endswith("/> ") and "Program stopped after a user fault." in output:
            return None
        assert "rum kernel panic" not in output and time.monotonic() < deadline, output
        time.sleep(0.02)
    return int(match[1])


def focus(console, job):
    start = console.serial.stat().st_size
    console.type(f"fg %{job}\n")
    deadline = time.monotonic() + 20
    while f"Foreground job {job}.\r\n".encode() not in console.serial.read_bytes()[start:]:
        assert time.monotonic() < deadline, console.serial.read_bytes()[start:]
        time.sleep(0.02)


def run(qemu, disk, ram, iso):
    with shell.boot(qemu, ram, f"jobs-{ram}m", disk=disk, iso=iso) as console:
        console.command("jobs", "No jobs.")
        copy = launch(console, "cat INPUT.BIN | cat | cat > COPY.BIN")
        console.command(f"wait {copy}", "Program exited with status 0.")
        console.command(f"fg {copy}", "job not found.")
        sleeper = launch(console, "sleep 5000")
        console.command(f"wflow jobabi {sleeper}", "job ABI validated")
        console.command("jobs", f"[{sleeper}] running  sleep | PIDs:")
        focus(console, sleeper)
        console.send("ctrl", "c"); console.wait("Program cancelled.\r\n/> ")
        reader = launch(console, "wflow focusread | cat > KEY.TXT")
        focus(console, reader)
        console.send("z"); console.wait("Program exited with status 0.\r\n/> ")
        denied = launch(console, "wflow bgread")
        console.command(f"wait {denied}", "Program exited with status 0.")
        # Completion is announced exactly once and retained for explicit collection.
        done = launch(console, "sleep 50")
        time.sleep(0.15)
        console.command("echo after completion", "after completion\r\n")
        text = console.serial.read_bytes().decode()
        assert text.count(f"[{done}] sleep: exited with status 0.") == 1, text[-3000:]
        console.command("jobs", f"[{done}] done  sleep")
        console.command(f"fg {done}", "Program exited with status 0.")
        # Concurrent exits retain every stage result and the rightmost normal status.
        a = launch(console, "nonzero | cat")
        b = launch(console, "echo data | nonzero")
        console.command(f"wait {a}", "Program exited with status 0.")
        console.command(f"wait {b}", "Program exited with status -37.")
        bad = launch(console, "spin | fault | cat", early_fault=True)
        if bad is not None:
            console.command(f"wait {bad}", "Program stopped after a user fault.")
        console.command("spin | absent | cat &", "file or directory not found.")
        console.command("echo wrong >INVALID.TXT & cat", "invalid or oversized command.")
        survivor = launch(console, "spin")
        for command in ("cat INPUT.BIN | spin", "spin | cat > BLOCK.BIN", "wflow diskloop ACTIVE.BIN"):
            job = launch(console, command)
            focus(console, job)
            console.send("ctrl", "c"); console.wait("Program cancelled.\r\n/> ")
            assert f"[{survivor}] running" in console.command("jobs")
        focus(console, survivor)
        console.send("ctrl", "c"); console.wait("Program cancelled.\r\n/> ")
        # Done records count toward the eight-job bound until wait/fg collects them.
        held = [launch(console, "sleep 1") for _ in range(8)]
        console.command("echo no >TOOFULL.TXT &", "job table is full")
        console.command("wait", "Program exited with status 0.")
        console.command("jobs", "No jobs.")
        console.command(f"wait {held[0]}", "job not found.")
        console.command("fg 0", "Usage: fg")
        # A waiting controller can be cancelled without killing its background job.
        job = launch(console, "spin | cat")
        console.type(f"wait {job}\n"); time.sleep(0.1); console.send("ctrl", "c")
        console.wait("Program cancelled.\r\n/> ")
        assert f"[{job}] running" in console.command("jobs")
        focus(console, job)
        console.send("ctrl", "c"); console.wait("Program cancelled.\r\n/> ")
        console.command("echo restored", "restored\r\n")
        console.command("recovery", "Kernel recovery shell.")
        diag = console.command("diag")
        for expected in ("Processes: 0 | 0 user tables, 0 user pages", "Jobs: 0 live, 0 running, 0 completed, 0 active waits",
                         "Handles: 0 descriptors, 0 open descriptions, 0 aliases",
                         "Pipes: 0 live, 0 buffered bytes | 0 read ends, 0 write ends, 0 active operations, 0 waiters",
                         "Sessions: 0 live, 0 closing, 0 members | 0 session refs, 0 process refs"):
            assert expected in diag, diag
    assert images.file_bytes(disk, "/COPY.BIN") == PAYLOAD
    assert images.file_bytes(disk, "/KEY.TXT") == b"reader got: z\n"
    assert images.file_bytes(disk, "/ACTIVE.BIN") == b"Z" * 512
    assert images.file_bytes(disk, "/INPUT.BIN") == PAYLOAD
    listing = images.command("mdir", "-b", "-i", str(disk), "::/").decode().upper()
    assert "INVALID.TXT" not in listing and "TOOFULL.TXT" not in listing
    images.audit(disk)
    print(f"PASS: {ram} MiB jobs: background streams, ABI, fg/wait, Ctrl+C isolation, completion retention, limits and cleanup", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args(); shell.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rum-jobs-") as temporary:
        disk, source = Path(temporary) / "disk.raw", Path(temporary) / "source"
        images.build(disk); images.seed_system(disk, {"WFLOW.ELF": ROOT / "build/tests/user/workflow.elf"})
        source.write_bytes(PAYLOAD); images.command("mcopy", "-i", str(disk), str(source), "::/INPUT.BIN")
        original = disk.read_bytes()
        for ram, iso in ((16, False), (64, True)):
            disk.write_bytes(original); run(args.qemu, disk, ram, iso)
        disk.write_bytes(original)
        with shell.boot(args.qemu, 16, "jobs-restart", disk=disk) as console:
            old = launch(console, "spin | cat > ABANDON.BIN")
            console.command("exit", "Type 'help' for commands.")
            console.command("jobs", "No jobs.")
            console.command(f"fg {old}", "job not found.")
            console.command("recovery", "Kernel recovery shell.")
            diag = console.command("diag")
            assert "Jobs: 0 live, 0 running, 0 completed, 0 active waits" in diag
            assert "Processes: 0 | 0 user tables, 0 user pages" in diag
    print("PASS: shell restart cancels and reclaims abandoned job stages and records", flush=True)


if __name__ == "__main__":
    main()
