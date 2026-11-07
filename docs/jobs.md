# Background jobs

End a command or pipeline with `&` to keep the prompt available while it runs.
The shell prints a job number. Use the numbers shown on your system in place
of the examples below.

```text
cat INPUT.BIN | cat > COPY.BIN &
jobs
wait 1
spin &
fg 2
```

| Command | Behavior |
| --- | --- |
| `jobs` | List running and completed jobs, their names and stage PIDs |
| `fg <job>` | Give a job foreground input, wait for it, report its result and collect it |
| `wait <job>` | Wait and collect a job without giving it keyboard input |
| `wait` | Wait and collect every job in the current snapshot |

`fg` and `wait` accept either `2` or `%2`. Job numbers and process PIDs are
different identifiers. `kill` still takes a PID; cancelling one stage also
cancels the remaining stages of its job. `jobs`, `fg` and `wait` are standalone
programs in `/RUM`, using public session interfaces. Run them as plain foreground
commands, rather than in a pipeline or with redirection.

The older `start <program> [arguments]` command retains its PID-based behavior
for compatibility. Its children appear in `ps` and are collected automatically;
they are outside the job table. Use `&` for programs you want to manage through
`jobs`, `fg` and `wait`.

## Input and cancellation

Background programs can write to the console and use files and pipes. A
positive-length console read returns access denied without consuming a key.
There is no stop/resume behavior: programs that want to wait for foreground
input must handle that error themselves. Pipe and file reads do not require
keyboard ownership.

`fg` gives console input to the job's original first stage, provided it has
not exited. Input is never reassigned
to a later stage. Ctrl+C cancels every active stage of that foreground job,
including stages blocked in pipe I/O. The controlling `fg` program and shell
remain alive, and input returns after the last stage finishes. Unrelated
background jobs keep running.

`wait` leaves its target in the background. Ctrl+C cancels the waiting command
and returns to the prompt; it does not cancel that background job. At the
prompt, Ctrl+C clears the edited line. Synchronous kernel disk work remains
bounded but can delay cancellation and scheduling.

Job start/completion notices fit in one console write. A background completion
wakes the shell, prints one automatic notice and redraws the edited input.
Arbitrary program output may still interleave with the prompt or other output.

## Results, limits and recovery

An ordinary pipeline uses its rightmost stage's exit status. A user fault wins
over cancellation; either condition cancels the other live stages. The kernel
keeps each stage's result, including signed exit status or fault vector, after
the process itself is reaped. Fault results contain no addresses.

Automatic notices do not discard results. Completed jobs remain in `jobs`
until an explicit `wait` or `fg` collects them. Those commands report the result
even if a completion notice was already shown. `fg` on a completed job reports
and collects it without assigning input to a stale process. Collected numbers
and numbers from another shell session are rejected.

Each session has eight job records and each job has at most four stages. Both
running and completed records occupy a slot. A full table reports an error
before opening a redirection target or starting a process; use `wait` to free
completed records. The existing process, handle, pipe, memory and file limits
also apply. A failed partial launch cancels and collects its started stages,
closes the parent's descriptors and discards its job record. Earlier filesystem
changes follow the [redirection failure rules](shell-workflows.md).

Shell exit, fault, recovery or restart abandons its job records, cancels and
adopts live stages, and reclaims them through their normal exit paths. A new
shell starts with an empty table. Records and event storage remain pinned
until any active wait has unwound. No stage from a closing session can take
input from its successor.

`diag` in recovery reports live job records and active waits. After userspace
cleanup, job, process, session, descriptor and pipe counts all reach zero.

## Public interface

Include `<rum/user.h>`. Syscall 28 uses a 48-byte `rum_job_request` with
`RUM_JOB_ABI_VERSION` 1. Its words are version, size, operation, job_id, flags,
entries address, capacity, count, total, name address and two reserved words.
Every reserved, unused or output-only input word must be zero. The kernel checks the entire
writable packet and output range, including page crossings, before changing
job state. Output storage cannot overlap the packet. Failure leaves user
outputs unchanged.

| Runtime call | Contract |
| --- | --- |
| `rum_job_create(name, flags)` | Initial shell only; reserve a building job and return a positive ID |
| `rum_job_commit(id)` | Initial shell only; seal a nonempty launch before waiting |
| `rum_job_cancel(id)` | Seal and request cancellation of the job's live stages |
| `rum_job_list(entries, capacity, total)` | Copy up to eight entries; zero capacity queries the total |
| `rum_job_wait(id, flags, entry)` | Return 1 with a completed result, or 0 for NOHANG; does not consume it |
| `rum_job_foreground(id)` | Give input to the original first stage and target Ctrl+C at the job |
| `rum_job_release(id)` | Collect an ended job and any unreaped stage records |
| `rum_job_ack(id)` | Initial shell only; acknowledge its automatic completion notice |

CREATE accepts only `RUM_JOB_FOREGROUND`; WAIT accepts only
`RUM_JOB_WAIT_NOHANG`. Other flags are zero. CREATE uses a nonempty printable
name of at most 63 bytes, with no entries/capacity. LIST uses job ID zero and
capacity 0–8. WAIT requires a nonzero job ID and capacity one. Other operations
use only their nonzero job ID.

Each 220-byte `rum_job_entry` contains job ID, state, flags, stage count, an
aggregate `rum_process_result`, four per-stage results and a 64-byte name.
Aggregate status is meaningful only when DONE; a live stage has termination
zero. States are BUILDING, RUNNING and DONE; flags identify foreground,
acknowledged notice and cancellation. IDs increase without reuse; exhausting
the signed-positive range returns EOVERFLOW.

Only the initial shell creates jobs or joins stages using mapped spawn's
`RUM_SPAWN_JOB` flag. One job may be under construction per shell, and JOB and
legacy PIPELINE spawn flags cannot be combined. A cancelled construction
rejects further launches with EINTR; publication completed during cancellation
returns an already cancelled child PID for cleanup. The kernel attaches job
membership before that child can run.

The shell and its marked direct foreground command may list, wait, focus,
cancel and release that session's jobs. Ordinary spawned programs have no
such authority. The command grant remains valid when another child completion
wakes the shell's synchronous RUN continuation. A nested foreground scope,
waiting on an unsealed job or releasing a live job returns EBUSY. A full job
table returns EJOBFULL. Invalid packets return EINVAL/EFAULT; inaccessible or
stale job IDs return ESRCH to an authorized controller.

## Focused checks

```powershell
.\rum.ps1 test -Suite jobs -Jobs 4
```

```sh
./rum test --suite jobs -j4
make test-jobs-host
```

Host tests cover result ordering, session identity, stale IDs, table bounds,
input leases, cancellation, parser limits and pinned wait lifetimes. QEMU
tests use disposable FAT disks at ELF/16 MiB and ISO/64 MiB for background
binary pipelines, cross-page syscall packets, foreground input, concurrent
completions, faults, Ctrl+C during pipe/disk work, table exhaustion and restart
cleanup. Host FAT tools verify exact bytes and cluster ownership. Logs are
under `build/test-artifacts/jobs/`; developer disks are never attached.
