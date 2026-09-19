# rum roadmap

rum 0.2.0 provides the kernel foundation for protected processes: explicit
address spaces, cooperative kernel tasks, per-task kernel stacks, a user ABI
and build, and ownership diagnostics. The next two releases turn that work into
a usable userspace and then add persistent storage.

The 32-bit x86 BIOS target, GRUB Multiboot boot path and VGA text interface
remain supported. The kernel owns hardware, memory protection, scheduling and
filesystems. User programs use versioned syscalls and never access kernel memory,
VGA memory or hardware ports directly.

Each phase must keep the host tests, GRUB ISO boot, direct ELF boot, kernel fault
fixtures, IRQ tests, paging tests, shell and Snake working. Add focused failure
and cleanup tests with every new owner or resource type.

## 0.3.0 — protected user processes

The release goal is a real foreground process running an embedded static ELF in
ring 3. It must print through production syscalls, return an exit status and
release all of its resources. A bad program must terminate without taking down
the kernel shell or another process.

### 1. User address spaces

- [x] Add ownership-aware operations for mapping, protecting and removing private
  user pages in a registered paging space.
- [x] Keep every shared kernel mapping supervisor-only in every process directory.
- [x] Map program segments within `RUM_USER_BASE`–`RUM_USER_PROGRAM_END` and a
  private 64 KiB stack below `RUM_USER_STACK_TOP`.
- [x] Leave `RUM_USER_STACK_GUARD_BASE` unmapped and keep page zero unmapped.
- [x] Enforce the per-process mapped-page budget before committing any mapping.
- [x] Zero each new physical frame, including segment padding, BSS and unused
  stack bytes, before making it visible to ring 3.
- [x] Add checked copy-in, copy-out and string-copy helpers for user ranges.
- [x] Roll back only pages acquired by the failed operation and leave existing
  mappings unchanged.

User mappings need independent read/write and user/supervisor permissions.
The i386 target has no NX support, so the ELF validator must continue rejecting
writable executable segments even though hardware cannot enforce execute denial.
Private frames belong to one address space even when two processes use the same
virtual address.

Tests should cover cross-page buffers, zero-length ranges, address and size
overflow, guard pages, read-only destinations, holes, the last permitted byte,
partial allocation failure and complete teardown. Two address spaces must map
the same user virtual address to different physical bytes. QEMU must inspect the
real page tables and PMM bitmap rather than relying only on paging API results.

### 2. Kernel entry stacks and process records

- [ ] Move task kernel stacks into the reserved virtual stack slots and leave an
  unmapped guard page between slots.
- [ ] Add a dedicated double-fault entry stack so stack exhaustion produces a
  controlled report instead of a reset or unexplained hang.
- [ ] Extend task records with a positive process ID, parent, address space,
  trusted user frame, exit status and resource accounting.
- [ ] Keep kernel-only tasks distinct from user processes while sharing the same
  scheduler and wait/event machinery.
- [ ] Update TSS.ESP0 before every switch to a process and retain a valid kernel
  entry stack throughout interrupt, syscall and fault handling.
- [ ] Publish a process as runnable only after its mappings, arguments and initial
  CPU frame are complete.

Construction remains private until the final publish step. Failure unwinds the
user stack, program pages, private tables, directory, kernel stack and metadata
in reverse order. Exit and faults first switch to a surviving kernel context;
only then may the reaper free the old CR3 or active stack.

Extend CPU-table, interrupt-frame and task diagnostics tests for guarded virtual
stacks and process records. Verify TSS.ESP0, actual CR3, kernel stack bounds,
supervisor permissions and PMM ownership before and after repeated switching.
Force failure after each construction stage and require the original resource
ledger to be restored.

### 3. Ring-3 entry and fault recovery

- [ ] Build the first trusted user frame with user selectors, validated EIP/ESP
  and EFLAGS `0x202`.
- [ ] Enter ring 3 through the production interrupt-return path.
- [ ] Preserve timer and keyboard delivery while user code is running.
- [ ] Distinguish exceptions from CPL 3 from faults in kernel code.
- [ ] Convert a user exception into process termination with a recorded reason.
- [ ] Preserve the existing kernel panic path and its register/resource report.
- [ ] Deny port I/O and keep the integer-only x87/MMX/SSE policy active.

Null access, kernel-memory access, writes to read-only pages, `ud2`, privileged
instructions and invalid port I/O must terminate only the current process.
The parent kernel task must wake, observe the result and remain usable. A fault
while handling a syscall or another kernel-mode fault still follows the kernel
panic path.

Tests should enter through the real GDT/TSS/IDT path and compare the reported
fault with the original user EIP, ESP and registers. After each fault, verify
the parent context, CR3, TSS stack, task registry, page ownership and interrupt
delivery. Keep deliberately faulting programs in isolated QEMU cases.

### 4. Production syscall path

- [ ] Install vector `0x80` as a present ring-3 interrupt gate.
- [ ] Dispatch ABI v1 syscall numbers from EAX with arguments in EBX, ECX and EDX.
- [ ] Implement `exit`, `read`, `write` and `getpid` with the documented signed
  results and errors from `include/rum/abi/`.
- [ ] Preserve every general register except EAX on return to userspace.
- [ ] Validate syscall numbers, handles, pointer ranges and page permissions before
  reading or writing user memory.
- [ ] Support partial console reads/writes and zero-length operations.
- [ ] Block standard-input reads on keyboard events without polling or holding
  interrupts disabled.

The first release uses handles 0, 1 and 2 for standard input, output and error.
Copy user data through checked helpers; drivers and the console never receive a
raw user pointer. Unsupported calls return `-RUM_ENOSYS`. Invalid arguments
return a defined ABI error and do not become kernel faults.

Test every syscall at range and page boundaries, including buffers spanning two
pages, unmapped holes, read-only memory, overflowing lengths, unknown numbers
and invalid handles. Exercise short writes and blocking input while PIT IRQs and
another runnable context continue making progress.

### 5. ELF loading and initial stack

- [ ] Parse the validated static little-endian i386 ELF32 subset from a RAM file.
- [ ] Recheck type, machine, program-header bounds, load ranges, alignment,
  permissions, file sizes and entry point inside the kernel before mapping.
- [ ] Allocate distinct pages for every load segment and copy only its file bytes.
- [ ] Zero BSS, page padding and the rest of the initial user stack.
- [ ] Validate a bounded `rum_arguments` packet and build the documented
  `argc`/`argv`/empty-`envp` stack with 16-byte alignment.
- [ ] Link stripped executables into the boot RAM filesystem while retaining
  symbol-rich copies and linker maps under `build/user/debug/`.

The loader accepts no interpreter, dynamic linking, relocation, TLS or shared
library dependency. Reject overlapping segments, shared pages between segments,
writable executable input, entry points outside file-backed executable bytes and
images that exceed process or RAM-file limits. Rejection must happen before the
process becomes runnable.

Reuse the host ELF corpus and run malformed images through production loader
fixtures. QEMU should compare mapped bytes and permissions with the source ELF,
including BSS and padding, then confirm that repeated load failure and successful
exit return every private frame.

### 6. Foreground launch and process lifetime

- [ ] Add a kernel-shell launch command for an embedded program and bounded
  arguments.
- [ ] Start one foreground child, transfer console input to it and block the parent
  on a process-exit event.
- [ ] Return the child's full signed exit status or fault reason to the parent.
- [ ] Reap all program pages, private tables, directory, kernel stack and metadata
  after switching away from the child.
- [ ] Add a cancellation flag checked at safe return-to-user boundaries.
- [ ] Decode Ctrl+C and terminate a foreground child, including a CPU-bound loop,
  before restoring input to the parent shell.
- [ ] Keep failed launch, normal exit, user fault and cancellation cleanup on the
  same ownership path.

The initial model remains deliberately small: one foreground child per parent,
no background jobs and no general fork operation. Kernel tasks stay cooperative;
timer/keyboard return provides a safe boundary for observing cancellation while
user code runs.

Repeatedly launch `hello`, a nonzero-return program, a user-fault program and a
CPU-bound cancellation probe. After each run, compare task, heap, page-directory
and PMM counts with the pre-launch snapshot. The kernel shell, keyboard, timer,
RAM files and Snake must remain usable.

### 7. 0.3.0 integration and documentation

- [ ] Run host, user-ELF and all existing QEMU cases on every supported RAM size.
- [ ] Add production process cases for normal exit, every user fault class,
  invalid syscalls, invalid buffers, limits, cancellation and repeated cleanup.
- [ ] Test both GRUB ISO and direct ELF boot with and without launching a process.
- [ ] Record process/task ownership in `diag` and panic logs without exposing
  kernel pointers through the user ABI.
- [ ] Document process lifetime, user memory, syscall errors, launch syntax and
  the supported ELF subset.
- [ ] Verify the release package on Windows and Linux and keep `rum.iso` usable
  without debug artifacts.

0.3.0 is ready when the normal kernel shell can launch the embedded `hello` ELF,
the program prints only through production `write`, receives its real PID and
exits back to the shell. A deliberately faulting program and Ctrl+C must also
return control without a leaked frame, stale task or corrupted parent context.

Work outside this release includes persistent disks, filesystem handles beyond
the standard streams, a userspace shell, background jobs, general preemption,
dynamic linking and extended CPU-state preservation.

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
