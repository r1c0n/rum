#!/usr/bin/env python3
"""Build, test, package and run rum from Linux or the Windows WSL launcher."""
import argparse
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SUITES = {
    "all": "test", "host": "test-host", "user": "test-user",
    "package": "test-package", "shell": "test-userspace-shell",
    "storage": "test-release-integration", "block": "test-block",
    "fat16": "test-fat16", "fat16-write": "test-fat16-write",
    "filesystem": "test-fs", "preemption": "test-preemption",
    "lifecycle": "test-lifecycle", "sleep": "test-sleep",
    "console": "test-console-sessions", "integration": "test-integration-stress",
    "handles": "test-shared-handles",
    "pipes": "test-pipes",
}
BOOT_ACTIONS = {"run", "run-kernel", "debug", "panic"}
DISK_ACTIONS = BOOT_ACTIONS - {"panic"} | {"create-disk", "prepare-disk"}
ACTIONS = ("build", "user", "run", "run-kernel", "debug", "test", "doctor",
           "setup", "toolchain", "clean", "prepare-disk", "create-disk",
           "package", "vm-images", "release-check", "panic", "help")


def bounded(low, high):
    def number(text):
        try:
            value = int(text)
        except ValueError:
            raise argparse.ArgumentTypeError("expected an integer") from None
        if not low <= value <= high:
            raise argparse.ArgumentTypeError(f"expected {low} through {high}")
        return value
    return number


def parser():
    result = argparse.ArgumentParser(description=__doc__, epilog=(
        "Examples: ./rum build -j 4; ./rum test --workers 4; "
        "./rum prepare-disk --disk build/system.raw; ./rum run --disk build/system.raw. "
        "Paths are relative to your current directory. No command prepares a disk implicitly."))
    result.add_argument("action", nargs="?", choices=ACTIONS, default="run")
    result.add_argument("-j", "--jobs", type=bounded(1, 64), default=os.environ.get("JOBS", "4"),
                        help="parallel build jobs (default: JOBS or 4)")
    result.add_argument("--workers", type=bounded(1, 4), default=os.environ.get("TEST_JOBS", "4"),
                        help="concurrent QEMU test workers (default: TEST_JOBS or 4)")
    result.add_argument("--suite", choices=SUITES, default="all", help="test group (default: all)")
    result.add_argument("--disk", type=Path, default=os.environ.get("DISK_IMAGE") or None,
                        help="explicit raw disk image; new path for prepare-disk/create-disk")
    result.add_argument("--read-only", action="store_true", default=os.environ.get("DISK_READ_ONLY") == "1")
    result.add_argument("--size-mib", type=bounded(1, 131071), default=os.environ.get("DISK_SIZE_MIB", "16"))
    result.add_argument("--memory", type=bounded(16, 4096), default=64, help="guest RAM in MiB (default: 64)")
    result.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    result.add_argument("--windows-qemu", type=Path, help=argparse.SUPPRESS)
    result.add_argument("--cross-prefix", default=os.environ.get("CROSS_PREFIX"))
    result.add_argument("--output", type=Path, help="new VM bundle directory or package ZIP path")
    result.add_argument("--no-build", action="store_true", help="use existing artifacts for boot or packaging")
    result.add_argument("--dry-run", action="store_true", help="validate options and print commands without executing")
    return result


def run(command, *, dry_run=False, environment=None):
    command = [str(value) for value in command]
    print("rum: " + shlex.join(command), flush=True)
    if not dry_run:
        subprocess.run(command, cwd=ROOT, env=environment, check=True)


def disk_launcher():
    spec = importlib.util.spec_from_file_location("rum_qemu", ROOT / "scripts/run-qemu.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate(args):
    if args.disk is not None:
        args.disk = args.disk.absolute()
        if args.action not in DISK_ACTIONS:
            raise ValueError("--disk is only supported for boot and disk creation")
        if args.action in {"create-disk", "prepare-disk"}:
            if args.disk.exists() or args.disk.is_symlink():
                raise ValueError(f"image already exists: {args.disk}; choose a new path")
            if not args.disk.parent.is_dir():
                raise ValueError(f"image parent does not exist: {args.disk.parent}")
        else:
            args.disk = disk_launcher().validate_disk(args.disk)
    elif args.action in {"create-disk", "prepare-disk"}:
        raise ValueError("supply --disk with a new image path")
    if args.read_only and (args.action not in BOOT_ACTIONS or args.disk is None):
        raise ValueError("--read-only requires a boot action and an existing --disk")
    if args.action == "prepare-disk" and not 16 <= args.size_mib <= 128:
        raise ValueError("prepared system disks must be 16 through 128 MiB")
    if args.action == "vm-images" and args.output is None:
        args.output = ROOT / "build/vm-images"
    if args.output is not None:
        args.output = args.output.absolute()
        if args.action not in {"package", "vm-images"}:
            raise ValueError("--output is only supported for package and vm-images")
        if args.action == "vm-images" and (args.output.exists() or args.output.is_symlink()):
            raise ValueError("VM output already exists; choose a new directory")
        if args.action == "package":
            if args.output.suffix.lower() != ".zip" or args.output.is_symlink() or args.output.is_dir():
                raise ValueError("package output must be a ZIP file path, not a directory or symlink")
            if not args.output.parent.is_dir():
                raise ValueError("package output parent must already exist")
    if args.no_build and args.action not in BOOT_ACTIONS | {"package", "vm-images"}:
        raise ValueError("--no-build applies only to boot or packaging")
    if args.suite != "all" and args.action != "test":
        raise ValueError("--suite requires the test action")
    if args.action in BOOT_ACTIONS and not args.dry_run:
        qemu = str(args.windows_qemu or args.qemu)
        if not shutil.which(qemu):
            raise ValueError(f"QEMU not found: {qemu}; run doctor or supply --qemu")


def dispatch(args):
    validate(args)
    environment = os.environ.copy()
    environment["JOBS"] = str(args.jobs)
    environment["QEMU"] = args.qemu
    if args.cross_prefix:
        environment["CROSS_PREFIX"] = args.cross_prefix
    def execute(*command):
        run(command, dry_run=args.dry_run, environment=environment)
    def make(*targets):
        execute("make", "--no-print-directory", f"-j{args.jobs}",
                f"TEST_JOBS={args.workers}", f"QEMU={args.qemu}", *targets)
    def script(name, *arguments):
        execute(sys.executable, ROOT / "scripts" / name, *arguments)

    action = args.action
    if action == "build":
        make("all")
    elif action == "user":
        make("user")
    elif action == "test":
        make(SUITES[args.suite])
    elif action in BOOT_ACTIONS:
        if not args.no_build:
            make(*({"run-kernel": ("check", "user"), "panic": ("build/tests/fault-ud.elf",)}.get(action, ("all",))))
        options = ["--qemu", str(args.windows_qemu or args.qemu), "--memory", str(args.memory)]
        if args.windows_qemu:
            options.append("--windows-paths")
        if action in {"run-kernel", "panic"}:
            options += ["--kernel", "--image", "build/rum.elf" if action == "run-kernel" else "build/tests/fault-ud.elf"]
        if action == "debug":
            options.append("--debug")
        if args.disk is not None:
            options += ["--disk", args.disk]
        if args.read_only:
            options.append("--disk-read-only")
        script("run-qemu.py", *options)
    elif action in {"prepare-disk", "create-disk"}:
        if action == "prepare-disk":
            make("user")
        script("prepare-disk.py" if action == "prepare-disk" else "disk-image.py",
               args.disk, "--size-mib", str(args.size_mib))
    elif action in {"package", "vm-images"}:
        if not args.no_build:
            make("all")
        options = ["--output", args.output] if args.output else []
        script("package.py" if action == "package" else "prepare-vm-images.py", *options)
    elif action == "clean":
        script("clean-build.py")
    elif action == "release-check":
        options = ["--jobs", str(args.jobs), "--workers", str(args.workers), "--qemu", args.qemu]
        if args.cross_prefix:
            options += ["--cross-prefix", args.cross_prefix]
        script("release-check.py", *options)
    elif action in {"doctor", "toolchain", "setup"}:
        if action == "setup":
            execute("sudo", "bash", ROOT / "scripts/install-deps.sh")
        if action in {"toolchain", "setup"}:
            execute("bash", ROOT / "scripts/build-toolchain.sh")
        if action in {"doctor", "setup"}:
            execute("bash", ROOT / "scripts/doctor.sh")
        if args.windows_qemu:
            execute(args.windows_qemu, "--version")


def main(argv=None):
    options = parser()
    args = options.parse_args(argv)
    if args.action == "help":
        options.print_help()
        return 0
    if os.name == "nt":
        options.error("use rum.ps1 on Windows; builds and tools run in WSL")
    try:
        dispatch(args)
    except subprocess.CalledProcessError as error:
        print(f"rum: command failed (exit {error.returncode})", file=sys.stderr)
        return error.returncode if 0 < error.returncode < 256 else 1
    except (OSError, ValueError) as error:
        print(f"rum: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
