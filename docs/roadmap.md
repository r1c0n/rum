# rum roadmap

rum has protected user processes, a versioned syscall ABI, persistent FAT16
storage, and a userspace shell with standalone commands. The next releases
build on those interfaces: concurrent processes first, then programs that can
work together through files and pipes.

The 32-bit x86 BIOS target, GRUB Multiboot boot path, direct ELF boot, and VGA
text interface remain supported. The kernel owns hardware, protection,
scheduling, and filesystems. Programs use public headers and syscalls; they
never access kernel memory, VGA memory, keyboard queues, or hardware ports.

Each numbered step should remain buildable and keep existing commands,
persistent files, diagnostics, fault recovery, and Snake working. Add tests
for new ownership rules and failure paths before moving to the next dependency.

## 0.5.0 — multitasking and process control

The goal is to run independent user programs without allowing a CPU-bound
program to stop the rest of the system. Introduce user-process preemption and
separate launch, wait, and termination operations. Kernel execution remains
cooperative at explicitly safe points.

### 1. Scheduling and ownership prerequisites

- [ ] Audit interrupt entry/return, syscall entry/return, context switching, and
  cancellation before allowing a timer interrupt to select another process.
- [ ] Document which task, process, filesystem, handle, console, and memory
  structures are shared, and what protects each update.
- [ ] Define safe scheduling points. Never switch in an IRQ handler, during a
  short interrupt-disabled update, while holding a lock, or while exposing
  partially constructed resources.
- [ ] Audit blocking callbacks and bounded ATA polling. Keep filesystem and
  driver operations serialized, and release ownership before a task sleeps.
- [ ] Define runnable, waiting, and exited states, scheduler limits, and the
  ownership of stacks, address spaces, working directories, and exit records.
- [ ] Preserve the existing user CPU policy, guarded stacks, and kernel fault
  behavior while introducing the new scheduling paths.

Test event wakeups racing with waits, cancellation during a syscall, and
failure partway through process construction. Add assertions for forbidden
scheduling contexts and compare ownership counts before and after cleanup.

### 2. Timer-driven user-process preemption

- [ ] Use PIT ticks to request rescheduling and define a documented time slice.
- [ ] Switch a user process at a trusted outer interrupt return to user mode;
  nested interrupts and interrupts from kernel mode only record pending work.
- [ ] Preserve the complete user register frame and update task identity, CR3,
  and TSS state together before restoring the selected context.
- [ ] Implement fair round-robin selection among runnable tasks, with an idle
  path when none are ready.
- [ ] Apply pending scheduling requests at safe kernel boundaries without
  adding arbitrary kernel preemption.
- [ ] Keep IRQ handlers bounded and allocation-free, and avoid treating every
  timer tick as a reason to wake unrelated input or filesystem waiters.

Run two CPU-bound programs that never yield and verify both make progress.
Check register preservation, private address spaces, user CPU restrictions,
stack guards, timer wraparound, and cancellation while processes alternate.
Document that bounded synchronous kernel I/O can still delay scheduling.

### 3. Separate spawn, wait, and termination interfaces

- [ ] Add fixed-width, versioned spawn, wait, and termination requests to the
  public ABI and user runtime.
- [ ] Make spawn return a process identifier after complete construction;
  failure must publish no runnable child and retain no temporary resources.
- [ ] Support waiting for a specific child and checking for completion without
  blocking. Define how a parent waits for any child as well.
- [ ] Return signed exit status, fault category, or cancellation without
  exposing kernel addresses or private process records.
- [ ] Define who may request termination. Permit child control and explicitly
  authorized shell-session control rather than arbitrary process access.
- [ ] Define orphan adoption, abandoned exit-record cleanup, identifier
  exhaustion, and rejection of stale or unrelated identifiers.
- [ ] Preserve existing foreground RUN behavior and compatible command-launch
  packets while implementing them through the new lifecycle operations.

Validate every packet, flag, path, argument, and output buffer before creating
or changing a process. Test failed loads, invalid identifiers, double waits,
parent exit, child exit before wait, and result-copy failures. An invalid wait
result buffer must not discard a child's exit record.

### 4. Sleeping and cancellable event waits

- [ ] Expose yield and bounded timed sleep through the public runtime.
- [ ] Use checked duration conversion and wrap-safe clock comparisons; define
  zero-duration sleep and the maximum supported interval.
- [ ] Let input, child completion, and timer deadlines block only the caller.
- [ ] Preserve the event-sequence protocol so a signal between checking a
  predicate and attaching a waiter cannot be lost.
- [ ] Wake a cancelled task without freeing its active stack or address space,
  and finish termination at an existing safe boundary.
- [ ] Remove wait registrations and deadline references on every exit path.

Test wakeups before and after registration, several waiters on one event,
cancelled sleepers, deadline overflow, and repeated sleep/exit cycles. Idle
should sleep when there is no runnable work; blocked tasks should not busy-poll.

### 5. Foreground console ownership and shell supervision

- [ ] Replace assumptions about one nested foreground chain with an explicit,
  referenced shell session and foreground owner.
- [ ] Give keyboard input and Ctrl+C only to the foreground process or the
  interactive shell; nonforeground processes must not consume shell input.
- [ ] Define the result of a background console read and how console output
  from several processes is serialized.
- [ ] Transfer ownership on foreground launch and restore it after normal
  exit, fault, cancellation, or a failed launch.
- [ ] Restrict directory-change, exit, and recovery requests to an authorized
  shell command; ordinary concurrent children cannot modify the shell.
- [ ] Define cleanup of session-owned children on shell restart and preserve
  the existing restart limit and kernel recovery path.

Test competing input readers, nested launches, a child fault during input,
Ctrl+C during disk work, and shell restart with live children. Recovery must
remain reachable without leaving a dead process as the console owner.

### 6. Process tools and minimal concurrent launch

- [ ] Add a bounded public process-list query with identifiers, names, state,
  and parent relationships, omitting addresses and kernel pointers.
- [ ] Build standalone `ps`, `kill`, and `sleep` programs using only public
  headers and the runtime.
- [ ] Add a minimal shell `start <program>` form that launches a nonforeground
  child and returns to the prompt; reserve full job syntax for 0.6.0.
- [ ] Track and reap those children without blocking line input. Notify the
  shell when an owned child finishes and report its result once.
- [ ] Keep existing foreground commands, program lookup, working-directory
  behavior, and user-facing error messages compatible.
- [ ] Document process limits, termination permissions, output interleaving,
  and the distinction between starting a child and waiting for it.

Start a CPU-bound child, use `ps` while it runs, run another program, and terminate
only the selected child. Test registry exhaustion, malformed process queries,
completion while the shell waits for input, and multiple rapidly exiting children.

### 7. 0.5.0 integration and release

- [ ] Stress several runnable, sleeping, and input-waiting processes while
  reading and writing FAT files.
- [ ] Verify address-space isolation and register preservation across many
  timer-driven switches.
- [ ] Repeat spawn, wait, fault, and cancellation until process, handle,
  object, heap, and physical-page counts return to their baseline.
- [ ] Exercise parent exit, orphan cleanup, failed publication, pending I/O,
  and shell restart with outstanding children.
- [ ] Run the complete host and QEMU suite through both boot paths and
  supported RAM sizes, including missing disks, read-only mounts, malformed
  volumes, and full media.
- [ ] Update process, scheduler, console, ABI, and troubleshooting guides.
- [ ] Verify Windows and Linux packaging and boot the exact packaged ISO.

0.5.0 is ready when two CPU-bound user programs make progress independently,
the shell remains usable while a nonforeground child runs, and cancelling or
faulting one process leaves the others alive. Persistent bytes must still
survive a separate boot, and diskless boot must retain recovery and Snake.

## 0.6.0 — shell workflows and userspace tools

The goal is to connect programs through standard streams and make everyday
file work convenient. Complete shared-handle and pipe semantics before adding
pipelines or job syntax. Programs remain separate ELFs in the read-only `/rum`
boot volume; user files and optional applications remain on `/disk`.

### 1. Shared open objects and descriptor inheritance

- [ ] Separate handle-table entries from referenced open-file descriptions.
  Duplicated handles share offset, access mode, and pending I/O error state;
  separate opens keep independent offsets.
- [ ] Add descriptor duplication and explicit spawn-time stream mappings.
  Allow files and future pipes to occupy standard handles 0–2.
- [ ] Define close-on-spawn behavior and inherit only descriptors requested
  by a validated launch packet.
- [ ] Validate source handles, target numbers, modes, reserved fields, and
  mapping conflicts before publishing a child.
- [ ] Keep backend objects pinned until the final shared reference closes;
  retain the documented busy-object rules for removal and replacement.
- [ ] Roll back every acquired reference on failed duplication, spawn, exit,
  user fault, and cancellation.

Test shared offsets, independent opens, closing one alias, standard-handle
replacement, table exhaustion, conflicting mappings, and partial launch
failure. Extend ownership diagnostics to count open descriptions and aliases.

### 2. Bounded pipes and stream I/O

- [ ] Add a versioned pipe-creation request returning separate read and write
  handles with a documented fixed buffer capacity.
- [ ] Route pipe transfers through the existing read/write interface with
  exact byte counts, partial transfers, and zero-length semantics.
- [ ] Block readers on an empty pipe while writers remain, and block writers
  on a full pipe while readers remain.
- [ ] Return EOF only after all writers close and buffered bytes are consumed;
  return a public broken-pipe error when no readers remain.
- [ ] Reject seeking and directory operations on pipes, and define ordering
  and any atomic-write limit for multiple writers.
- [ ] Wake waiters on transfer, final close, fault, and cancellation without
  losing events or releasing a pipe that still has live references.

Compare binary payloads larger than the pipe capacity byte for byte. Cover
several readers/writers, last-end closure, cancelled blocked tasks, inherited
aliases, handle exhaustion, and repeated pipelines returning counts to baseline.

### 3. File redirection and pipeline launch

- [ ] Extend file opening with explicit create, truncate, and append behavior,
  including backend capability checks and documented busy-object rules.
- [ ] Keep append positioning and writes serialized across concurrent open
  descriptions. Preserve backend size limits and partial-failure reporting.
- [ ] Parse `<`, `>`, `>>`, and `|` in userspace with explicit bounds on commands,
  arguments, redirections, and pipeline length.
- [ ] Prepare pipes and stream mappings, spawn every pipeline stage before
  waiting, and close the shell's unused pipe endpoints promptly.
- [ ] Define foreground input ownership for a pipeline and cancel and reap
  all of its stages on Ctrl+C; step 4 adds the shell's job registry.
- [ ] Define redirection order, standard error behavior, and the pipeline's
  reported status.
- [ ] Terminate and reap already-started stages if later setup fails. Document
  that an earlier file truncation or committed write cannot be rolled back.
- [ ] Make `cat` offer a byte-preserving stream mode for redirection and pipes
  while retaining a safe display mode for the console.

Test empty and binary files, append, failed opens, full/read-only media,
pipelines longer than one pipe buffer, missing executables, and a failed middle
stage. Verify exact resulting file bytes and ensure no inherited endpoint
keeps a completed pipeline waiting forever.

### 4. Background jobs and foreground control

- [ ] Track a bounded job as one process or a pipeline, with one shell-session
  identity and a defined lifecycle.
- [ ] Add `&` launch syntax and standalone `jobs`, `fg`, and `wait` commands using
  public session interfaces.
- [ ] Transfer console ownership to a foreground job and return it when every
  stage finishes or the job is cancelled.
- [ ] Deliver Ctrl+C to the foreground job's active stages, leaving the shell
  and unrelated jobs running.
- [ ] Report job completion once, preserve each stage's result for cleanup,
  and define the user-visible pipeline result.
- [ ] Define shell exit/restart policy for outstanding jobs and handle job-table
  exhaustion without leaking children or descriptors.

Test foreground pipelines, background pipelines, concurrent completions,
background attempts to read the console, `fg` on an exited job, and cancellation
while a stage is blocked in pipe or file I/O. Stop/resume, Ctrl+Z, and a general
Unix signal model remain later work.

### 5. Public terminal input and userspace Snake

- [ ] Add fixed-width terminal capability queries and bounded cursor, color,
  text-update, and noncanonical keyboard-input operations.
- [ ] Restrict interactive terminal state changes and keyboard reads to the
  foreground owner; validate all positions, lengths, flags, and user buffers.
- [ ] Define input events and timed waits needed by interactive programs,
  keeping PS/2 queues and VGA addresses private to the kernel.
- [ ] Restore terminal mode, cursor, colors, and input ownership after normal
  exit, cancellation, a user fault, or partial setup.
- [ ] Move the game loop, board, scoring, and rendering into `snake.elf` using
  only public terminal, timing, and process interfaces.
- [ ] Retain a recovery-shell game path so missing or rejected system images
  do not remove diskless recovery functionality.

Test Snake under competing CPU load, invalid terminal packets, background
access attempts, rapid input, and faults while a terminal mode is active.
Ordinary output and the shell prompt must remain usable afterward.

A small bounded text editor is a stretch goal after these interfaces are
stable. It should preserve the original file when saving fails and state its
supported size and encoding limits.

### 6. Shell parsing and line editing

- [ ] Add single and double quotes plus documented escaping rules, preserving
  empty arguments and literal operator characters.
- [ ] Use one bounded parser for normal commands, pipelines, and redirections;
  reject incomplete syntax before launching anything.
- [ ] Pass parsed `argv` to commands without depending on a privileged raw-text
  shortcut. Define `echo` and `write` spacing under the new parser and document
  changes from their earlier raw-tail behavior.
- [ ] Add cursor movement, Home/End, Delete, and bounded in-memory history.
- [ ] Add bounded completion for program names and filesystem paths, respecting
  each backend's naming and traversal limits.
- [ ] Redraw the prompt and current line after asynchronous job notifications.

Test quoted paths, empty arguments, escaped separators, unmatched quotes,
maximum line/argument sizes, history limits, completion on both mounts, and
notifications arriving during editing. Variables, globbing, command substitution,
and script execution remain outside this release.

### 7. File tools and persistent workspace

- [ ] Add `touch`, `cp`, `mv`, and `stat` programs with clear overwrite rules,
  checked sizes, and public filesystem error messages.
- [ ] Add public metadata queries and same-backend regular-file rename where
  required by those tools. Document rename write ordering and interruption
  windows without promising crash-safe replacement.
- [ ] Make `touch` preserve existing contents and define its timestamp
  behavior within the capabilities of the current filesystem and clock.
- [ ] Prevent accidental self-copy and define open-object, read-only, and
  destination-exists behavior before mutation.
- [ ] Keep cross-mount moves explicit: copy and flush successfully before
  deleting the source, and report partial outcomes honestly.
- [ ] Provide an opt-in setup command for `/disk/HOME` and `/disk/APPS`. Never
  create, format, or replace a user disk as a side effect of normal boot.
- [ ] Document how to run copied applications without making `/rum` writable
  or searching user-controlled directories ahead of system tools by default.

Test empty and multi-cluster files, exact binary copies, naming limits,
cross-mount failures, busy files, overwrite refusal, and full media. Validate
renames and copied bytes with host FAT tools after QEMU exits. Directory moves
and general crash recovery are not required.

### 8. 0.6.0 integration and release

- [ ] Create, copy, append to, and read persistent files through shell
  redirection and pipelines, then recover identical bytes after a separate boot.
- [ ] Exercise background jobs and foreground cancellation while pipes and
  file handles remain open.
- [ ] Inject setup, allocation, pipe, block, and filesystem failures at each
  ownership transfer and confirm the shell and recovery path remain usable.
- [ ] Verify every pipeline, cancelled job, child fault, and shell restart
  releases descriptors, open descriptions, pipes, process records, and pages.
- [ ] Run the complete host and QEMU suite through both boot paths and
  supported RAM sizes, including diskless and rejected-volume boots.
- [ ] Document shell grammar, stream semantics, job control, terminal APIs,
  naming limits, save/flush behavior, and interrupted-write expectations.
- [ ] Verify Windows and Linux packages, keep the ISO standalone, and boot
  the exact packaged artifact without bundling a mutable user disk.

0.6.0 is ready when the shell can run a pipeline, redirect its exact output to
a persistent file, and manage a background job without losing foreground input.
A fault or Ctrl+C must close every pipe endpoint and restore the prompt. Snake
must run in userspace, and the saved file must survive another QEMU boot.

## Delivery order

Work through each release in numbered order, using one focused branch and pull
request per step, such as `0.5/scheduling-prerequisites`, `0.5/user-preemption`,
`0.6/shared-handles`, or `0.6/pipes`. Keep conventional commits focused and commit
tested changes periodically. Integration branches collect completed steps.

Before a release, use the [clean release workflow](release.md), inspect the
saved serial/QMP artifacts and ownership counts, build `rum.zip`, and boot its
exact ISO. Update the release version only when that release is ready. Change
ABI versions when public contracts change and document binary compatibility.

General kernel preemption, SMP, fork, a GUI, networking, USB, long FAT names,
and 64-bit support remain future work. These releases keep the existing
storage format and interrupted-write guarantees while improving process
control and the public interfaces available to applications.
