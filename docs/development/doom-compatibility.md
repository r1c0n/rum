# Doom application compatibility

Doom remains a userspace application target. The SDK probe prepares its runtime
requirements without adding game code, display shortcuts or hardware access
to the kernel. The first playable port remains planned after the foundation
gate; the current probe does not implement graphics or game input.

## Source and reproducibility

The candidate is [DoomGeneric](https://github.com/ozkl/doomgeneric), pinned to
[`e257a09527062bfb986b6e5e0780839b6cbd5b71`](https://github.com/ozkl/doomgeneric/tree/e257a09527062bfb986b6e5e0780839b6cbd5b71).
Its source is GPL-2.0-or-later. The SDK includes rum adapters and probe tooling;
it contains no upstream engine source, compiled game or WAD assets. The probe
fetches source only with an explicit `--fetch`; ordinary SDK tests stay offline.
Existing source directories must be clean and at the pinned revision and are
never updated automatically.

From an unpacked SDK and separate work directory:

```sh
python3 "$RUM_SDK/tools/probe-doom.py" --source ./doomgeneric-source --output ./doom-report --cross-prefix /path/to/i686-elf- --fetch
```

Keep output outside the upstream checkout. Without `--fetch`, the same command
uses an existing checkout offline. Compilation uses four workers by default;
`--jobs` accepts 1–4.

## What the probe proves

The tool compiles the 80 engine units named by the pinned upstream Makefile,
excluding its X11 platform backend. Every unit gets a diagnostic log and an
entry in `report.json`. Missing declarations are errors. Two probe-only
declaration bridges, `inttypes.h` and `strings.h`, let common type headers
compile without treating those missing library surfaces as implemented SDK
features. They are not on the normal application include path.

The compiled units also receive a partial-link check. A full engine link is
attempted only once all units compile, with a headless rum platform adapter;
that result is reported separately and is never launched automatically.
Undefined symbols in individual objects include internal engine dependencies,
so the report does not label every undefined symbol as a missing SDK function.

Independently, the tool builds and validates `DOOMIO.elf` from the upstream
`z_zone.c` and `w_file_stdc.c` without modifying either source. The rum adapter
supplies zone backing through `malloc`, fatal error reporting and file length
through stdio. The resulting program initializes Doom's real zone allocator,
opens a synthetic single-lump WAD through the real standard-C backend, seeks
to directory/payload offsets, compares exact binary bytes, checks EOF and
closes/frees its resources. It also queries the public monotonic clock.

That is a runnable subsystem probe, not full `D_DoomMain` initialization or a
test with a commercial/shareware IWAD. The synthetic fixture contains no game
assets and cannot be used to play Doom.

## Measured readiness

On 2026-11-09, the exported SDK compiled 53 of 80 pinned engine units with the
declaration bridges. Full-engine linking remained blocked by compile failures.
The zone/WAD ELF compiled, linked, passed ELF validation and ran successfully
through ELF boot at 16 MiB and ISO boot at 64 MiB. The synthetic WAD bytes
remained unchanged on the FAT16 disk, and recovery diagnostics returned to
zero user pages, regions, mapping leases and handles.

| Area | Current result / remaining work |
| --- | --- |
| Heap | Real zone initialization works with a 1 MiB arena; full-game peak memory and its usual larger arena still need measurement |
| WAD I/O | Real stdio backend passes exact random/sequential reads and EOF on the synthetic fixture; real IWAD initialization remains untested |
| Executable loading | The linked probe runs as an ordinary SDK-built ELF; full-engine image/BSS and memory use remain to be measured |
| C headers | Missing `ctype.h`, `math.h`, `unistd.h`, `assert.h` and `fcntl.h` stop some translation units |
| C functions | `abs` lacks a declaration/implementation; further string, conversion, scanning, environment and POSIX requirements need resolving beyond the first compile errors |
| Math and CPU state | Audit floating-point operations and runtime helpers against rum's integer-only CPU policy; do not enable x87/SSE without kernel state support |
| Graphics and input | Add a public framebuffer presentation interface and complete key press/release delivery in the planned foundation/graphics work |
| Paths | Adapt game/config discovery to FAT16's 8.3 rules; long config names and host environment search paths cannot be assumed |

Runtime work should add supported, tested behavior rather than hiding missing
functions behind successful no-op stubs. Background jobs, privileges or kernel
pointers are not substitutes for missing application interfaces.

## Repository verification

After exporting the SDK and obtaining the pinned checkout, maintainers can run:

```sh
python3 tests/sdk-package-test.py --cross-prefix "$PWD/.tools/cross/bin/i686-elf-" --doom-source build/doomgeneric-source
python3 tests/sdk-qemu-test.py --doom
```

Build the current ISO and user programs first with `./rum build`. The host
check unpacks the SDK outside the repository and builds both applications there.
The guest check uses disposable disks, installs the ELFs and synthetic WAD,
checks normal/fault/cancellation cleanup and validates the resulting FAT image.
