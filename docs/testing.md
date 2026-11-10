# Testing rum

Run `./rum test -j4 --workers 4` in Ubuntu/WSL, or
`.\rum.ps1 test -Jobs 4 -Workers 4` on Windows. The suite
builds its kernels and programs once, runs host checks and creates fixtures,
then runs independent guest suites with four workers. Each worker runs at most
one QEMU instance. Use `--workers 2` / `-Workers 2` on a slower
machine. The allowed worker range is 1–4. Build jobs and guest workers are
separate settings. `make -j4 test TEST_JOBS=4` remains supported.

## Coverage

The default command runs all distinct regression scenarios. There is no hidden
longer release-only tier. Focused `make test-*` targets remain available when
changing one subsystem; see the relevant subsystem documentation.

Use `./rum test --suite host` or `.\rum.ps1 test -Suite host` for host checks.
Other groups include `user`, `package`, `shell`, `storage`, `block`, `fat16`,
`fat16-write`, `filesystem`, `preemption`, `lifecycle`, `sleep`, `console` and
`integration`, `handles`, `pipes`, `workflows`, `jobs`, `terminal`, `editing`
and `file-tools`.
Use `--suite vm` / `-Suite vm` for private-region host checks, paging transactions
and the allocation/protection/release syscall lifecycle through both boot paths.
Cases include cross-page buffers, read-only faults, retained blocking I/O,
permission restoration and cancellation cleanup. These
cases also run in the normal smoke/lifecycle groups of the full suite.
Run `help` for the full option list.

`make -j4 test-shell-tools-host` runs the fast command-stream and pager checks.
The `editing` group also exercises the real help/file viewers, prompt/listing
colors, navigation, Ctrl+C recovery, cross-page styled-line validation and
byte-exact redirected tool output on disposable disks through both boot paths.

`./rum.ps1 test -Suite runtime -Jobs 4` (Linux: `./rum test --suite runtime`)
runs focused heap/runtime host checks and real userspace tests through ELF
boot at 16 MiB and ISO boot at 64 MiB. Coverage includes fragmented reuse,
growth/shrink preservation, recoverable exhaustion, concurrent private heaps,
file streams, clock packets and page/handle cleanup after exit, fault and
cancellation. `make test-runtime-host` runs only the fast host portion.

Use `--suite executables` / `-Suite executables` for ELF validation and larger
program loading. Host checks inject failures at every read, copy, protection and
allocation stage. Guest checks cover larger LOADs, large BSS, inherited-handle
rollback, parent offsets, physical exhaustion and fault/cancellation cleanup
through both boot paths. `make test-executable-host` runs only the host portion.
The shell group additionally loads the large fixture from a real FAT16 disk.

Use `--suite sdk` / `-Suite sdk` for standalone SDK export, manifest validation,
an external project build and real application execution through both boot
paths. Normal tests are offline. The optional [Doom probe](doom-compatibility.md)
compiles pinned upstream sources and runs its zone/WAD backend separately.
The full suite includes the SDK host check and a bounded SDK guest group.
That group also builds a larger mixed-workload ELF outside the repository,
runs concurrent heap/file workers with bounded memory pressure, exhausts and
recovers allocations, rejects protected cross-page output buffers, and cleans
up live file/heap/pipe resources after user faults and cancellation.

* Portable host tests cover memory, paths, handles, ABI layouts, terminal and
  keyboard rules, sessions, recovery commands, Snake and the ATA port model.
  Guest Snake checks cover rendering, pause/timer behavior and cleanup; gameplay
  rules run in deterministic host tests instead of a timed QEMU food chase.
* FAT host tests retain all malformed images, cluster sizes, full-media cases,
  allocation failures and interrupted write stages. Writes test five transfer
  extents; flushes test one I/O failure because they have no transfer buffer.
  Timeout and device-fault results are checked separately.
* Hardware/CPU/paging smoke fixtures run once at 64 MiB. Recovery-console tests
  use both loaders. Basic boot checks still exercise **both ELF and GRUB ISO
  at 16, 64, 256 and 1152 MiB**.
* Preemption, lifecycle, sleeps, console sessions, process tools and mixed
  integration workloads use **ELF at 16 MiB and ISO at 64 MiB**, rather than
  repeating every assertion across a four-way Cartesian product.
* Block, FAT and file-syscall fixtures run at 16 MiB. Production shell tests
  cover the persistent root, installed programs, both loaders, child faults/cancellation and recovery.
* Storage integration covers persistence across separate boots, malformed,
  missing, read-only and full disks, and injected read/write/flush errors on
  the representative ELF/16 MiB and ISO/64 MiB configurations.
* Packaging runs once. Storage integration boots the exact extracted archive
  ISO at 16, 64, 256 and 1152 MiB. `release-check.py` uses this same suite.

Repeated RAM/loader combinations, duplicate host executions, repeated flush
transfer extents and a second release packaging/boot pass were removed. The
suite keeps distinct host and guest checks: a host mock cannot establish that
real interrupts, privilege transitions or ATA commands work in the guest.

Keyboard tests send real PS/2 press/release events through QMP and wait for
observable guest output. A 5 ms interval lets the emulated keyboard drain;
the old 70 ms key-hold delay is gone. Full-suite FAT
fixtures live in a temporary host directory to avoid WSL Windows-drive latency.
Focused FAT targets keep their usual `build/tests/fat16/` fixtures.

## Results and timings

`build/test-artifacts/suite/result.json` records success, worker count, elapsed
test time and every suite's exit status and duration, sorted by duration.
Adjacent `.log` files contain complete output for each suite. Individual
guest logs and memory/disk artifacts remain in their subsystem directories.
The elapsed suite time excludes the initial build; failed host preparation
stops before guests run, while independent guest failures are collected.

Shared builds, FAT fixture generation and archive creation finish before the
guest workers start. Each guest uses its own temporary writable disk and QMP
socket; tests never use a caller's disk image. Do not launch two complete test
runs in the same checkout at once, since their build outputs and reports are
shared. Run the Windows launcher checks separately with
`tests/disk-launcher-test.ps1` when changing PowerShell argument handling.
Shared workflow validation and disk-preserving cleanup are covered by the
portable `tests/build-environment-test.py` host test.
