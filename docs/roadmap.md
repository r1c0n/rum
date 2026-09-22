# rum roadmap

rum provides protected foreground processes with isolated address spaces,
ring-3 execution, a versioned syscall ABI, static ELF loading, fault recovery,
cleanup, and Ctrl+C cancellation. The 0.4.0 release builds on that foundation
with persistent storage, file handles, and a userspace shell.

The 32-bit x86 BIOS target, GRUB Multiboot boot path and VGA text interface
remain supported. The kernel owns hardware, memory protection, scheduling and
filesystems. User programs use versioned syscalls and never access kernel memory,
VGA memory or hardware ports directly.

Each phase must keep the host tests, GRUB ISO boot, direct ELF boot, kernel fault
fixtures, IRQ tests, paging tests, shell, foreground programs, and Snake working.
Add focused failure and cleanup tests with every new owner or resource type.

## 0.4.0 — persistent files and a userspace shell

The release goal is a userspace shell that launches programs and reads and
writes a FAT16-backed `/disk`. Files must survive separate QEMU runs. rum must
also boot cleanly without a disk and retain its RAM filesystem and kernel
recovery shell.

### 1. Block-device interface and ATA PIO

- [ ] Define a kernel block-device API with sector size/count, read, write and
  flush operations plus explicit error results.
- [ ] Add an ATA PIO driver for an optional secondary IDE disk in QEMU.
- [ ] Identify device capabilities and validate LBA/count ranges without integer
  overflow before issuing commands.
- [ ] Use bounded status polling with timeout, device-fault and error-register
  reporting; never wait forever for missing or broken hardware.
- [ ] Serialize requests and keep port I/O inside the kernel.
- [ ] Add launch-script support for an explicitly supplied raw disk image.
- [ ] Add a separate command that creates a disposable test image; normal boot
  must never format, truncate or replace an existing image.

Host-driven tests should write recognizable sector patterns, read them through
the guest and compare exact bytes. Cover first/last sectors, zero sectors, ranges
past the end, arithmetic overflow, missing disks, read-only images, controller
errors and timeout paths. Canary sectors outside each request must not change.

### 2. Filesystem and path layer

- [ ] Introduce common filesystem operations for RAM and disk backends.
- [ ] Resolve absolute and relative paths through one bounded parser.
- [ ] Support directories, `.`, `..`, repeated separators and a per-process
  working directory without escaping a mounted root.
- [ ] Mount the persistent volume at `/disk` while preserving embedded RAM files
  in the existing root namespace.
- [ ] Define path, component and directory-depth limits in public/private headers.
- [ ] Define backend naming, access-mode and error translation rules.
- [ ] Track open objects by backend identity so removal and replacement have
  explicit behavior while handles remain open.

Reject invalid bytes, overlong paths/components, arithmetic overflow and mount
traversal before calling a backend. RAM filenames keep their existing rules.
FAT begins with uppercase-compatible 8.3 names; long filename support remains
outside 0.4.0.

Test root and nested traversal, equivalent normalized paths, missing components,
file/directory confusion, maximum lengths, mount boundaries and operations on
both backends. Existing RAM-file commands must retain their bytes and limits.

### 3. Read-only FAT16

- [ ] Mount a small unpartitioned FAT16 image and validate its BPB and derived
  region sizes against the block-device bounds.
- [ ] Read both FAT copies, the fixed root directory, subdirectories and regular
  files with bounded cluster-chain traversal.
- [ ] Detect invalid/reserved clusters, premature end markers, loops, chains that
  exceed file size and directory entries outside the volume.
- [ ] Decode valid 8.3 names and ignore deleted, volume-label and unsupported
  long-name entries safely.
- [ ] Expose directory iteration and random/sequential file reads through the
  common filesystem layer.

Use host FAT tools to build known images, then compare guest listings and bytes.
Add malformed-image fixtures for invalid geometry, overlapping regions, bad
cluster sizes, cyclic chains, truncated directories and out-of-range clusters.
A rejected volume must leave rum usable with RAM files.

### 4. Writable FAT16

- [ ] Allocate and free clusters consistently in every FAT copy.
- [ ] Create, replace, extend, truncate and delete regular files.
- [ ] Create and remove directories, including `.` and `..` entries and nonempty
  directory checks.
- [ ] Reuse deleted directory slots and clusters without cross-linking files.
- [ ] Flush file data, FAT changes and directory metadata in a documented order.
- [ ] Report full media, read-only devices and partial I/O failures without
  corrupting in-memory ownership state.

Define the interrupted-write guarantee honestly. 0.4.0 does not need journaling
or general crash recovery, but it must never write outside the volume. Prefer an
ordering that makes newly allocated data reachable only after its contents and
FAT chain are written, and document cases that may require host repair.

Test empty and multi-cluster files, boundary-sized writes, replacement, truncate,
delete/recreate, directory growth, full media, cluster reuse and injected failures
at each write stage. Validate the resulting image with host FAT tools after QEMU
exits.

### 5. File handles and filesystem syscalls

- [ ] Extend the public ABI with open, close, seek, directory listing and path or
  working-directory operations, using fixed-width versioned structures.
- [ ] Give each process a bounded handle table with access mode, offset and a
  referenced backend object; reserve handles 0–2 for standard streams.
- [ ] Route `read` and `write` through handles with defined partial-operation and
  end-of-file behavior.
- [ ] Validate every user buffer, path, structure, flag, handle and offset before
  touching a driver or filesystem.
- [ ] Close every handle and release backend references on exit, user fault,
  cancellation and partial process construction.
- [ ] Block filesystem work only in foreground/task context and never while an IRQ
  or short interrupt-protected update is active.

Tests should combine invalid handles, modes and flags with cross-page user buffers,
large offsets, seek overflow, short I/O, removal of open files and exhaustion of
the 32-handle process limit. Repeated process exit must return handle/object counts
to their baseline.

### 6. Userspace shell and tools

- [ ] Build a userspace shell with the same separate toolchain and public headers
  as every other program.
- [ ] Start it as the initial foreground process after kernel initialization.
- [ ] Keep a kernel recovery shell available if the userspace shell cannot load or
  exits repeatedly.
- [ ] Move command parsing and `echo`, `cat`, `ls`, `pwd` and `cd` behavior into
  user programs using only syscalls.
- [ ] Launch another ELF as a foreground child and wait for its exit status.
- [ ] Transfer console input to the child and restore it after exit, fault or Ctrl+C.
- [ ] Report filesystem and process errors without exposing kernel addresses or
  internal error values.

The userspace shell may keep commands as built-ins initially; separate tool ELFs
can follow when the launch interface is stable. Background jobs, pipelines,
redirection, quoting, history and completion remain future work. Snake may stay
as a kernel application for this release and later move through the same public
interfaces.

Test normal commands, paths on both mounts, empty/binary files, failed launches,
child statuses, child faults, CPU-bound cancellation and shell restart. The shell
must not access VGA, keyboard queues, RAMFS nodes, ATA ports or kernel pointers
directly.

### 7. 0.4.0 integration and release

- [ ] Boot with no disk, a valid disk, a read-only disk, a malformed disk and an
  image that becomes full.
- [ ] Verify process isolation and resource cleanup while file handles and disk
  requests are active.
- [ ] Write `/disk/notes.txt`, shut down QEMU, boot again and recover identical
  bytes through both the userspace shell and host FAT tools.
- [ ] Inject block and filesystem failures and confirm the shell, RAM files,
  diagnostics and kernel recovery path remain usable.
- [ ] Run the complete host and QEMU suite through both boot paths and supported
  RAM sizes.
- [ ] Document image creation, QEMU attachment, mounts, paths, FAT limitations,
  syscall structures, flush behavior and recovery expectations.
- [ ] Verify packaging on Windows and Linux. Keep the ISO standalone and do not
  bundle a mutable user disk unless a release explicitly calls for one.

0.4.0 is ready when the userspace shell can launch a program, create and read
`/disk/notes.txt`, survive a child fault or Ctrl+C, and recover the same file
after a separate QEMU boot. Booting with no disk must still provide the kernel
recovery shell, RAM files, diagnostics and Snake.

## Delivery order

Use one focused branch and pull request for each numbered phase, for example
`0.3/user-address-spaces`, `0.3/syscalls`, `0.4/ata-pio` and `0.4/fat16-write`.
Keep each phase buildable and testable before starting the next dependency.
Integration branches collect completed work; they should not hide unrelated
features in one final commit.

Before either release, rerun the full suite from a clean build, review the saved
serial/QMP artifacts and ownership ledgers, build `rum.zip`, and boot the exact
packaged ISO. Update the ABI version only when a public contract changes, and
keep the previous contract documented when existing binaries remain supported.
