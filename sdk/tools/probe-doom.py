#!/usr/bin/env python3
"""Probe pinned DoomGeneric sources against this SDK; no game data is downloaded."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
from pathlib import Path
import re
import subprocess

SDK = Path(__file__).resolve().parents[1]
REVISION = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
UPSTREAM = "https://github.com/ozkl/doomgeneric.git"


def load_builder():
    spec = importlib.util.spec_from_file_location("sdk_build", SDK / "tools/build.py")
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


def sources(path, fetch):
    if not path.exists():
        if not fetch:
            raise ValueError("source directory is missing; use --fetch to download the pinned source")
        path.mkdir(parents=True)
        subprocess.run(["git", "init", str(path)], check=True, capture_output=True)
        subprocess.run(["git", "-C", str(path), "fetch", "--depth", "1", UPSTREAM, REVISION], check=True)
        subprocess.run(["git", "-C", str(path), "checkout", "--detach", "FETCH_HEAD"], check=True, capture_output=True)
    revision = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(path), "status", "--porcelain"], text=True)
    if revision != REVISION or dirty:
        raise ValueError("probe requires a clean checkout of the pinned revision; existing sources are never changed")
    return path / "doomgeneric"


def probe(source, output, prefix, jobs=4, fetch=False):
    if output == source or output.is_relative_to(source):
        raise ValueError("probe output must be outside the upstream source checkout")
    engine = sources(source, fetch)
    output.mkdir(parents=True, exist_ok=True)
    builder = load_builder(); config = builder.configuration(prefix)
    adapter = SDK / "probes/doom"
    # These two headers expose additional diagnostics without pretending to
    # implement their library functions. They are not on normal SDK include paths.
    flags = [*config["cflags"], "-I" + str(adapter / "include"), "-I" + str(engine),
             "-Werror=implicit-function-declaration", "-Wno-unused-parameter", "-Wno-sign-compare"]
    makefile = (engine / "Makefile").read_text()
    names = re.search(r"^SRC_DOOM\s*=\s*(.+)$", makefile, re.M).group(1).split()
    names = [Path(name).with_suffix(".c").name for name in names if name != "doomgeneric_xlib.o"]
    objects = output / "objects"; objects.mkdir(exist_ok=True)

    def compile_one(name):
        result = subprocess.run([config["cc"], *flags, "-c", str(engine / name), "-o", str(objects / Path(name).with_suffix(".o"))], capture_output=True, text=True)
        (output / (name + ".log")).write_text(result.stdout + result.stderr)
        return {"source": name, "compiled": result.returncode == 0,
                "missing_headers": sorted(set(re.findall(r"fatal error: ([^:\n]+): No such file", result.stderr))),
                "missing_declarations": sorted(set(re.findall(r"implicit declaration of function [‘']([^’']+)", result.stderr)))}

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        units = list(pool.map(compile_one, names))
    # Actual upstream code is compiled and linked for a runnable headless subset.
    # This uses Z_Init/Z_Malloc/Z_Free and the engine's unmodified stdio WAD backend.
    smoke = output / "DOOMIO.elf"
    builder.build([adapter / "headless.c", engine / "z_zone.c", engine / "w_file_stdc.c"], smoke, prefix,
                  extra=["-I" + str(adapter / "include"), "-I" + str(engine), "-Wno-unused-parameter"])
    undefined = []
    built = [objects / Path(unit["source"]).with_suffix(".o") for unit in units if unit["compiled"]]
    if built:
        symbols = subprocess.check_output([prefix + "nm", "--undefined-only", *map(str, built)], text=True)
        undefined = sorted(set(re.findall(r"\bU\s+(\S+)", symbols)))
    platform = objects / "rum-platform.o"
    subprocess.run([config["cc"], *flags, "-c", str(adapter / "platform.c"), "-o", str(platform)], check=True)
    partial = subprocess.run([config["cc"], "-nostdlib", "-r", *map(str, built), str(platform),
                              "-o", str(output / "partial-engine.o")], capture_output=True, text=True)
    (output / "partial-link.log").write_text(partial.stdout + partial.stderr)
    full_link = "blocked by compile failures"
    missing_link = []
    if all(unit["compiled"] for unit in units):
        linked = subprocess.run([config["cc"], *config["ldflags"], config["startup"], *map(str, built),
            str(platform), *config["libraries"], "-o", str(output / "doom-engine.debug.elf")], capture_output=True, text=True)
        (output / "full-link.log").write_text(linked.stdout + linked.stderr)
        full_link = "passed (not executed)" if linked.returncode == 0 else "failed"
        missing_link = sorted(set(re.findall(r"undefined reference to [`‘']([^'’]+)", linked.stderr)))
    report = {"upstream": UPSTREAM, "revision": REVISION, "engine_units": units,
              "compiled_units": sum(unit["compiled"] for unit in units), "total_units": len(units),
              "full_engine_link": full_link, "missing_link_symbols": missing_link,
              "partial_engine_link": "passed" if partial.returncode == 0 else "failed",
              "object_undefined_symbols": undefined, "headless_link": "passed", "headless_elf": smoke.name,
              "probe_only_headers": ["inttypes.h", "strings.h"],
              "remaining_platform_work": ["framebuffer presentation", "key press and release input", "full C/runtime and math dependencies", "FAT 8.3 data/config paths"],
              "note": "Object undefined symbols include references resolved by other engine objects; they are not all missing SDK functions."}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"DoomGeneric: {report['compiled_units']}/{len(units)} engine units compiled; headless zone/WAD ELF linked.")
    print(f"Report: {output / 'report.json'}")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cross-prefix", default="i686-elf-")
    parser.add_argument("--jobs", type=int, choices=range(1, 5), default=4)
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    try:
        probe(args.source.resolve(), args.output.resolve(), args.cross_prefix, args.jobs, args.fetch)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Doom probe: {error}\n")


if __name__ == "__main__":
    main()
