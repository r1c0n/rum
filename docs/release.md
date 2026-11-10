# Building a release

`rum.zip` contains the ISO, README and documentation. The ISO provides the
kernel and recovery console. Normal startup also needs a prepared FAT16 disk
containing `/RUM` programs. The separate `rum-vm-images.zip` contains a fresh
installed disk in raw, VMDK and VDI formats, the ISO and a VMware preset. Never
publish a disk that has been used for personal files or testing.

## Validate a committed build

In Ubuntu/WSL, from a clean committed checkout:

```sh
python3 scripts/release-check.py --jobs 4 --workers 4
```

The checker copies tracked sources to a new temporary directory, builds with
bounded parallelism and runs the complete host/QEMU suite. Tests validate
persistent root files across separate boots, read-only/malformed/full disks,
process isolation, cancellation, cleanup and injected storage failures.
The production-shell scenario pipes binary ELF bytes to a file, appends quoted
text, copies and renames files, cancels a blocked pipeline, collects a child
fault and restarts the shell with an active job. Every resource ledger returns
to its recovery baseline. A separate boot reads identical saved bytes and runs
the copied application from `/APPS`.
The exact ISO extracted from the package is booted too. No caller's disk is
used and the original build directory is not cleaned.

Results are saved under `build/release-check/<commit>-<time>/`: `result.json`,
logs, guest diagnostics, kernel, ISO, package and a pristine `rum-system.raw`.
The report records ISO, SDK and system-disk SHA256 values. Only a report with
`passed: true` represents a successful check.

The release gate combines subsystem checks with the production workflow:

| Area | Evidence |
| --- | --- |
| Ownership transfers | Host handle/pipe tests and guest construction-budget tests verify rollback of descriptors, aliases, pages and stacks |
| Active jobs and streams | Workflow/job tests cover blocked pipes, active filesystem handles, child faults, Ctrl+C and session restart |
| Disk failures | ATA/FAT tests inject controller errors, timeouts, allocation failures, torn writes and failed flushes |
| Persistence | Production shell and host FAT tools compare exact bytes across separate QEMU boots |
| Console and recovery | Terminal restoration, userspace Snake, scrollback/editing, diskless/rejected volumes and recovery resource counts |
| Packaging | Exact archive membership/bytes, no mutable disk in `rum.zip`, extracted-ISO boots at 16/64/256/1152 MiB, then native host checks |
| Application platform | An exported SDK builds a larger ELF outside the repository; concurrent heap/file workers, exhaustion, protected buffers and fault/cancellation cleanup pass through both boot paths |

Timed ATA checks include slow flushes beyond the former poll-count budget,
clock wraparound and a stopped clock. Every wait remains bounded; no storage
failure is retried silently or hidden to make a release check pass.

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
It also verifies the validated SDK manifest and recreates the SDK archive with
native Python, comparing every member with the clean build's SDK. The SDK stays
a separate download and contains no ISO, mutable disk or game data.
It checks pipelines, file tools, job cancellation/fault/restart, userspace Snake,
recovery and resource cleanup, then reboots the same temporary disk to recover
the saved text and launch the copied application. It also audits FAT ownership
and verifies that the pristine source disk's hash has not changed.
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

## 0.6.0 validation record

Validated November 8, 2026. The clean code/test snapshot is
`dc09f0ef6c1735132d4546cebad94a72e6848438`; subsequent checklist and evidence
updates change documentation only.

- All 24 regression groups passed with four build jobs and four guest workers.
  The test phase took 121.05 seconds; the complete clean check took 162.5 seconds
  including build, preparation and artifact copying. Timings depend on the host.
- ELF and GRUB boot paths passed, including supported 16/64/256/1152 MiB boot
  checks and extracted-package ISO boots. Mixed process/I/O, resource rollback,
  storage failure injection, user Snake, shell editing and file tools passed.
- Ubuntu/WSL Linux and native Windows packaging passed with the same extracted
  ISO. Both hosts exercised workflows, cancellation, faults, shell restart,
  user Snake and cleanup, then recovered the saved file and copied application
  after a separate boot. The PowerShell launcher checks passed.
- A fresh VM download was built from program assets compared with the pristine
  validated disk. Raw/VMDK and raw/VDI comparisons were identical. This checks
  converted image contents; it does not claim a native VMware/VirtualBox test run.

Validated ISO SHA256:

```text
ea77ab96a764060b55ffae56dddb342185390d301abb18d24c41c6c07c5a9041
```

Local evidence is in
`build/release-check/dc09f0ef6c17-20261108-202155/`: the clean `result.json`,
suite logs/timings, `host-linux/result.json`, `host-windows/result.json`, and
`vm-images.log`. The pristine system disk hash is recorded in the clean report
and verified by the host checks; guests use temporary copies.

`host-windows/rum.zip` and `host-linux/rum.zip` contain the standalone ISO,
README and documentation. `vm-images/rum-vm-images.zip` is the separate fresh
system-disk bundle. Neither archive contains a personal or previously used disk.
