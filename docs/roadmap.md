# rum roadmap

rum 0.8.0 provides private user memory, persistent FAT16 storage, a standalone
SDK, typed kernel objects and explicit access rights. The next foundations are
safe sharing between threads, followed by consistent I/O ownership and input
delivery. Completed release records live in [release validation](release.md).

Keep low-level kernel mechanisms, executive services and userspace policy
separate. Continue targeting one 32-bit x86 CPU with BIOS firmware, GRUB
Multiboot and direct ELF boot. User preemption remains timer-driven; kernel
code switches only at documented safe boundaries. SMP and arbitrary kernel
preemption are outside these two releases.

The [foundation plan in Notion](https://app.notion.com/p/411ed3fd93a9812b92c1e2feb55468f0)
records the longer-term order. Implement the numbered steps below in sequence.
Every unchecked item is planned work, not a claim about current behavior.

## 0.9.0 — synchronization and userspace threads

The goal is several threads sharing one process safely: separate execution
contexts and guarded stacks, shared memory and handles, reliable synchronization,
and predictable process-wide cleanup. Preserve existing single-threaded programs.

### 1. Thread ownership, synchronization and ABI contracts

- [ ] Audit the process/thread split. Specify owners for identity, address
  space, handles, working directory, console membership, termination results,
  registers, wait registrations and syscall buffers.
- [ ] Define independent system and per-process thread limits. Account for
  user/kernel stacks, guards, TLS and metadata within supported memory budgets;
  update stack-slot assumptions that currently track processes.
- [ ] Publish a lock order and context table covering scheduler updates,
  process tables, VM, handles, open descriptions, filesystems and drivers.
  Specify where blocking, allocation, callbacks and final destruction are legal.
- [ ] Define thread creation, return/exit, join and process-exit semantics,
  including the initial thread, faults and process-wide cancellation. Avoid
  unsafe asynchronous termination of individual threads.
- [ ] Design fixed-width, versioned thread and synchronization packets,
  object types and rights. IDs identify threads; handles authorize operations.
  Extend diagnostics without resizing existing 0.8 resource-count packets.
- [ ] Record memory-ordering and compatibility guarantees. Preserve syscall
  numbers, process waits, standard streams and documented released SDK behavior.

Validation: assign every shared field an owner and lock. Check packet layouts,
checked arithmetic, stack-slot capacity and worst-case memory use at 16 MiB.
Resolve teardown and lock dependencies before enabling user threads.

### 2. Kernel waits, events, mutexes and semaphores

- [ ] Build bounded wait registration over scheduler events. Checking a
  condition and registering its waiter must be atomic with respect to signals,
  without missed wakeups or duplicate registrations.
- [ ] Implement referenced event, mutex and semaphore objects with explicit
  query/wait/signal rights, limits and destruction rules. Define event reset
  modes and semaphore acquisition/release behavior.
- [ ] Specify mutex ownership, recursion policy and owner-exit handling.
  Report abandonment or terminate the affected process as documented; do not
  silently treat potentially inconsistent protected data as repaired.
- [ ] Resolve signal, acquisition, timeout and process cancellation through
  one completion decision. A semaphore permit or acquired mutex must not be
  lost when cancellation competes with a successful wait.
- [ ] Keep IRQ signaling bounded and allocation-free. Blocking acquisition and
  final cleanup require allowed task context, outside short protected updates.
- [ ] Define fairness, zero-time polls, timeout rounding, tick wraparound and
  saturation. General multi-object wait sets can remain future work; existing
  waits must remain compatible.

Validation: force signal-before/during-registration, multiple waiters, permit
exhaustion, timeout/cancel races, invalid rights, owner exit and final-close
races. Check wait registrations and object references after every case.

### 3. Protect shared process resources

- [ ] Make descriptor and typed-handle acquisition, close, duplication and
  inheritance safe under sibling-thread interleaving. Retain objects under
  table locks; do blocking work and final destruction outside those locks.
- [ ] Serialize shared offsets, append and deferred errors across aliases,
  preserving partial I/O, EOF and pipe semantics. Define concurrent-operation
  atomicity without promising whole-file transactions.
- [ ] Protect VM registries and mapping changes. Extend buffer leases to
  sibling-thread release/protection races and prevent unmapping active thread
  stacks, TLS or memory still referenced by kernel operations.
- [ ] Protect working directories, child publication/wait consumption, process
  references and diagnostics. Define what relative path operations observe
  when another thread changes the shared directory.
- [ ] Preserve process-level console/session authority while serializing
  interactive leases and foreground controls. Thread exit must not detach
  another thread's console operation or grant extra shell authority.
- [ ] Audit filesystem/block serialization and callbacks against the lock
  order. Never wait while holding a lock needed by the completing operation,
  or rely on user-mode preemption being disabled.

Validation: interleave close/use, duplicate/close, seek/read, append, chdir/path,
spawn/wait and VM changes at controlled scheduling points. Rejected races must
preserve rights, references, offsets and buffer ownership.

### 4. User-thread creation, scheduling and join

- [ ] Allow multiple scheduler threads to reference one process/address space.
  Give each a complete saved user frame, kernel-entry stack and guarded user
  stack with explicit allocation and lifetime ownership.
- [ ] Validate entry points, arguments, stack layout and flags before publishing
  a runnable thread. Initialize privately and unwind failure at every resource
  acquisition and mapping stage.
- [ ] Add thread objects and public creation, self-identification, exit and
  join operations with rights and stale-handle rejection. Define self-join,
  concurrent/repeated joins, retained results and closing an unjoined handle.
- [ ] Update thread/process identity, address-space selection and TSS state
  consistently at the trusted switch boundary. Preserve registers, guards and
  the current integer-only CPU restrictions.
- [ ] Round-robin runnable threads and document how additional runnable threads
  affect a process's CPU share. Supported waits block only their calling thread;
  retain an idle path when none are ready.
- [ ] Preserve the initial thread's existing launch stack and process ABI.
  Until step 5, use dedicated test entry points that avoid concurrent access
  to the current single-threaded C runtime.

Validation: run CPU-bound sibling threads without explicit yields. Verify
shared memory within a process, isolation between processes, stack guards,
join races, thread limits, identity reuse and failed construction cleanup.

### 5. TLS, concurrent runtime and standalone SDK

- [ ] Define the supported TLS/TCB layout, initialization and access method
  for i386. Validate configured bases and retain mappings until the owning
  thread is inactive. Dynamic-library TLS remains outside this release.
- [ ] Publish TLS state before the thread runs and restore it during switches.
  Give `errno` and other declared thread-local runtime state separate storage.
- [ ] Make allocator and stdio state safe for supported concurrent use. Define
  per-stream locking, lock order, initialization and failure behavior without
  blocking under incompatible kernel scopes.
- [ ] Add SDK thread/synchronization wrappers and a startup trampoline that
  turns worker-function return into thread exit, not process exit. Document
  which C functions support concurrent calls.
- [ ] Preserve released single-threaded binaries and explain why programs
  opting into threads need the updated SDK/runtime. Version new features and
  diagnostic layouts without changing existing packet sizes or call meanings.
- [ ] Provide an external example using shared memory, synchronization,
  thread-local errors, heap allocation and file I/O through public interfaces.

Validation: compare TLS across switches and processes. Stress allocator/stream
sharing, initialization races and recoverable allocation failure. Build the
threaded example outside the repository using only the exported SDK.

### 6. Thread groups, faults and process teardown

- [ ] Preserve `rum_exit` and return-from-`main` as process-wide termination;
  provide a distinct worker-thread exit path. Normal thread exit must not
  close shared handles or destroy the process address space.
- [ ] Make user faults terminate the entire process for this release, avoiding
  continued execution with possibly corrupted shared state. Capture a bounded
  fault result and preserve shell/recovery behavior.
- [ ] On process cancellation, prevent new thread/child publication, wake
  cancellable waits and drain threads through safe boundaries. Never reclaim
  an active stack or abandon a kernel operation's owned buffer.
- [ ] Detach waits and thread-owned leases exactly once. Resolve mutex-owner
  exit according to step 2; retain legitimate result observations without
  retaining inactive execution stacks unnecessarily.
- [ ] Publish process completion only after siblings can no longer execute
  and required cleanup is complete. Close shared tables and release VM once;
  retained process/thread handles keep only their documented state.
- [ ] Integrate joins, child collection, orphan adoption, jobs, recovery and
  shutdown with the new lifetimes. Keep bounded synchronous I/O's possible
  cancellation delay explicit.

Validation: terminate groups with runnable, sleeping, contending and I/O-blocked
threads, including faults in workers and exit during construction. Repeated
runs must restore thread, wait, object, descriptor, TLS and page counts.

### 7. 0.9.0 integration and release

- [ ] Combine scheduling, synchronization, shared handles, heap pressure,
  filesystem I/O and process management in one external SDK application.
- [ ] Exercise invalid packets, wrong rights, exhaustion, missed-wakeup races,
  abandonment, timeouts, faults and cancellation through both boot paths.
- [ ] Extend public resource summaries and trusted recovery ledgers for
  threads, synchronization objects, waits and TLS. Public diagnostics must
  expose neither kernel addresses nor other processes' private data.
- [ ] Preserve shell commands, pipelines, jobs, Snake, persistent bytes, power
  controls and recovery on missing, damaged, read-only and full disks.
- [ ] Document locks, memory ordering, TLS, thread/process exit, SDK usage and
  enforced limits. Verify released 0.8 SDK applications alongside the new runtime.
- [ ] Run the complete clean gate, supported RAM configurations and native
  Windows/Linux package checks. Boot the exact packaged ISO and keep mutable
  disks separate. Bump the version only after validation passes.

0.9.0 is ready when independent SDK-built threads can share resources, contend,
sleep, time out and exit without missed wakeups or premature destruction when
using the documented lock order. Kernel synchronization must not deadlock
supported operations. Process faults and cancellation must
reclaim the whole group while preserving other processes and recovery.

## 0.10.0 — common I/O requests and input events

The goal is one explicit ownership and completion contract for filesystem and
device work. Requests may still complete synchronously. Introduce workers or
deferred completion where needed for correctness and progress; broad public
asynchronous I/O, DMA and new device classes are not release prerequisites.

### 1. Request, completion and cancellation contracts

- [ ] Specify ownership from construction through validation, submission,
  queueing, execution, completion observation and final release. Keep kernel
  request layouts and driver pointers private.
- [ ] Define states, legal transitions and exactly-once completion, including
  references held by callers, queues, drivers and observers. Distinguish a
  cancellation request from proof the operation has stopped.
- [ ] Define byte counts, short transfers, EOF, partial failures and flush
  results consistently across descriptors, typed I/O and backends.
- [ ] Specify buffer preparation, limits, mapping lifetime and copy-in/out.
  No operation may retain a borrowed user pointer or stack packet after the
  ownership protecting it ends.
- [ ] Define deadlines, cancellation points and irreversible stages. Report
  completed progress honestly: cancelling a write does not imply that no data
  reached disk or that earlier writes were rolled back.
- [ ] Publish a request/lock/context table using 0.9's synchronization rules,
  covering filesystem-to-block nesting and allowed IRQ completion work.

Validation: enumerate references and buffer owners at every success, failure
and cancellation boundary before migrating drivers.

### 2. Bounded request core and completion delivery

- [ ] Add bounded request storage with explicit system, process and device
  quotas. Reject overflow/exhaustion before publishing queue entries.
- [ ] Implement submission, queue removal, active ownership and completion
  publication as synchronized, legal state transitions.
- [ ] Keep driver references until completion even if a caller closes its
  handle or stops waiting. Release queue entries, objects and results only
  after their final legitimate owner finishes.
- [ ] Make request completion waitable under established timeout/cancel rules.
  Prevent stale completions from targeting reused storage and specify how
  long results remain observable.
- [ ] Limit IRQ completion to bounded state changes and signaling. Allocation,
  formatting, user-memory access and blocking destruction belong in task
  context outside driver/update locks.
- [ ] Provide synchronous adapters preserving existing calls. Add workers only
  behind bounded queues with a documented stop/drain path.

Validation: force completion-before-wait, duplicate completion, queue exhaustion,
queued removal, stale reuse, final-close races and allocation failure. No path
may complete twice, leak requests or free active storage.

### 3. User buffers and shared file operations

- [ ] Validate complete packets, flags, handles, rights and user spans before
  submission. Deferred work must retain the requester's authority rather than
  acquiring the worker's trusted role or bypassing `/RUM` protection.
- [ ] Use bounded snapshots, bounce buffers or retained mappings according to
  the contract. Workers must not dereference user virtual addresses as though
  they belonged to the worker's current address space.
- [ ] Retain output mappings and objects until final completion or proven-safe
  unwind. Coordinate sibling-thread VM changes with leases without retaining
  incompatible locks across waits.
- [ ] Serialize offsets, append positions and deferred errors through completion,
  preserving transfer counts and existing pipe atomic-write limits.
- [ ] Define close, remove, replace and exit while operations are pending.
  Closing a table entry is not proof its driver operation has stopped.
- [ ] Bound and account for queued data, pinned pages and completion records
  independently so callers cannot consume unbounded kernel memory.

Validation: combine cross-page buffers, permission changes, unmapping attempts,
aliases, short I/O and closing all visible handles during active work. Denied
submissions must alter no backend data or ownership.

### 4. Block, filesystem and pipe integration

- [ ] Route ATA/block and filesystem operations through the common lifecycle,
  preserving validated LBA/count arithmetic, transfer bounds, controller
  serialization, bounded polling and internal error reporting.
- [ ] Establish serialization for FAT operations and their block requests.
  Never wait for a worker that needs a lock the caller holds, or recursively
  enter a backend through its own non-reentrant request path.
- [ ] Adapt file/directory operations and flushing without changing FAT bounds
  or overstating interrupted-write guarantees. Stronger storage integrity and
  recovery remain the 0.11.0 focus.
- [ ] Integrate pipe operations with retained-request/completion rules while
  preserving EOF, broken pipe and bounded backpressure.
- [ ] Replace broad no-switch assumptions only where ownership and locking
  prove a safe wait. Keep port I/O in the kernel without arbitrary kernel
  preemption or a wholesale driver rewrite.
- [ ] Notify relevant waiters rather than waking unrelated input/filesystem
  work each tick. Waiting for queued work must allow runnable threads to
  progress; document delays from bounded synchronous hardware operations.

Validation: exercise disk-root operations and diskless recovery, first/last
sectors, read-only/full media, malformed volumes, pipe pressure, partial I/O and
injected controller errors. Compare guest bytes/listings with host FAT tools
and verify canaries outside requested writes.

### 5. Cancellation, deadlines and draining

- [ ] Cancel queued work before dispatch where possible. For active work,
  report cancellation only after a documented safe driver boundary; do not
  free buffers merely because the waiting thread was cancelled.
- [ ] Resolve completion, timeout and cancellation races once, preserving
  transferred byte counts and the correct final result for each observer.
- [ ] Bound polling, retries and deadline arithmetic, including clock wraparound
  and a stopped timing source. Never hide a failed flush or retry irreversible
  work without a documented contract.
- [ ] Drain or safely abandon requests during exit, faults and construction
  failure. Dead callers must not leave requests pointing to reclaimed stacks,
  mappings, tables or authority records.
- [ ] Define quiesce/drain for recovery, unmount and shutdown: stop new work,
  settle active operations, flush as required, then release resources. Failure
  must preserve diagnostics and a usable recovery path.
- [ ] Extend sanitized summaries and trusted ledgers with queued/active
  requests, completion records, retained buffers and cancellation outcomes.

Validation: inject failures/cancellation at every transition with concurrent
close, timeout and teardown. Verify exactly-once completion, bounded draining
and restored request, object, buffer and page counts after repeated runs.

### 6. Input events and interactive ownership

- [ ] Define fixed-width, versioned events for physical key identity,
  press/release transitions, modifiers and repeat. Separate characters from
  physical-key state and document the supported keyboard layout.
- [ ] Deliver events through bounded queues and explicit foreground ownership.
  Ordinary programs cannot read another session's input, inject privileged
  controls or access keyboard ports/queues directly.
- [ ] Make overflow observable and define state resynchronization so a lost
  release cannot leave a key permanently held. Handle focus/lease changes,
  cancellation and consumer exit under the same ownership rules.
- [ ] Keep IRQ decoding/queue updates bounded and allocation-free; deliver
  application events through request/wait contracts in task context.
- [ ] Preserve character streams and terminal events through adapters,
  including shell editing, paging, Ctrl+C and console restoration. Define
  ownership when choosing raw versus translated input.
- [ ] Add an external SDK probe for simultaneous held keys, releases and timed
  waits. Keep the interface suitable for future Doom controls without graphics,
  audio or game-specific syscalls in this release.

Validation: test make/break sequences, extended keys, modifiers, repeat,
overflow/resynchronization, interrupted waits and foreground transfers during
child exit, faults and Ctrl+C. Reject stale ownership and prevent stuck keys.

### 7. 0.10.0 integration and release

- [ ] Combine threaded applications, file/block work, pipes, input and
  cancellation under bounded memory/request pressure through the public SDK.
- [ ] Preserve shell tools, pipelines, jobs, Snake, power controls and recovery
  on absent, damaged, read-only and full disks, including injected I/O failures.
- [ ] Recover exact persistent bytes after separate boots and audit FAT
  ownership/canaries after failed writes. Keep durability limitations explicit.
- [ ] Demonstrate that final-owner exit, faults, timeouts and cancellation
  restore request, completion, buffer, wait, handle, object, thread and page
  counts while unrelated processes remain usable.
- [ ] Document states, authority, buffers, partial errors, drain behavior,
  input events and overflow recovery. Verify released 0.9 SDK applications
  and older documented interfaces remain compatible.
- [ ] Run the complete clean gate through both boot paths and supported RAM
  sizes, validate Windows/Linux packages and exact packaged boots, and keep
  pristine VM disks separate from the standalone ISO. Then bump the version.

0.10.0 is ready when queued or active I/O can complete, fail, time out or be
cancelled without stranded requests, freed active buffers or frozen unrelated
processes. Input ownership/key state must survive overflow, foreground changes
and teardown, and recovery must remain usable after device failures.

## Later releases

| Release | Focus |
| --- | --- |
| 0.11.0 | Storage integrity, flush guarantees and recovery |
| 0.12.0 | Userspace initialization, services and system authority |
| 0.13.0 | Combined foundation integration and readiness gate |
| 0.14.0 | Minimal graphics and a first playable userspace Doom port, after the foundation gate |

The Doom compatibility probe remains an application/runtime reference. Prepare
key press/release delivery during 0.10.0; the playable port stays an ordinary
SDK-built fullscreen application after the foundation gate. Broader libraries,
nonessential drivers, audio and desktop work follow as requirements justify them.

## Delivery order

Start with 0.9.0 step 1 and complete each release in numbered order. Use one
focused branch and PR per step, created from `master` after dependencies land,
with work-focused names such as `feat/thread-contracts`, `feat/kernel-waits` or
`feat/io-requests`. Use Conventional Commits and save verified work locally in
focused commits. Do not mark planned work complete before its checks pass.

Use focused tests during development. Reserve the complete suite for release
integration or changes spanning enough interfaces to require it. Follow the
[local release workflow](release.md), inspect diagnostics, package only fresh
system disks and boot the exact downloadable ISO. Change release versions only
after readiness gates pass; version public contracts independently.
