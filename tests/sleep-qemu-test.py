#!/usr/bin/env python3
"""Check timer deadlines, event races, public sleep/yield and PS/2 cancellation."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("sleep_smoke", ROOT / "scripts/smoke-test.py")
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    args = parser.parse_args()
    artifacts = ROOT / "build/test-artifacts/sleep"
    artifacts.mkdir(parents=True, exist_ok=True)
    for case in ("waits", "sleep"):
        for boot in ("elf", "iso"):
            for ram in (16, 64):
                label = f"{case}-{boot}-{ram}m"
                with tempfile.TemporaryDirectory(prefix="rum-sleep-") as temporary:
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
                    image = ROOT / f"build/tests/{case}.{boot}"
                    image_args = ["-kernel", str(image)] if boot == "elf" else ["-boot", "d", "-cdrom", str(image)]
                    process = subprocess.Popen([
                        args.qemu, "-machine", "pc", "-accel", "tcg", "-m", f"{ram}M", "-display", "none",
                        "-serial", f"file:{serial}", "-qmp", qmp, "-no-reboot", "-no-shutdown", *image_args,
                    ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                    sent = False
                    try:
                        deadline = time.monotonic() + 90
                        while True:
                            text = serial.read_text(errors="replace") if serial.exists() else ""
                            if f"rum_{case}_ok" in text:
                                assert case != "sleep" or sent, "PS/2 cancellation was not exercised"
                                break
                            if "_failed:" in text or "rum kernel panic" in text or process.poll() is not None or time.monotonic() > deadline:
                                raise AssertionError(f"{label}: {text[-5000:]}")
                            if "rum_sleep_keyboard_ready" in text and not sent:
                                with socket.socket(family, socket.SOCK_STREAM) as connection:
                                    connection.settimeout(10)
                                    connection.connect(address)
                                    with connection.makefile("rwb") as stream:
                                        assert "QMP" in json.loads(stream.readline())
                                        smoke.qmp_command(stream, "qmp_capabilities")
                                        send, _, _ = smoke.guest_keyboard(stream, serial)
                                        send("ctrl", "c")
                                        sent = True
                            time.sleep(0.02)
                    finally:
                        if process.poll() is None:
                            process.kill()
                        process.wait(timeout=5)
                        (artifacts / f"{label}.qemu.log").write_bytes(process.stderr.read())
                        if serial.exists():
                            (artifacts / f"{label}.serial.log").write_bytes(serial.read_bytes())
                print(f"PASS: {label}: bounded sleep/yield, clock wrap, event/deadline races, cancellation and cleanup", flush=True)


if __name__ == "__main__":
    main()
