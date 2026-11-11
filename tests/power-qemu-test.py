#!/usr/bin/env python3
"""Exercise shell restarts, real reset/power-off and the recovery VGA banner."""
import argparse
import importlib.util
from pathlib import Path
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("power_shell", ROOT / "tests/userspace-shell-test.py")
shell = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shell)
shell.ARTIFACTS = ROOT / "build/test-artifacts/power"


def restarted(console, start):
    deadline = time.monotonic() + 30
    while True:
        output = console.serial.read_bytes()[start:]
        if b"rum_boot_ok\r\n" in output and output.endswith(b"/> "):
            return
        assert time.monotonic() < deadline, output[-4000:]
        time.sleep(0.02)


def power_off(console):
    deadline = time.monotonic() + 10
    while shell.smoke.qmp_command(console.stream, "query-status")["status"] != "shutdown":
        assert time.monotonic() < deadline, console.serial.read_bytes()[-3000:]
        time.sleep(0.02)


def banner(console, label):
    data = shell.smoke.dump_ram(console.stream, 0xB8000, 4000, shell.ARTIFACTS / f"{label}.vga.bin")
    assert data[:160:2].decode("ascii").strip() == "rum | Recovery shell"
    assert data[1:160:2] == bytes([0x1F]) * 80
    assert b"uptime:" in data[24 * 160::2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    shell.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rum-power-") as temporary:
        disk = Path(temporary) / "system.raw"
        shell.images.build(disk)
        shell.images.seed_system(disk)
        for ram, iso in ((16, False), (64, True)):
            label = f"{'iso' if iso else 'elf'}-{ram}m"
            with shell.boot(args.qemu, ram, label, disk=disk, iso=iso) as c:
                # Override the test harness's stop-on-reset defaults. Production
                # run-qemu has neither flag and exits normally on power-off.
                shell.smoke.qmp_command(c.stream, "set-action", {"reboot": "reset", "shutdown": "pause"})
                for _ in range(5):
                    c.command("exit", "Restarting the shell.")
                c.command("/RUM/EXIT.ELF", "Restarting the shell.")
                assert "Kernel recovery shell." not in c.serial.read_text()
                c.command("shutdown extra", "Usage: shutdown")
                c.command("reboot extra", "Usage: reboot")
                c.command("write /POWER.TXT saved before reset")
                c.command("start spin", "started spin")
                start = c.serial.stat().st_size
                c.type("reboot\n")
                restarted(c, start)
                c.command("cat /POWER.TXT", "saved before reset")
                c.command("start spin", "started spin")
                c.type("shutdown\n")
                power_off(c)
                start = c.serial.stat().st_size
                shell.smoke.qmp_command(c.stream, "system_reset")
                shell.smoke.qmp_command(c.stream, "cont")
                restarted(c, start)
                c.command("cat /POWER.TXT", "saved before reset")
                c.command("recovery", "Kernel recovery shell.")
                banner(c, label + "-entry")
                c.command("clear")
                for _ in range(2):
                    c.command("help", "Commands:")
                banner(c, label + "-scroll")
                c.command("run /RUM/HELLO.ELF", "exited with status 0")
                banner(c, label + "-program")
                c.type("snake\n"); c.wait("rum_snake_started\r\n")
                c.send("q"); c.wait("rum_snake_quit\r\n> ")
                banner(c, label + "-snake")
                start = c.serial.stat().st_size
                c.type("reboot\n")
                restarted(c, start)
                c.command("recovery", "Kernel recovery shell.")
                c.type("shutdown\n")
                power_off(c)
            assert shell.images.file_bytes(disk, "/POWER.TXT") == b"saved before reset"
            shell.images.audit(disk)
            print(f"PASS: {label} intentional exits, child cleanup, reboot, shutdown, disk bytes and recovery banner", flush=True)
        before = disk.read_bytes()
        with shell.boot(args.qemu, 16, "readonly", disk=disk, options="rum.disk-readonly") as c:
            c.type("shutdown\n"); power_off(c)
        assert disk.read_bytes() == before
        with shell.boot(args.qemu, 16, "diskless", marker="Kernel recovery shell.") as c:
            banner(c, "diskless")
            c.type("shutdown\n"); power_off(c)
        print("PASS: read-only and diskless power-off", flush=True)


if __name__ == "__main__":
    main()
