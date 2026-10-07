# Process and console syscalls

Include `<rum/user.h>` and link with rum's user runtime. The normal shell uses
these calls to launch programs and manage the console without accessing kernel
objects or device registers.

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

Exit, user fault and cancellation close the child's handles. The parent restores
foreground ownership, releases the remaining directory reference and private
pages, and returns with its own offsets and working directory intact. Input
still queued when a child exits becomes available to its parent; keyboard
input is not buffered separately per process.

Ctrl+C targets the active child. A blocked input syscall wakes and observes the
request; a CPU-bound program observes it at an interrupt return to user mode.
Filesystem callbacks finish in task context before cancellation, so a disk
write already in progress can commit. `RUN` returns a cancellation result rather
than cancelling the waiting parent. The kernel marks only the supervised initial
shell as interactive: Ctrl+C reaches that shell as a character for line editing.

There are no background jobs, independent wait syscall, process-handle inheritance
or general asynchronous kill interface.

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

`make test-userspace-shell` checks actual ring-3 launch packets, invalid headers,
paths and cross-page buffers, nested foreground children, signed statuses,
faults, cancellation and object/handle/page baselines. It also drives the normal
shell through QEMU's PS/2 keyboard on both boot paths, compares FAT file bytes
with host tools, and checks shell restarts and recovery. Logs are saved under
`build/test-artifacts/process-syscalls/` and `build/test-artifacts/userspace-shell/`.
