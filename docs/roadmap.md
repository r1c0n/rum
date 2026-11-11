# rum roadmap

rum 0.7.0 provides private user memory, a growing application heap, larger ELF
loading and a standalone SDK alongside persistent FAT16 storage, a userspace
shell, pipelines, background jobs and Snake. The next release builds common
object ownership and access-rights contracts for kernel resources.

The architecture separates low-level kernel mechanisms, executive services
such as memory and object management, and userspace policy. The 32-bit x86 BIOS
target, GRUB Multiboot and direct ELF boot paths remain supported. Hardware
access and protection stay in the kernel; applications use the public ABI.

The [foundation release plan in Notion](https://app.notion.com/p/411ed3fd93a9812b92c1e2feb55468f0)
records the longer-term priorities. This roadmap breaks the next two releases
into implementation steps; checked items have implementation and validation.

## 0.7.0 — userspace memory and the application SDK

The goal is to build an application outside the rum repository, load a larger
executable, and let it allocate and release its own memory through public
interfaces. Keep one userspace thread per process for this release. Eager page
allocation is sufficient; demand paging, swapping and copy-on-write can follow
when a requirement justifies them.

### 1. Virtual-memory contracts and resource accounting

- [x] Define the available user address ranges and ownership of executable
  mappings, dynamic regions, stacks and guards. Keep kernel mappings and
  reserved ranges inaccessible to allocation requests.
- [x] Specify page alignment, address selection, protection flags, region and
  per-process limits, zero-length behavior and failure results.
- [x] Add fixed-width, versioned public request structures for allocating,
  protecting and releasing regions. Reject unknown flags, reserved fields,
  invalid sizes and address arithmetic overflow before modifying mappings.
- [x] Define which operations require task context, how mapping changes are
  serialized, and when scheduling is allowed during construction or cleanup.
- [x] Account for committed user pages, page-table pages and region metadata.
  Document how these consume physical memory and the process budget.

Test boundary addresses, page rounding, overlapping ranges, quota exhaustion,
invalid packet sizes and overflow. A rejected request must leave mappings,
accounting and the calling process unchanged.

See [User virtual memory](virtual-memory.md) for packets, policy, ownership,
accounting and the allocation/release interfaces implemented in step 2.

### 2. Private region allocation and release

- [x] Add a bounded per-process region registry with explicit ownership and
  collision checks against existing ELF, stack and reserved mappings.
- [x] Allocate private, zeroed pages and publish a region only after all required
  pages and metadata are ready. Roll back every partial construction failure.
- [x] Release only regions owned by the caller under the documented release
  contract. Reject double release, foreign addresses and attempts to free
  loader-owned mappings or active stacks through the region API.
- [x] Update page tables and invalidate translations safely for active and
  inactive address spaces without exposing partially updated state.
- [x] Release all dynamic regions during normal exit, fault, cancellation and
  failed process construction, including after allocation failure.

Exercise multiple processes allocating, writing and freeing regions. Check
zero-filled reuse, isolation, guard boundaries, allocation failure at each
stage and page/metadata counts after repeated exits.

### 3. Protection changes and syscall buffer safety

- [x] Apply supported access protections to complete owned ranges, with checked
  bounds and a documented policy for partial-region changes and splitting.
- [x] Describe the permissions the current paging mode can actually enforce.
  Do not promise non-executable pages or execute-only mappings without hardware
  support; reject unsupported requests explicitly.
- [x] Make syscall input/output validation cover dynamic regions and changes
  in readability or writability, including cross-page and unmapped spans.
- [x] Define the lifetime of mappings referenced by a blocked operation. Copy,
  retain or otherwise protect those buffers until the operation completes;
  establish a rule that remains safe when threading arrives later.
- [x] Keep permission faults confined to the offending process and preserve
  kernel recovery and cleanup behavior.

Test reads and writes across protection boundaries, malformed pointers,
read-only output buffers and invalid ranges. Verify that failed protection
changes leave the previous permissions intact and that process faults release
all outstanding resources.

### 4. Growing heap and application runtime

- [x] Build a userspace allocator over the region API with aligned `malloc`,
  `calloc`, `realloc` and `free` operations and defined zero-size behavior.
- [x] Check multiplication and size overflow, preserve the old allocation on
  failed `realloc`, and return allocation failure without terminating the OS.
- [x] Reuse freed storage and document when backing regions return to the OS.
  Release all process memory on exit even if an application omits `free`.
- [x] Add the C runtime functions needed by the external examples and planned
  application compatibility probe, with documented file, timing and error
  behavior. Keep wrappers implemented through public syscalls.
- [x] Document the runtime's current single-threaded assumptions so shared
  allocator and library state can be addressed in the threading release.

Test alignment, zeroing, preservation across growth and shrink, fragmented
allocation patterns, recoverable exhaustion and interleaved processes. Check
that heap pressure cannot alter another process's data or prevent recovery.

See [Application runtime](application-runtime.md) for allocation, file streams,
integer formatting, monotonic timing, errors and supported C functions.

### 5. Larger executable loading

- [x] Replace the current small image-loading bound with explicit executable,
  segment and committed-memory limits appropriate to supported RAM sizes.
- [x] Load bounded portions of an image instead of relying on a large fixed
  kernel-stack buffer or allocating the entire file unnecessarily.
- [x] Validate ELF32 header and program-header arithmetic, file spans, alignment,
  segment overlaps, `p_filesz <= p_memsz`, entry points and reserved ranges.
- [x] Preserve zero-filled BSS, guarded stacks, process isolation and the
  permission policy from step 3. Charge all mappings against process limits.
- [x] Keep static i386 executables as the supported format and update the host
  ELF validator and SDK linker contract alongside the kernel loader.

Load valid images beyond the old limit, multiple segments and large BSS.
Reject truncated files, overflowing offsets, overlapping or forbidden mappings
and invalid entry points. Inject read/allocation failures and verify complete
rollback without changing the parent or its descriptors.

See [Executable loading](executable-loading.md) for streamed reads, public
limits, validation, failure results and SDK checks.

### 6. Standalone SDK and application compatibility probe

- [x] Export public headers, startup objects, a runtime library, linker script,
  ELF validator and version/target information as a standalone SDK package.
- [x] Remove the exported validator's dependence on private kernel headers.
  Publish the executable and resource contracts external tools need.
- [x] Provide an external-project template and Windows/WSL and Linux build
  instructions using the separate cross compiler. The template must build
  without repository-private include paths or access to kernel sources.
- [x] Document application arguments, memory limits, filesystem access, streams,
  launch/exit behavior and ABI/runtime compatibility expectations.
- [x] Use a userspace Doom engine compile/link probe to identify missing heap,
  C runtime, executable-loading and WAD file-I/O support. Where practical,
  exercise initialization and file access without a display.
- [x] Record remaining port dependencies and keep application code outside the
  kernel. A playable game, framebuffer and sound are not 0.7.0 release gates.

Build and validate an application from an unpacked SDK in a separate directory.
Run it with dynamic allocation, file I/O and recoverable failures, then verify
cleanup after normal exit, fault and cancellation. The exported package must
not require an OS rebuild to compile an application.

See [Standalone SDK](sdk.md) for packaging, external builds and application
contracts, and [Doom compatibility](doom-compatibility.md) for the pinned
compile/link probe, tested zone/WAD subsystem and remaining port requirements.

### 7. Integration and release

- [x] Combine larger ELF loading, heap growth, filesystem operations and
  concurrent processes under bounded memory pressure.
- [x] Verify isolation, stack guards, buffer validation and page/region cleanup
  across exit, fault, cancellation and partial construction failures.
- [x] Keep existing shell commands, pipelines, jobs, persistent files, Snake and
  kernel recovery working, including without a usable system disk.
- [x] Run focused host tests during development, then the complete release
  suite through both boot paths and supported RAM configurations.
- [x] Document memory syscalls, enforced protections, quotas, runtime behavior
  and the SDK installation/build workflow.
- [x] Validate Windows and Linux SDK/release packages and boot the exact
  packaged ISO. Keep mutable user disks separate from the standalone ISO.

0.7.0 is ready when an application built outside the OS repository can load,
grow and free its heap, perform public file I/O and exit or fault without
leaking pages or corrupting another process. Allocation failures must remain
recoverable, and the exported SDK must reproduce that application build.

## 0.8.0 — kernel objects, handles and access rights

The goal is consistent lifetime and authority rules for files, pipes, console
endpoints and process references. Build on shared open descriptions rather than
replacing their behavior. Low-level scheduling and address-space mechanisms
stay separate from object management and userspace policy.

### 1. Common object ownership

- [x] Define a private object header with type, reference ownership, destruction
  rules and bounded diagnostics. Keep object addresses and layouts private.
- [x] Distinguish a named filesystem entry, an open description, a handle-table
  entry and an in-flight operation's reference. Specify which owns each
  resource and which state is shared between aliases.
- [x] Define acquisition, publication and final-release rules, including failed
  construction and destruction that may perform filesystem work.
- [x] Require final destruction in an allowed context, outside short protected
  updates and incompatible locks. Keep IRQ paths bounded and allocation-free.
- [x] Establish accounting for live objects, table entries and retained
  references before migrating existing resource types.

Test reference underflow/overflow rejection, failed construction, ownership
transfer and final close. No object may be published partially initialized or
freed while an operation still retains it.

See [Kernel object ownership](objects.md) for lifecycle contracts, reference
classes, context restrictions, accounting and focused checks.

### 2. Typed handles and access masks

- [ ] Define versioned public object-handle operations and type-specific rights.
  Reject unsupported bits, wrong object types and invalid handles before
  accessing the underlying resource.
- [ ] Use generation-aware identities for the new object-handle interface so a
  stale value cannot refer to a replacement object. Specify invalid values,
  capacity limits and safe generation-wrap behavior.
- [ ] Preserve existing stream descriptors, including slots 0–2, the bounded
  descriptor table, shared offsets and close-on-spawn flags. Clearly separate
  descriptor reuse semantics from generation-aware object-handle semantics.
- [ ] Grant only authorized rights at creation/open. Duplication and inheritance
  may preserve or reduce rights, never add authority the source does not hold.
- [ ] Define compatible ABI evolution and provide public wrappers without
  exposing pointers or requiring applications to understand kernel objects.

Test wrong-type use, forged/stale values, table exhaustion, reuse, generation
wrap and unknown access bits. Confirm existing binaries retain their documented
stream behavior, or provide an explicit versioned compatibility path.

### 3. File, pipe and console integration

- [ ] Adapt existing open descriptions and pipe endpoints to common object
  ownership while preserving shared offset, append and deferred-error behavior.
- [ ] Preserve filesystem backend identity, retained open-file references and
  explicit removal/replacement behavior while handles remain open.
- [ ] Keep EOF, broken-pipe, partial I/O, descriptor mapping and terminal
  foreground ownership semantics unchanged.
- [ ] Retain references across blocked I/O and release them exactly once on
  completion, cancellation or process teardown.
- [ ] Avoid double-counting or double-closing resources while old descriptor
  operations and the new object interface share implementation.

Repeat alias/inheritance, pipe and terminal tests through the adapted paths.
Check close during pending operations, child faults, last-reader/last-writer
exit and final backend release against ownership-count baselines.

### 4. Process objects and management rights

- [ ] Represent process references as objects with separate rights for supported
  query, wait and termination operations. Define who may acquire those rights.
- [ ] Keep process IDs used for display/discovery separate from authority.
  Prevent ID reuse from redirecting an existing reference to a new process.
- [ ] Separate a running process's execution resources from its retained exit
  status. Define when status is consumed and when final references disappear.
- [ ] Preserve direct-child, job and shell-session behavior while expressing
  existing delegation through explicit rights.
- [ ] Define process exit and cancellation with pending waits or I/O; leave
  thread and synchronization object implementations for 0.9.0.

Test parent/child exit ordering, retained exit status, failed acquisition,
foreign-session operations, reused IDs and termination during I/O. Closing a
reference must not accidentally terminate its process or lose another owner's
legitimate wait result.

### 5. Duplication, inheritance and construction failures

- [ ] Specify explicit duplication and child-inheritance rules for each object
  type, including rights reduction and non-inheritable entries.
- [ ] Integrate new handles with existing spawn descriptor mappings without
  changing standard-stream replacement or close-on-spawn semantics.
- [ ] Validate complete requests before publishing child-visible entries and
  unwind every acquired reference if child construction fails.
- [ ] Keep caller, child and in-flight operation references independent so exit
  or cancellation cannot revoke another owner's live reference accidentally.
- [ ] Document whether authority is checked when a handle is acquired or on
  each operation; define any supported revocation behavior explicitly.

Combine invalid mappings, rights escalation attempts, full tables and injected
construction failures. Repeated spawn/exit/fault cycles must restore handle,
object, pipe, filesystem-reference and page counts to baseline.

### 6. Trusted operations and system-file protection

- [ ] Define a small single-user authority policy for trusted boot/recovery,
  shell sessions and ordinary applications. Do not infer trust from a program's
  filename, directory or request flags alone.
- [ ] Enforce process/device management rights and trusted changes to `/RUM`
  at kernel service boundaries, including direct syscall use.
- [ ] Cover create, write, truncate, remove and rename operations, checking
  both source and destination identities where required. Normalization, case
  aliases and retained writable handles must not bypass protection.
- [ ] Define how system programs are installed or updated and how ordinary
  applications can use writable data/application directories without acquiring
  system-management authority.
- [ ] Preserve a controlled recovery path and document the limits of this
  single-user policy. Offline host edits remain outside the guest trust model.

Attempt protected operations through equivalent paths, aliases, inherited
handles and renamed files/directories. Denied requests must acquire no extra
rights, modify no protected data and leak no references. Exercise the supported
trusted maintenance workflow as well as denial paths.

### 7. Diagnostics, integration and release

- [ ] Expose bounded object/handle/reference counts and sanitized diagnostics
  without exposing kernel addresses or another process's private data.
- [ ] Combine stale handles, wrong types, rights failures, exhaustion, blocked
  I/O, process exit and cancellation in ownership-focused regression tests.
- [ ] Run existing shell, pipeline, job, terminal, persistence and recovery
  checks alongside an external application built with the 0.7 SDK.
- [ ] Verify that faults and failures leave no live objects or retained buffers
  after their final legitimate owner is gone.
- [ ] Document object types, rights, descriptor/handle differences, inheritance,
  process references, trusted operations and ABI compatibility.
- [ ] Run the full release gate and validate Windows/Linux packages and exact
  packaged boots before changing the release version.

0.8.0 is ready when aliases and inherited handles preserve lifetime correctly,
stale object handles cannot target replacements, and denied operations cannot
acquire authority. Failed construction, cancellation, faults and final close
must restore ownership counts to baseline. Existing applications and the SDK
must retain their documented behavior.

## Later releases

Work through these foundation areas in order. Detailed steps belong in the
roadmap when each release becomes the next implementation target.

| Release | Focus |
| --- | --- |
| 0.9.0 | Synchronization and multiple userspace threads |
| 0.10.0 | Common I/O requests, cancellation and input event contracts |
| 0.11.0 | Storage integrity, flush guarantees and recovery |
| 0.12.0 | Userspace initialization, services and system authority |
| 0.13.0 | Combined foundation integration and readiness gate |
| 0.14.0 | Minimal graphics and a first playable userspace Doom port, conditional on the foundation gate |

Prepare Doom's application/runtime requirements during 0.7 and input contracts,
including key presses and releases, during 0.10. The first playable port targets
fullscreen graphics without requiring audio or a desktop. It must remain an
ordinary SDK-built userspace application. If foundations need more work after
0.13, revise the target rather than bypassing their readiness requirements.
Broader libraries, additional drivers, audio and desktop work follow as needed.

## Delivery order

Complete each release's steps in numbered order, with one focused branch and
pull request per step, created from `master` after its dependencies land.
Use work-focused names such as `feat/object-ownership`, `feat/typed-handles`
or `test/object-lifecycle`; a release-number prefix is optional. Keep Conventional
Commits focused and commit verified changes periodically. Do not mark planned work complete
until its implementation and relevant checks pass.

Use focused tests while developing each step and reserve the complete suite
for integration or changes that cross enough interfaces to require it. Follow
the [clean release workflow](release.md), inspect saved diagnostics and resource
counts, and boot the exact packaged ISO before declaring a release ready.
Update the OS version only at that point; version public contracts separately
when they change and document compatibility.
