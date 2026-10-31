# Running concurrent programs

Use `start` to launch a program without waiting for it. The shell immediately
returns to the prompt and prints the new process identifier:

```text
/> start spin
[3] started spin.
/> ps
PID  PARENT  STATE  NAME
1  0  waiting  shell.elf
3  1  ready  spin.elf
4  1  running  ps.elf
/> kill 3
Cancellation requested for 3.
[3] spin: cancelled.
/> start sleep 2000
[6] started sleep.
```

Identifiers and row order vary. `ps`, `kill` and `sleep` are standalone programs
in `/rum`. `start` is shell syntax because the shell must own and collect its
concurrent children. Bare program names search `/rum` first, then the working
directory; explicit paths bypass that search. Arguments use the existing
space/tab splitting rules.

## Input and completion

A started program has its own address space, working-directory reference and
fresh handles. It can use files and print to standard output/error, but cannot
read keyboard input, clear the console or start a foreground child. An attempted
input read returns access denied. Ctrl+C at the prompt clears the line; use
`kill` to cancel a background child. Foreground programs still receive Ctrl+C
normally, and nested foreground cancellation returns to the immediate parent.

When a child exits, faults or is cancelled, the shell collects its result and
prints one completion notice. It wakes even if no key is pressed. If a line is
being edited, the shell prints a new prompt and redraws that line intact.
Program output may interleave with the prompt or other programs: individual
console chunks of up to 128 bytes are serialized, not whole lines or messages.

There are no background job numbers, `&` syntax, pipelines, redirection, history
or job-control signals. The bracketed number is the process's PID.

## Process tools

`ps` shows user processes only, including the shell and `ps` itself. Rows contain
the PID, parent PID, state and executable basename. Parent zero means a kernel
parent or adoption for cleanup. States are `ready`, `running`, `waiting`,
`exited`, or `cancelling`. An exited process remains visible until its parent
collects its result. Names are printable bounded labels, not complete paths;
use the PID to select a process. No kernel addresses are exposed.

`kill <pid>` requests cancellation of a direct child of the same shell. It runs
with the shell command's delegated authority. It cannot cancel the shell,
another session, an unrelated process or a nested descendant. A successful
request does not mean cleanup has already finished; synchronous disk work can
complete before cancellation takes effect. The shell reports completion when
the process actually ends.

`sleep <milliseconds>` accepts a decimal duration from 0 to 86400000 (24 hours).
Zero yields once. Positive durations round up to PIT ticks with a phase guard;
scheduling and synchronous kernel work can delay return. `sleep 1000` waits
about a second in the foreground; `start sleep 1000` leaves the prompt available.
Ctrl+C cancels a foreground sleep. Invalid numbers and overflow are rejected.

## Limits and recovery

The registry has 16 slots shared by user processes and kernel workers. BOOT
and idle have separate slots. Asynchronous launches leave one slot available
for a foreground tool such as `ps`, `kill` or `recovery`. With the usual shell
and uptime worker, up to 13 concurrent children fit. Nested foreground launches
need additional free slots. Memory limits can refuse a launch earlier.

A failed start publishes no child. Finished children are reaped without waiting
for another command, making their resources and slots available again. PIDs are
monotonic and are not reused during a boot. Restarting or exiting the shell
cancels its remaining session children; they cannot become detached daemons.
See [Console sessions](console-sessions.md) for shutdown and input ownership.

## Tests

`make test-process-tools` boots through direct ELF and GRUB ISO at ELF/16 MiB and ISO/64 MiB.
It runs CPU-bound children while executing commands, verifies public listing and
parent relationships, checks scoped cancellation and invalid numeric arguments,
and tests sleep cancellation. It checks completion without keyboard activity,
partial-line redraw, rapid completions, background exhaustion, a reserved control
slot, slot reuse and shell restart with live children. Serial logs are under
`build/test-artifacts/process-tools/`.

`make test-lifecycle` additionally checks malformed process-list packets,
cross-page headers and entries, read-only/unmapped buffers, arithmetic limits,
overlapping output and truncated snapshots alongside lifecycle resource ledgers.
