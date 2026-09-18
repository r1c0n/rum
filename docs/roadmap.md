# rum 0.2.0 roadmap

The next release lays the foundations for user processes and persistent files.
The first target is a small program running in ring 3 that prints through a
syscall and exits without affecting the kernel. From there, add disk storage
and move the shell and basic tools into userspace.

rum keeps its 32-bit x86 BIOS target and VGA text interface. The kernel owns
hardware drivers, memory protection, process management, and filesystems.
User programs access those services through syscalls.

## Preparation

Record the current passing tests and memory usage first, then complete these
foundations before entering user mode. Each change should keep the kernel shell,
Snake, and both boot paths working.

### P1. Memory layout and resource ownership

- [x] Define shared address-range constants for the kernel, identity window, heap,
  kernel stacks, user programs, and user stacks.
- [x] Keep the existing boot layout and reserve non-overlapping user/stack ranges.
- [x] Define ownership of private frames, shared tables, stacks, and process records,
  including cleanup after partial initialization.

The kernel is loaded at 2 MiB, identity maps physical memory below 1 GiB, and
reserves `0x40000000`–`0x403fffff` for its heap. User mappings must fit around
these existing ranges. Set initial bounds for process count, stack sizes, user
memory, arguments, and file handles. Allocate user payloads directly as physical
pages; the kernel heap holds metadata rather than program payloads.

The shared constants and initial policy are in `include/rum/memory_layout.h`
and `include/rum/process_limits.h`. [Memory layout and ownership](memory-layout.md)
records the ranges, cleanup contract, and passing test/memory baseline. Process
allocation and runtime quota enforcement belong to the later implementation.

### P2. Paging contexts and shared kernel mappings

- [x] Refactor paging operations to accept an explicit address-space object instead
  of relying on one global directory.
- [x] Distinguish the kernel directory from the currently active CR3 and provide
  a controlled switch that updates bookkeeping and cached translations.
- [x] Keep kernel code, CPU tables, stacks, and heap mappings available in every
  address space, with supervisor-only permissions.
- [x] Make later kernel/heap growth visible in all existing address spaces.

Paging now uses registered `struct paging_space` handles. New directories borrow
supervisor-only kernel tables, including the heap and boot stack. Kernel table
creation and removal propagate to every live space; switching updates CR3 and
active-space bookkeeping. Destruction releases only an inactive directory and
its metadata, preserving shared kernel resources.

Tests switch between two directories, grow the heap afterwards, publish and
retire kernel tables, return from live timer IRQs, and recover from limits and
allocation failure. Null and code/constant protection are also tested under
child CR3s. [Address spaces](memory.md#address-spaces) documents the API. Private
user mappings remain part of the process address-space implementation below.

### P3. CPU entry and descriptor preparation

- [ ] Put mutable GDT/TSS storage in writable kernel memory and define their layouts
  with size/offset assertions.
- [ ] Refactor common interrupt entry/return code while preserving the current
  register, segment, direction-flag, stack-alignment, and EOI guarantees.
- [ ] Define how frames distinguish ring-0 entries from entries carrying user SS/ESP.
- [ ] Specify initial user EFLAGS and port-I/O permissions, and an FPU/SIMD policy.

The GDT currently lives in read-only data. Loading a TSS with `ltr` updates its
descriptor's busy flag, so writable descriptor storage must be ready first.
See [Intel's system programming manual](https://www.intel.com/content/dam/support/us/en/documents/processors/pentium4/sb/25366821.pdf),
sections 6.2 and 9.8.4.

Start user programs with interrupts enabled and IOPL zero; do not carry kernel
flags into an unchecked user frame. Keep the first user ABI integer-only and
reject unsupported floating-point/SIMD use until context preservation exists.
Compile flags alone do not enforce that policy on loaded programs.

### P4. Kernel contexts, stacks, and waiting

- [ ] Introduce a current-task record, saved kernel context, and a kernel idle context.
- [ ] Allocate private kernel stacks and prove context switches using ring-0 test tasks.
- [ ] Define runnable, blocked, and exited states with wait/wakeup operations that
  cannot lose an event between checking a queue and sleeping.
- [ ] Keep short interrupt-protected updates separate from blocking or device waits.
- [ ] Defer freeing an exited task's active stack and address space until execution
  has switched to a surviving kernel context.

This supplies the recovery path needed when a user program exits or faults,
and the blocking path needed for console reads and parent waits. Handlers should
queue events and wake tasks; they must not allocate, block, or reclaim the
currently executing task. Preserve the existing interrupt-safe idle behavior.

If kernel stacks get guard pages, add an independent double-fault stack/entry
path at the same time so overflow does not depend on the failed stack. Test
stack switching, event delivery at sleep boundaries, idle wakeups, and deferred
cleanup before adding user contexts. General timer preemption can come later.

### P5. User ABI and build foundation

- [ ] Create shared ABI headers with fixed-width syscall types, error values, and
  bounded argument structures, separate from private kernel headers.
- [ ] Add a `user/` source tree, separate compile/link rules, a user linker script, startup code,
  and build directories using the existing cross-toolchain.
- [ ] Define static ELF32 support and the initial stack/argument convention.
- [ ] Check that executable assets fit the embedding/runtime limits and keep
  symbol-rich debugging artifacts separately when stripping embedded binaries.

The asset generator and RAM filesystem currently limit each file to 64 KiB.
Keep initial programs within that limit or deliberately update both layers and
their tests before embedding larger files. User binaries must not link host
startup code, a host C library, or kernel implementation objects.

### P6. Foundation verification and diagnostics

- [ ] Verify host, GRUB, direct ELF, kernel-fault, IRQ, paging, and heap/file checks
  after preparation, rerunning relevant checks during each refactor.
- [ ] Extend CPU-table and frame tests for intentional layout changes while retaining
  checks for permissions, register restoration, and kernel panic behavior.
- [ ] Add diagnostics for the active task, CR3, kernel stack, and owned resources.
- [ ] Add repeatable allocation-failure and context-switch fixtures for the new paths.

Use memory/frame counts to catch leaks after failed setup and repeated switching.
Keep fault tests isolated so malformed frames or programs cannot turn the whole
test run into an unexplained hang. Preserve serial logs and QEMU artifacts.

## Planned work

After preparation, work through these items in order. Add targeted tests alongside
each feature; release integration gathers the completed checks.

### 1. User address spaces

- [ ] Create and destroy a page directory for each process.
- [ ] Share kernel mappings as supervisor-only pages and reserve a separate user range.
- [ ] Map private program data and a user stack with an unmapped guard page.
- [ ] Zero private frames, including stack and segment padding, before exposing them.
- [ ] Add bounded helpers for checking and copying user buffers.

Keep kernel code, heap, physical identity mappings, and page tables inaccessible
from ring 3. Test page ownership, cross-page buffers, address overflow, failed
allocation rollback, and complete teardown. Two address spaces must be able to
use the same user virtual address without sharing private data.

### 2. Ring 3 and exception handling

- [ ] Add user code/data descriptors and a TSS to the GDT.
- [ ] Provide a kernel stack for entry from each process and keep port I/O privileged.
- [ ] Load the TSS and update its ring-0 stack pointer when the current task changes.
- [ ] Enter user mode with `iret` and preserve timer/keyboard delivery.
- [ ] Handle privilege-changing interrupt frames, including saved user ESP and SS.
- [ ] Separate user faults from kernel panics.

A user null access, kernel-memory access, or privileged instruction must terminate
the offending process and leave rum usable. Kernel faults must retain their
panic diagnostics. Update CPU-table tests for the new descriptors and verify
interrupt return from both privilege levels.

### 3. Syscall interface

- [ ] Add an `int 0x80` entry callable from ring 3 with a documented register ABI.
- [ ] Start with console input/output, process exit, and process identification.
- [ ] Validate user pointers, lengths, syscall numbers, and return errors consistently.
- [ ] Block for keyboard input without spinning or holding interrupts disabled.

Use the prepared task and wait/wakeup paths; syscall entry is not permission to
block while holding an interrupt-protected resource. Define partial-read/write
behavior and distinguish an invalid user buffer from a kernel implementation fault.

User programs must not access VGA memory, kernel pointers, or hardware ports
directly. Test invalid calls and buffers, including buffers spanning unmapped
or read-only pages, while keeping the kernel alive and resources accounted for.

### 4. ELF programs and process lifetime

- [ ] Load static ELF32 executables built with the existing `i686-elf` toolchain.
- [ ] Validate load segments, entry points, permissions, sizes, and address ranges.
- [ ] Zero BSS and build the initial user stack with bounded arguments.
- [ ] Extend the task records with the program image, user context, and exit status.
- [ ] Launch an embedded `hello` program from the existing kernel shell.

Start with one foreground program. On exit or a user fault, release its pages
and kernel stack and return to the shell. Repeated launches must not leak memory.
Reject malformed executables before entering user mode.

Add small C syscall wrappers and the first program under the prepared `user/`
tree. Load the first binaries from embedded RAM files.

### 5. Disk I/O

- [ ] Add an ATA PIO block driver for a secondary IDE disk in QEMU.
- [ ] Validate sector ranges and use bounded waits with reported device errors.
- [ ] Add an optional disk-image argument to the launch scripts.
- [ ] Provide a separate command to create a disposable test image.

Check reads and writes against host-side bytes. Missing disks must leave rum
usable with RAM files. Normal boot must never format or reset an existing image.
Drivers stay in the kernel; user programs receive file access through syscalls.

### 6. Filesystems and persistent files

- [ ] Introduce common filesystem operations for RAM and disk backends.
- [ ] Add directories, absolute/relative paths, and a working directory.
- [ ] Mount a small, unpartitioned FAT16 image at `/disk`, initially read-only.
- [ ] Add file and directory creation, replacement, deletion, and write flushing.
- [ ] Add per-process file handles and file/directory syscalls.
- [ ] Define backend naming rules, file/path limits, access modes, and error mapping.

Start disk filenames with FAT's 8.3 format and defer long-name support. Preserve
the existing RAM filename rules. Add volume bounds checks and bounded cluster-chain
traversal before writing files, including detection of loops and invalid clusters.

Keep embedded files at the RAM root. Begin file syscalls with open, read, write,
close, seek, and directory listing; extend them for path and directory operations.
Check handles, access modes, offsets, paths, and user buffers. Process exit must
close its handles. Direct block-device access remains a kernel operation.

Files under `/disk` must survive separate QEMU runs and be readable with host
FAT tools. Test invalid volumes, full disks, cluster reuse, and I/O failures.
Document interrupted-write behavior; crash recovery is outside this release's
scope. RAM files remain temporary.

### 7. Userspace shell and tools

- [ ] Add foreground child launch and wait so a shell can run another executable.
- [ ] Start a userspace shell at boot, with a kernel recovery shell available.
- [ ] Move command parsing and basic tools such as `echo`, `cat`, and `ls` into `user/`.
- [ ] Use syscalls for console access, paths, and files instead of kernel internals.
- [ ] Allow Ctrl+C to interrupt a foreground child, including a CPU-bound loop,
  and return control to its parent.

Keep the initial process model small: one foreground child at a time, with its
parent waiting. Transfer console input to the child and restore it on exit.
Extend keyboard control-key decoding and defer cancellation to a safe kernel
return boundary, using the prepared context-switch and cleanup paths.
Test normal exit, user faults, failed launches, and cleanup without losing the
parent shell. Background jobs and a general preemptive scheduler can follow
after this path is reliable.

Shell history, a text editor, and moving Snake to userspace follow the process
and syscall foundations. They should use the same interfaces as other programs.

### 8. Release integration

- [ ] Extend the suite with ring-3, syscall, ELF, process, and filesystem tests.
- [ ] Run programs that deliberately fault or pass invalid syscall arguments.
- [ ] Verify process isolation, repeated launch/exit cleanup, and parent recovery.
- [ ] Check foreground cancellation and resource limits as well as fault recovery.
- [ ] Test disk persistence across two separate QEMU runs and boots without a disk.
- [ ] Document the user ABI, executable build process, and disk-image commands.
- [ ] Verify packaging on Windows and Linux; keep the ISO usable on its own.

The release is ready when the userspace shell can launch a program, read and
write `/disk/notes.txt`, survive a child fault, and recover those bytes after a
restart. Kernel memory, exception, and IRQ checks must still pass.
