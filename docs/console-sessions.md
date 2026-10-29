# Console sessions

The normal shell owns one console session. Its foreground program receives
keyboard input until it exits, faults or is cancelled. If that program launches
another foreground program, input follows the new child and returns to its
immediate parent afterward. Ctrl+C cancels only the active foreground child;
it does not cancel the waiting parent or unrelated processes.

At the interactive shell prompt, Ctrl+C is an input character that clears the
current line. The shell cannot be cancelled through the ordinary child-control
interface. Snake uses the same foreground ownership checks while its kernel
engine runs.

## Input and output

`rum_spawn` creates a nonforeground child. It joins its parent's session but
receives no console ownership. A positive-length read on standard input returns
`-RUM_EACCES` immediately, even when keys are queued. Console clear and Snake
requests also require ownership. Such a child cannot acquire the console by
calling `rum_run`; that launch returns `-RUM_EBUSY` before loading a program.
Zero-length reads retain their existing no-transfer behavior.

Standard output and standard error remain writable by every process. Each
syscall accepts at most 128 bytes and renders that chunk to VGA and COM1 without
switching tasks between devices. Interrupts remain enabled. Separate writes
can interleave, and a line longer than one chunk is not atomic. There is no
per-process terminal, output redirection or prompt-redraw protocol yet.

The keyboard queue is shared. Ownership is checked again after an input wait
wakes, before any character is removed. Keys left after a child's exit remain
available to its parent. Closing an interactive shell session discards queued
keys so an unfinished line cannot become a command in its successor or in
recovery. Scan-code decoder and modifier state are preserved.

Cancellation records a request and wakes a blocked owner. It does not destroy
an active stack or interrupt a filesystem operation. A synchronous disk request
may finish, including a write, before cancellation takes effect. Repeated Ctrl+C
requests continue to target the cancelling owner until it relinquishes input.

## Shell commands and restart

Only the interactive shell can grant command authority through
`rum_run_command`. That grant belongs to its direct foreground child and the
same referenced session. While the shell waits for that command, it may request
a shell directory change, exit or recovery. Ordinary `RUN` children, spawned
children and nested commands cannot change the shell's state. A process can
still change its own directory with `rum_chdir`.

When the shell ends, the kernel closes its session, revokes command authority,
and cancels and adopts every remaining session member for cleanup. Handles
close on each child's own exit path; stacks, address spaces and directory pins
are reclaimed after switching away. Children from a closing session cannot
take input or control a newly started shell.

The supervisor restarts a shell that exits or faults. Three short-lived shell
exits enter kernel recovery; a session lasting at least 30 seconds resets that
failure window. A missing or invalid initial executable enters recovery
immediately. `recovery` requests recovery directly. The new shell starts at `/`;
RAM edits survive until reboot and flushed disk edits retain their normal
persistence guarantees.

## Kernel ownership

`kernel/process/session.c` owns the bounded session pool and active-console
reference. Each process embeds a stable `console_member`. Membership retains a
session; a session retains its root and current owner; a nested foreground
launch retains the previous owner through an explicit return reference.
Background children hold membership only. No public ABI contains these pointers.

Session updates use short interrupt-disabled sections and perform no allocation,
I/O or waits. Process publication attaches membership and, for the initial shell,
marks its protected role in the same update. Construction rechecks cancellation
and ownership after preparing its stack and working directory; refusal releases
the temporary resources without publishing a child.

Exit restores a live previous owner before publishing completion. It removes
return references to exiting ancestors and clears all ownership when a session
closes. Collection only acknowledges and reaps an exited child; it cannot move
input away from a newer owner. The reaper refuses a process still referenced by
a session. Closed sessions remain allocated until their last member detaches.

The recovery `diag` command reports live and closing sessions, member count,
session references and process references. After all userspace processes have
been reclaimed in recovery, every session count should be zero.

## Testing

Run `make test-console-sessions` in Ubuntu, or through WSL from Windows. The
fixtures boot through direct ELF and GRUB ISO at 16 and 64 MiB. Real PS/2 input
checks competing readers, shell Ctrl+C, nested cancellation returning to a
parent, a child fault with queued input, failed loads, serialized output chunks,
and cancellation during construction and synchronous filesystem work.

A separate test boots the normal kernel supervisor with a test shell that
leaves a live sleeping child with open handles before exiting or faulting.
Three generations must reach recovery with no processes or session references
left; recovery commands, RAM files and Snake must still work. Tests use a
separate boot archive and never attach or modify a developer disk. Logs are
saved under `build/test-artifacts/console-sessions/`.

`make test-host` also checks nested reference lifetimes, cancellation-pending
owners, closed sessions coexisting with successors, bounded pool exhaustion and
repeated reuse. `make test` includes both suites.
