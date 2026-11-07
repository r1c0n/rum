# Redirection and pipelines

The shell connects programs using their standard input and output. Programs
remain separate user ELFs; the kernel provides file handles, pipes and process
control.

```text
echo hello > NOTES.TXT
echo another line >> NOTES.TXT
cat < NOTES.TXT
cat INPUT.BIN | cat | cat > COPY.BIN
```

| Operator | Effect |
| --- | --- |
| `< path` | Read standard input from an existing file |
| `> path` | Create an output file or truncate an existing closed file |
| `>> path` | Create an output file or append each write at its current end |
| `|` | Connect the preceding program's output to the following program's input |

Spaces around operators are optional. Names and paths follow the usual FAT
8.3 rules. The shell accepts up to four stages, 32 arguments per stage including
the program name, and eight redirections across a line. The input line limit is
255 bytes. A malformed or oversized line starts no process and opens no file.
End a command or pipeline with `&` to run it as a [background job](jobs.md).
Quoting, descriptor prefixes such as `2>` and shell expansion are not supported.

## Bytes and errors

`cat [path]` reads a file, or standard input when the path is omitted. Console
output displays text safely: CRLF and LF become line endings, tabs remain tabs,
and unsupported controls and binary bytes are escaped. File or pipe output
preserves every byte, including NUL, CRLF and a missing final newline. `cat -b
[path]` explicitly requests byte-preserving output even on the console.

Standard error stays on the console. Redirecting stdout does not put file or
process diagnostics into the output file. Programs should write diagnostics to
handle 2 and loop over short reads and writes. See [pipes](pipes.md) for the
512-byte atomic-write limit and EOF/broken-pipe rules.

Redirections override pipe connections and are processed left to right within
each stage, from the first stage to the last. For example, `echo hello > A.TXT
> B.TXT` empties A.TXT and sends output to B.TXT. `echo hello > A.TXT | cat`
sends no bytes through the pipe, so `cat` reaches EOF. Every redirection is
prepared before any stage is launched. An earlier successful creation or
truncation remains in effect if a later open or launch fails.

An ordinary pipeline reports the last stage's status. An earlier nonzero exit
does not override a successful last stage. A user fault or cancellation stops
the remaining stages, reaps all of them, and prints one fault/cancellation
message. Setup errors also cancel and reap every stage already started.

## Input, cancellation and lifetime

All stages are launched before the shell waits. Each inherits only standard
handles 0–2; the shell closes its copies of the pipe endpoints promptly. This
lets the last writer produce EOF and the last reader produce broken-pipe
errors without a hidden shell reference keeping either end alive.

The first stage alone may read console input. Later stages read a pipe or an
explicitly redirected file. Input is never reassigned to a later stage when
the first stage exits. Ctrl+C cancels every stage in the current foreground
scope, while leaving the shell and unrelated background children alive.
Cancellation is latched during setup so a stage loaded during a keyboard IRQ
cannot escape the request. The shell restores its input after collecting all
stage results. A bounded synchronous disk operation can delay cancellation.

Plain `cd`, `exit`, `recovery` and `start` keep their existing behavior. They
cannot use pipeline/redirection syntax. Pipeline stages do not receive authority
to change the shell's directory, restart it, or launch a nested foreground
command through `run`. Invoke the program itself as a stage instead.
`jobs`, `fg` and `wait` also require a plain foreground command invocation.

## File-open API

`rum_open` adds these flags to the existing version-1 packet, without changing
its layout or existing zero-flag behavior:

| Flag | Contract |
| --- | --- |
| `RUM_OPEN_CREATE` | Create a missing regular file; parents must exist |
| `RUM_OPEN_TRUNCATE` | Empty an existing unpinned regular file; combine with CREATE to allow a missing file |
| `RUM_OPEN_APPEND` | Select EOF and write within one serialized filesystem operation |

Each flag requires write access and cannot be combined with DIRECTORY. Unknown
bits are rejected before filesystem access. Read-only volumes reject mutation;
backends missing required write, create or truncate operations return ENOSYS.
Truncate-open returns EBUSY if the existing identity is already open, including
an alias of that identity. Creation never replaces an existing file. An append
description can be duplicated or inherited; seek affects its read position but
each later append still selects EOF. Separate append descriptions cannot select
the same stale end position. Atomicity covers one write syscall, not a series
of calls or an entire application record larger than that call.

A committed prefix advances the description's offset. A simultaneous I/O error
is retained for the next nonempty transfer, as with ordinary file writes.
Existing size limits and [FAT16 write/flush guarantees](fat16.md) still apply.
Metadata changes committed before a failure cannot be rolled back, and neither
redirection nor append adds journaling or crash recovery.

`rum_isatty(handle)` (syscall 27) returns 1 for console input/output descriptions,
0 for files, directories and pipes, or a negative error. Extra arguments must be
zero. It performs no device or filesystem access and does not confer ownership.

The shell uses the [session job API](jobs.md) for foreground and background
workflows. The older foreground-only scope remains available through `rum_session` actions
`RUM_SESSION_PIPELINE_BEGIN` and `RUM_SESSION_PIPELINE_END`, using a NULL path.
Mapped spawn with `RUM_SPAWN_PIPELINE` joins a direct child to that scope.
Ordinary processes cannot use these controls. A nested scope or ending one
with live stages returns EBUSY; a mapped launch after scope cancellation returns
EINTR. Construction that completes during cancellation publishes an already
cancelled child and returns its PID for collection. Stream mappings retain
their usual validation and rollback rules. These controls cover one foreground
workflow and cannot overlap a foreground job.

## Focused validation

```powershell
.\rum.ps1 test -Suite workflows -Jobs 4
```

```sh
./rum test --suite workflows -j4
make test-workflows-host
```

The host checks cover parser limits, open modes, append offsets and partial
errors, reference lifetimes, cancellation during setup and scope reuse. QEMU
uses disposable FAT disks at ELF/16 MiB and ISO/64 MiB. It compares binary output
larger than a pipe buffer, concurrent append records, empty files, redirection
order and rebooted bytes through host FAT tools. It also checks failed middle
launches, faults, real keyboard input/Ctrl+C, unrelated child completion,
read-only/full media, and zero resource counts in recovery. Logs are under
`build/test-artifacts/workflows/`. Developer disks are never attached.
