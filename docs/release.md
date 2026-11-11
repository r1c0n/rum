# Building a release

`rum.zip` contains the ISO, README and documentation. The ISO provides the
kernel and recovery console. Normal startup also needs a prepared FAT16 disk
containing `/RUM` programs. The separate `rum-vm-images.zip` contains a fresh
installed disk in raw, VMDK and VDI formats, the ISO and a VMware preset. Never
publish a disk that has been used for personal files or testing.

## Validate a committed build

In Ubuntu/WSL, from a clean committed checkout:

```sh
python3 scripts/release-check.py --jobs 4 --workers 4 --compat-sdk /path/to/0.7.0/rum-sdk.zip
```

The checker copies tracked sources to a new temporary directory, builds with
bounded parallelism and runs the complete host/QEMU suite. Tests validate
persistent root files across separate boots, read-only/malformed/full disks,
process isolation, cancellation, cleanup and injected storage failures.
The preserved 0.7.0 SDK is required for this release's compatibility check;
its manifest must identify the clean released source commit. An unchanged
external application is compiled with that SDK, run through both boot paths,
and checked for exact file bytes, fault recovery, cancellation and cleanup.
The input archive hash and resulting ELF/provenance are saved with the report.
The default archive path is `build/releases/0.7.0-110836a/rum-sdk.zip`.
The production-shell scenario pipes binary ELF bytes to a file, appends quoted
text, copies and renames files, cancels a blocked pipeline, collects a child
fault and restarts the shell with an active job. Every resource ledger returns
to its recovery baseline. A separate boot reads identical saved bytes and runs
the copied application from `/APPS`.
The exact ISO extracted from the package is booted too. No caller's disk is
used and the original build directory is not cleaned.

Results are saved under `build/release-check/<commit>-<time>/`: `result.json`,
logs, guest diagnostics, kernel, ISO, SDK, package, a pristine `rum-system.raw`
and `vm-images/rum-vm-images.zip`. The VM bundle is built from the same clean
program assets and ISO, with raw/VMDK/VDI contents compared by `qemu-img`.
The report records ISO, SDK, system-disk and VM-archive SHA256 values. Only a report with
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
| Object authority | Local count packets, typed aliases, stale tokens, wrong types, access denial, explicit child grants, retained process status and final-owner cleanup |
| System maintenance | Real FAT protection and recovery installs survive a separate boot with exact guest/host bytes; both kernel and userspace power controls preserve cleanup |

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

The clean release check prepares the VM download automatically. To create
another fresh bundle after building the intended source commit:

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

## 0.8.0 validation record

Validated November 11, 2026, from clean versioned source snapshot
`a0579a720a98f5503f8faf9fa80c8edcaf07f0b0`. Subsequent checklist and evidence
updates change README/docs only.

- All 27 regression groups passed with four build jobs and four guest workers.
  The suite took 142.12 seconds; the complete clean check took 201.0 seconds,
  including a fresh system disk and VM downloads. Timings depend on the host.
- Caller-local resource packets passed cross-page, read-only, malformed-input,
  table-exhaustion, alias, stale-token, type and access-right checks. Mixed
  file/process/pipe workloads, blocked I/O, faults, cancellation and failed
  construction restored every object/reference, handle, buffer and page ledger.
- The SDK archived with 0.7.0 built an unchanged external application using its
  released headers, startup and runtime. ELF/16 MiB and ISO/64 MiB runs recovered
  identical file bytes and passed fault/cancellation cleanup. Its original SDK
  source is `110836a41e60823015f2de6caf594afc03f06471`; the input archive hash
  and compiled application's provenance are preserved in the clean report.
- Protected `/RUM` mutations were denied, while trusted recovery installed a
  65,537-byte system file and recovered exact bytes after a separate boot.
  Userspace and recovery power controls, repeated intentional shell restarts,
  crash fallback, the recovery banner and diskless/read-only shutdown passed.
- Both boot paths and the supported 16/64/256/1152 MiB boot configurations passed.
  The exact packaged ISO passed native Windows and Linux packaging, SDK payload
  comparisons, pipelines/jobs, Snake, memory/file I/O and separate-boot
  persistence with zero remaining objects and references in recovery.
- Pristine raw/VMDK and raw/VDI conversions were identical. No personal or test
  disk was packaged. This does not claim a native VMware or VirtualBox run.

Validated ISO SHA256:

```text
5ac65e59eb070cd30698ea703c7c7a58ef631c0193ed1334eac90bf8dfd5ea56
```

Evidence is in `build/release-check/a0579a720a98-20261111-122728/`, including
`result.json`, suite logs/timings, `sdk07-app.json`, `host-linux/result.json`,
`host-windows/result.json` and VM conversion logs. The earlier pre-version gate
also passed from `83858593a1f374cfe6e4a0c627c7276841e3bef9` before the version
changed. Ready-to-upload downloads are collected in
`build/releases/0.8.0-a0579a7/` with checksums and validation provenance.

Publish `rum.zip`, `rum-sdk.zip` and `rum-vm-images.zip` separately. The standalone
ISO and `rum.zip` contain no mutable user disk; the VM download contains only a
fresh installed system disk.

## 0.7.0 validation record

Validated November 9, 2026, from clean source snapshot
`110836a41e60823015f2de6caf594afc03f06471`. Subsequent checklist and evidence
updates change README/docs only.

- All 25 regression groups passed with four build jobs and four guest workers.
  The suite took 116.23 seconds; the complete clean check took 167.5 seconds,
  including the build, fresh disk/VM downloads and artifact collection.
- Both boot paths passed, with boot and extracted-package ISO checks at
  16/64/256/1152 MiB. The external SDK workload combined a larger ELF, private
  BSS, concurrent heap/file operations, recoverable exhaustion, protected
  cross-page buffers, guard faults and cancellation. Resource ledgers returned
  to baseline, and host FAT checks recovered identical file bytes.
- Ubuntu/WSL and native Windows SDK exports matched every payload byte and
  manifest hash. Native release packages booted the exact validated ISO on
  both hosts, exercised heap/file I/O, pipelines, faults, Ctrl+C, shell restart
  and Snake, then recovered saved files after a separate boot. PowerShell
  launcher checks also passed.
- Diskless, malformed, full and read-only volumes and injected block/FAT
  failures preserved recovery. Raw/VMDK and raw/VDI conversions were identical.
  Conversion checks do not claim a native VMware or VirtualBox test run.

Validated ISO SHA256:

```text
b9939dfac5adf6369d0c0c9e5268c15985a1dbae42353adacf23120b4ac9fd35
```

Evidence and pristine release files are in
`build/release-check/110836a41e60-20261109-231033/`: `result.json`, suite logs,
`host-linux/result.json`, `host-windows/result.json` and `vm-images.log`.
The report records the SDK, VM download and untouched system-disk hashes.
Publish `rum.zip`, `rum-sdk.zip` and `vm-images/rum-vm-images.zip` as separate
downloads. The standalone package contains no mutable disk or game assets.

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
