# Building a release

`rum.zip` contains the ISO, README and documentation. The ISO provides the
kernel and recovery console. Normal startup also needs a prepared FAT16 disk
containing `/RUM` programs. The separate `rum-vm-images.zip` contains a fresh
installed disk in raw, VMDK and VDI formats, the ISO and a VMware preset. Never
publish a disk that has been used for personal files or testing.

## Validate a committed build

In Ubuntu/WSL, from a clean committed checkout:

```sh
python3 scripts/release-check.py
```

The checker copies tracked sources to a new temporary directory, builds with
bounded parallelism and runs the complete host/QEMU suite. Tests validate
persistent root files across separate boots, read-only/malformed/full disks,
process isolation, cancellation, cleanup and injected storage failures.
The exact ISO extracted from the package is booted too. No caller's disk is
used and the original build directory is not cleaned.

Results are saved under `build/release-check/<commit>-<time>/`: `result.json`,
logs, guest diagnostics, kernel, ISO, package and a pristine `rum-system.raw`.
The report records ISO and system-disk SHA256 values. Only a report with
`passed: true` represents a successful check.

## Verify on Windows and Linux

Run the host checker against that successful release directory:

```powershell
py -3 tests/release-host-test.py --release build/release-check/<commit>-<time> --qemu qemu-system-i386.exe
.\tests\disk-launcher-test.ps1
```

```sh
python3 tests/release-host-test.py --release build/release-check/<commit>-<time>
```

It packages with the host's native Python, verifies source/archive bytes and
boots the extracted ISO using a temporary copy of the validated system disk.
It checks program launch, process tools, cancellation, recovery and cleanup.
The original system disk is never booted writable. Source changes beyond
README/docs require a new clean release check.

## Prepare VM downloads

After building the intended source commit:

```sh
python3 scripts/prepare-vm-images.py --output build/vm-images-release
```

This refuses an existing output directory. The raw image is freshly formatted
and populated from `build/user/programs.txt`; VMDK and VDI conversions are
compared byte for byte with it. The bundle includes checksums and VM setup
instructions. Keep the ISO attached: the disk contains programs and files,
not an HDD bootloader.

## Persistence and recovery

With a prepared image, write `/NOTES.TXT`, wait for the prompt, stop QEMU and
boot again with the same disk. Read identical bytes with `cat /NOTES.TXT` and
host mtools after QEMU exits. Never open the same writable image in two VMs.

A missing, unsupported or damaged disk starts the recovery console. Help,
echo, `mem`, `diag` and Snake remain available; file commands require a usable
disk. The normal shell's built-in `recovery` command also works after storage
errors prevent loading programs. FAT16 is not journaled. Preserve a damaged
image and inspect or repair a copy with host FAT tools; rum does not silently
format or repair it. See [FAT16](fat16.md) for interrupted-write guarantees.
