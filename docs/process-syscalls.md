# Process and console syscalls

Include `<rum/user.h>` and link with rum's user runtime. The normal shell uses
these calls to launch programs and manage the console without accessing kernel
objects or device registers.

`rum_run` combines launch, foreground input ownership and waiting. For independent
children, use `rum_spawn`, `rum_wait` and `rum_terminate`. All programs use private
address spaces, fresh standard streams and an independent working-directory
reference. The shell still launches foreground commands; job syntax and a shell
`start` command are separate work.

## Running a foreground child

```c
struct rum_arguments args = {
    .argc = 2, .string_bytes = 13, .offsets = {0, 6},
    .strings = "hello\0island"
};
struct rum_process_result child = {
    .version = RUM_PROCESS_ABI_VERSION, .size = sizeof child
};
rum_result_t error = rum_run("/rum/hello.elf", &args, &child);
if (error < 0) return 1; /* No child was launched. */
if (child.termination == RUM_PROCESS_EXITED) return child.status;
return 1;
```

`rum_run` uses syscall 13. It loads an existing regular file through the common
filesystem, validates the static i386 ELF, publishes one foreground child,
waits for termination and reaps it before returning. Relative paths use the
calling process's working directory. The kernel has no program-search path;
if a file is absent and its name does not end in `.elf`, it tries that suffix.
The shell resolves bare program names in `/rum` first, then its working directory.

The 32-byte `rum_run_request` contains eight fixed-width words:

| Word | Input |
| --- | --- |
| `version`, `size` | `RUM_PROCESS_ABI_VERSION`, `sizeof(struct rum_run_request)` |
| `path` | Address of a bounded NUL-terminated path |
| `arguments` | Address of the complete `rum_arguments` packet |
| `result` | Address of a readable and writable `rum_process_result` packet |
| `flags`, `reserved[2]` | Zero |

Arguments include `argv[0]`: 1–32 offsets describe consecutive NUL-terminated
strings beginning at offset zero. `string_bytes` must count exactly those
strings, up to 4096 bytes. Empty arguments are permitted. The kernel copies
the entire 4232-byte packet; no string pointer is followed. Packet contents,
every covered user page, the result header and the path are checked before any
filesystem access. Inputs may straddle pages or alias output because they are
copied before launch.

Initialize the 28-byte result with its version, size and zero reserved word.
All other fields are output:

| Field | Meaning |
| --- | --- |
| `process_id` | Child's positive PID |
| `termination` | `RUM_PROCESS_EXITED`, `RUM_PROCESS_FAULTED`, or `RUM_PROCESS_CANCELLED` |
| `status` | Full signed 32-bit exit status; zero for fault or cancellation |
| `fault_vector` | CPU exception vector for a fault; otherwise zero |

A negative syscall result reports a launch error and leaves the result packet
unchanged. Missing files, invalid paths, unavailable mounts, oversized or
unsupported executables and resource exhaustion use public `RUM_E*` errors.
No instruction, stack, physical or kernel address is returned. The kernel
retains a detailed fault record for trusted diagnostics only.

## Ownership and cancellation

Each child inherits an independent working-directory reference and starts with
fresh handles 0–2. It does not inherit open file handles. The parent sleeps
inside `RUN`, while the child receives standard input. A child may call `RUN`
again; only the deepest foreground child receives input and Ctrl+C. The shared
16-slot worker/process limit bounds nesting.

Exit, user fault and cancellation close the child's handles and restore the
previous live foreground owner before publishing completion. The parent collects
the remaining directory reference and private pages, and returns with its own
offsets and working directory intact. Input
still queued when a child exits becomes available to its parent; keyboard
input is not buffered separately per process.

Ctrl+C targets the active child. A blocked input syscall wakes and observes the
request; a CPU-bound program observes it at an interrupt return to user mode.
Filesystem callbacks finish in task context before cancellation, so a disk
write already in progress can commit. `RUN` returns a cancellation result rather
than cancelling the waiting parent. The kernel marks only the supervised initial
shell as interactive: Ctrl+C reaches that shell as a character for line editing.

Open handles are not inherited, and there is no general signal or arbitrary-kill
interface. The calls below provide bounded child control.

## Launching without waiting

```c
/* args uses the same packed argument format shown above. */
rum_result_t pid = rum_spawn("/rum/hello.elf", &args);
if (pid < 0) return 1;

struct rum_process_result child = {
    .version = RUM_PROCESS_ABI_VERSION, .size = sizeof child
};
rum_result_t completed = rum_wait((rum_pid_t)pid, 0, &child);
if (completed != 1) return 1;
return child.termination == RUM_PROCESS_EXITED ? child.status : 1;
```

`rum_spawn` (18) returns a positive PID after the executable, address space,
guarded kernel stack, working directory, streams and initial CPU frame have
been prepared and published. It does not wait or transfer console input. A
negative result publishes no child and releases temporary image, argument and
construction resources. Paths, ELF validation and argument limits match `RUN`.

The 32-byte `rum_spawn_request` contains `version`, `size`, `path`, `arguments`,
`flags` and `reserved[3]`. Use `RUM_LIFECYCLE_ABI_VERSION` (1), the exact size,
and zero flags/reserved words. Path and argument packets are fully copied and
validated before loading. The unused ECX and EDX syscall arguments must be zero.
There is no output pointer: EAX contains the PID or a negative public error.

Nonforeground children receive `-RUM_EACCES` when reading standard input or
changing console state through `rum_console`. They can use files and write to
standard output/error. Output calls copy bounded chunks in cooperative kernel
context. Each accepted chunk, up to 128 bytes, reaches VGA and COM1 without a
task switch; separate chunks from different processes can interleave. A
nonforeground child cannot acquire the console through `RUN` (`-RUM_EBUSY`).
Ownership, session cleanup and shell command permissions are described in
[Console sessions](console-sessions.md).

## Waiting and collecting results

`rum_wait(pid, flags, result)` (19) waits for one direct child. PID
`RUM_WAIT_ANY` (0) selects any direct child; when several have finished, the
kernel selects one completed registry slot without promising completion order.

| Return | Meaning |
| --- | --- |
| `1` | Result copied successfully; that child's completion and remaining resources are collected |
| `0` | `RUM_WAIT_NOHANG` was requested and matching children remain alive |
| `-RUM_ECHILD` | No owned child matches, including an already collected or unrelated PID |
| Other negative error | Invalid packet/buffer, or a refused operation; no completion is consumed |

With zero flags, wait sleeps until a matching child ends. `RUM_WAIT_NOHANG` is
the only supported flag and returns immediately. Both forms report signed exit
status, cancellation, or the CPU fault vector through the existing 28-byte
`rum_process_result`; no addresses or private fault record are returned.

The 32-byte `rum_wait_request` contains `version`, `size`, `process_id`,
`result`, `flags` and `reserved[3]`. Use lifecycle version 1 and zero reserved
words and unused syscall registers. The result buffer must be readable and
writable, with its process ABI version, exact size and zero reserved word
initialized. Its other fields are ignored as input. A zero return or any error
leaves it unchanged.

The kernel validates output before sleeping, revalidates it after waking, and
copies it before collecting the child. Even a failed copy after the child exits
leaves its result available for a later valid wait. A successful wait consumes
exactly one completion; waiting for it again returns `-RUM_ECHILD`. Each process
has its own child-completion event, so another parent's child exit does not
wake it. PIT ticks do not wake these waiters.

## Requesting termination

`rum_terminate(pid, flags)` (20) requests cancellation. With flags zero, only a
direct child is permitted. It cannot target the caller, its parent, a sibling,
an unrelated process, or the supervised shell. The kernel marks the target and
wakes it if blocked; the target closes its handles and exits on its own stack
at a safe boundary. Synchronous kernel I/O can finish before cancellation.

The 24-byte `rum_terminate_request` contains `version`, `size`, `process_id`,
`flags` and `reserved[2]`. Use lifecycle version 1, the exact size, zero reserved
words and zero unused syscall registers. PID must be 1 through
`RUM_ABI_PID_MAX`. Zero returns mean the request was accepted, including an
already pending cancellation or an authorized child that has already exited.
An existing exit result is preserved. A collected/unknown PID returns
`-RUM_ESRCH`; an existing unauthorized target returns `-RUM_EACCES`.

`RUM_TERMINATE_SESSION` is explicit delegated authority for a command launched
by the initial shell's `rum_run_command`. Only that marked command while it
owns the foreground may use the flag, and only against another direct child of
its shell. It cannot cancel the shell, itself, another session's children or
arbitrary descendants. Ordinary spawned children do not inherit this authority.
This supports later shell process tools without making PID knowledge a right
to terminate a process. Only the owning parent may wait for the result.

## Parent exit, limits and identity lifetime

A parent owns its uncollected child results. Exited children retain their slot,
directory pin, address space and kernel stack until a successful wait collects
them. Exit itself closes handles. The 16 worker/process slots include runnable,
waiting and exited records; failing to wait can exhaust the registry.

If a parent exits, faults or is cancelled, the kernel adopts its entire
remaining descendant tree into boot's cleanup ownership, requests cancellation
of live members and discards abandoned completions. This is a cleanup owner,
not a public process a program can wait through. Each child terminates at a safe
boundary; inactive stacks and address spaces are then reaped automatically.
Cleanup can finish after the parent's result is returned. Applications cannot
leave a daemon running by exiting its parent. A shell exit additionally closes
its referenced console session and adopts any remaining session members for
cleanup, even after their parent links changed. Foreground return references
and the recovery supervisor remain valid throughout that cleanup.

PIDs are positive and monotonically allocated through `RUM_ABI_PID_MAX`.
Reaping frees a slot but never recycles its PID. Exhausting the PID or internal
task-identity namespace returns `-RUM_EOVERFLOW` on future launches until reboot;
registry capacity returns `-RUM_ENOMEM`. A failed publication consumes neither
identity. Stale IDs can never select a later occupant of a reused slot.

The new calls append syscall numbers without changing the original version-1
ABI, `RUN` version-1 packets, version-2 command packets or result layout. Existing
ELFs keep working. `RUN` uses the same launch, child-wait and collection
primitives to preserve its synchronous foreground behavior.

## Standalone commands and session control

The supervised initial shell can use `rum_run_command(path, args, text, result)`
instead of `rum_run`. It uses syscall 13 with a 32-byte `rum_command_request`:
version `RUM_COMMAND_ABI_VERSION` (2), the exact structure size, the same path,
arguments and result addresses, flags `RUM_RUN_COMMAND`, a `text` address, and
zero `reserved`. The original version-1 packet remains supported unchanged.

`text` is the unparsed argument tail, at most 255 printable ASCII bytes plus
NUL. It preserves spaces for `echo` and `write`, while `argc` and `argv` retain
the usual split argument layout. The kernel validates and copies it before
launch. Ordinary programs cannot request this launch mode.

The direct command child may call `rum_command_text(buffer, capacity)` (16).
It validates the full writable range and returns bytes including NUL. A short
or zero capacity returns `-RUM_ERANGE`; other processes receive `-RUM_EACCES`.

`rum_session(action, path)` (17) accepts `RUM_SESSION_CHDIR` with a bounded path,
or `RUM_SESSION_EXIT`/`RUM_SESSION_RECOVERY` with a null path. The unused third
register must be zero. The caller must be the direct foreground command child
of the blocked initial shell; nested and ordinary children are denied.
`CHDIR` changes only the waiting shell's directory using normal path validation.
An exit request is applied after the command ends and its launch syscall has
reaped the child and freed temporary buffers. Existing restart/recovery rules
then apply. This interface exposes no parent pointer, arbitrary PID or kernel
address. `run cd /disk` is denied because `run` launches an ordinary nested child;
type `cd /disk` directly.

## Yielding and sleeping

`rum_yield()` (21) gives up the caller's current turn and returns zero when it
runs again. It can return immediately if no other task is runnable. All three
syscall argument registers must be zero.

`rum_sleep(milliseconds)` (22) blocks only the caller until its relative delay
expires. Zero milliseconds performs the same voluntary yield without arming a
deadline. Positive delays support 1 through `RUM_SLEEP_MAX_MS` (86,400,000 ms,
24 hours); larger values, including `UINT32_MAX`, return `-RUM_ERANGE` without
waiting. The request never allocates a timer object or a wait node.

```c
for (unsigned step = 0; step < 10; ++step) {
    /* Do a small amount of application work. */
    if (rum_sleep(100) < 0) return 1;
}
```

The current PIT runs at approximately 100 Hz. A positive delay rounds up to
whole ticks and includes one extra tick to cover the unknown starting phase.
For example, 1 ms waits for two ticks, normally about 10–20 ms; 100 ms waits for
eleven ticks, normally about 100–110 ms. Timer delivery, synchronous kernel I/O
and other runnable tasks can delay resumption further. These are coarse sleeps
with no real-time scheduling guarantee. Use a blocking wait for a condition
rather than polling it in a yield loop.

The 24-byte `rum_sleep_request` in `<rum/abi/time.h>` contains six 32-bit words:
`version`, `size`, `milliseconds`, `flags` and `reserved[2]`. Use
`RUM_SLEEP_ABI_VERSION` (1), the exact size and zero flags/reserved words. ECX
and EDX must be zero. The kernel copies and checks the whole readable packet
before registering a deadline; it accepts a packet across mapped pages or in
read-only user memory. Invalid packets return `-RUM_EINVAL` and invalid ranges
return `-RUM_EFAULT`. There is no output pointer.

On cancellation, a sleeper becomes runnable with its deadline detached, then
terminates at the existing safe return boundary. The caller's stack, address
space and open handles remain owned until that termination and later collection.
The parent receives `RUM_PROCESS_CANCELLED`. Kernel wait helpers return to their
owning continuation so temporary resources can be released before cancellation
takes effect. There is no user signal handler that retries the sleep.

Sleep and yield append calls to ABI version 1 without changing the existing
process or filesystem packet layouts. The standalone shell `sleep` command is
part of the later process-tools work; the SDK calls are available now.

## Console actions

`rum_console(action)` uses syscall 15. `RUM_CONSOLE_CLEAR` clears the text area
and emits the serial clear-screen sequence. `RUM_CONSOLE_SNAKE` runs the existing
kernel Snake application synchronously until Q returns to the caller. Ctrl+C
returns to the supervised shell, or cleans up the game and cancels a child.
No coordinates, port numbers, queue pointers or kernel pointers are accepted.
Unknown actions and nonzero unused syscall arguments return `-RUM_EINVAL`.

Console writes count bytes accepted from the program; serial output renders LF
as CRLF, as kernel text output does. File writes preserve bytes exactly.

## Tests

`make test-console-sessions` drives real PS/2 input through nested foreground
launches, competing readers, child faults, failed loads, cancellation during
construction and filesystem work, and output from simultaneous writers. It also
boots the normal supervisor through three shell generations with live children
and verifies recovery with zero remaining session references. Both boot paths
and 16/64 MiB are covered; see [Console sessions](console-sessions.md#testing).

`make test-sleep` checks the actual PIT deadline scan, kernel event waits and
public sleep/yield calls through ELF and GRUB ISO boot at 16 and 64 MiB. It covers
zero and maximum durations, conversion overflow, clock wrap and a deadline of
zero, event/timeout races, broadcast waits, unrelated sleepers, slot reuse,
cancellation before registration and after expiry, and repeated sleep/exit
cycles. QEMU injects real PS/2 Ctrl+C into a long sleep. Resource and wait counts
must return to baseline; logs are under `build/test-artifacts/sleep/`.

`make test-lifecycle` runs the public SDK calls through real ring-3 programs
and separately checks identity exhaustion. Both fixtures boot through ELF and
GRUB ISO at 16 and 64 MiB. Tests cover packet/flag validation, cross-page
structures and buffers, failed loads and construction, nohang and any-child
waits, retained and stale results, termination permissions, delegated shell
control, parent exit/fault/cancellation, process-limit pressure and cleanup.
A fixture revokes output write access while a wait is blocked and verifies
that a failed copy does not discard the completion. Private counters are seeded
near exhaustion only inside the isolated boundary fixture; rum exposes no
counter-setting API. Logs are under `build/test-artifacts/lifecycle/`.

`make test-userspace-shell` checks actual ring-3 launch packets, invalid headers,
paths and cross-page buffers, nested foreground children, signed statuses,
faults, cancellation and object/handle/page baselines. It also drives the normal
shell through QEMU's PS/2 keyboard on both boot paths, compares FAT file bytes
with host tools, and checks shell restarts and recovery. Logs are saved under
`build/test-artifacts/process-syscalls/` and `build/test-artifacts/userspace-shell/`.
