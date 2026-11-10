#!/usr/bin/env python3
"""Build a static i386 rum application using only this installed SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

SDK = Path(__file__).resolve().parents[1]
CFLAGS = ["-std=gnu11", "-ffreestanding", "-O2", "-g", "-Wall", "-Wextra",
          "-fno-stack-protector", "-fno-pie", "-fno-pic", "-fno-builtin",
          "-fno-asynchronous-unwind-tables", "-msoft-float", "-mno-mmx", "-mno-sse", "-mno-sse2"]


def configuration(prefix):
    return {"cc": prefix + "gcc", "objcopy": prefix + "objcopy",
            "cflags": ["-I" + str(SDK / "include/c"), "-I" + str(SDK / "include"), *CFLAGS],
            "ldflags": ["-T", str(SDK / "linker/user.ld"), "-nostdlib", "-static", "-no-pie",
                        "-Wl,--build-id=none", "-Wl,--no-undefined", "-Wl,-z,max-page-size=0x1000"],
            "startup": str(SDK / "lib/crt0.o"), "libraries": [str(SDK / "lib/librum.a"), "-lgcc"]}


def run(command):
    print("sdk: " + shlex.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True)


def validator(directory, host_cc):
    output = directory / ("check-user-elf.exe" if os.name == "nt" else "check-user-elf")
    headers = sorted((SDK / "include/rum").rglob("*.h"))
    source = SDK / "tools/check-user-elf.c"
    digest = hashlib.sha256()
    for path in [source, *headers]:
        digest.update(path.relative_to(SDK).as_posix().encode() + b"\0" + path.read_bytes())
    signature = output.with_suffix(".sha256")
    identity = digest.hexdigest()
    # ZIP timestamps are fixed, so mtime alone cannot detect an SDK update.
    if not output.exists() or not signature.exists() or signature.read_text() != identity:
        run([host_cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(SDK / "include"), source, "-o", output])
        signature.write_text(identity)
    return output


def build(sources, output, prefix, host_cc="cc", extra=()):
    config = configuration(prefix)
    machine = subprocess.check_output([config["cc"], "-dumpmachine"], text=True).strip()
    if machine != "i686-elf":
        raise ValueError(f"expected i686-elf cross compiler, got {machine}")
    output = Path(output).absolute()
    sources = [Path(source).absolute() for source in sources]
    if output.suffix != ".elf" or output.is_symlink() or output in sources:
        raise ValueError("application output must be an ELF path distinct from its sources")
    output.parent.mkdir(parents=True, exist_ok=True)
    objects = []
    object_dir = output.parent / (output.stem + "-objects")
    object_dir.mkdir(exist_ok=True)
    checker = validator(object_dir, host_cc)
    for source in sources:
        if not source.is_file():
            raise ValueError(f"source not found: {source}")
        name = source.stem + "-" + hashlib.sha256(str(source).encode()).hexdigest()[:12] + ".o"
        obj = object_dir / name
        run([config["cc"], *config["cflags"], *extra, "-c", source, "-o", obj])
        objects.append(obj)
    debug = output.with_suffix(".debug.elf")
    run([config["cc"], *config["ldflags"], "-Wl,-Map," + str(output.with_suffix(".map")),
         config["startup"], *objects, *config["libraries"], "-o", debug])
    temporary = output.with_suffix(".elf.tmp")
    try:
        run([config["objcopy"], "--strip-all", debug, temporary])
        run([checker, temporary, debug])
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", nargs="*")
    parser.add_argument("--output", type=Path, default=Path("build/example.elf"))
    parser.add_argument("--cross-prefix", default=os.environ.get("CROSS_PREFIX", "i686-elf-"))
    parser.add_argument("--host-cc", default=os.environ.get("HOST_CC", "cc"))
    parser.add_argument("--cflag", action="append", default=[])
    parser.add_argument("--print-config", action="store_true")
    args = parser.parse_args()
    try:
        if args.print_config:
            print(json.dumps(configuration(args.cross_prefix), indent=2))
        elif not args.sources:
            parser.error("supply at least one source file")
        else:
            build(args.sources, args.output, args.cross_prefix, args.host_cc, args.cflag)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"sdk: {error}\n")


if __name__ == "__main__":
    main()
