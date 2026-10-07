# Shell

Boot rum, click inside QEMU, and type `help` at the `/> ` prompt. The prompt
shows the working directory: `/disk/DOCS> ` means you are in `/disk/DOCS`.
The normal shell is `/rum/shell.elf`, a userspace program. Each command in the
table below is a separate ELF in `/rum`. They use the same public syscall
interface as other programs. Input uses a US QWERTY keyboard layout.

If QEMU captures input, press `Ctrl+Alt+G` to release it.

## Commands

| Command | Description |
| --- | --- |
| `help` | List commands |
| `about` | Show the rum version and platform |
| `clear` | Clear the console |
| `echo <text>` | Print text followed by a newline |
| `ls [path]` | List a directory; defaults to the working directory |
| `cat <path>` | Display a file; escape binary and control bytes |
| `pwd` | Show the working directory |
| `cd [path]` | Change directory; defaults to `/` |
| `write <path> [text]` | Create or replace a file; omit text for an empty file |
| `mkdir <path>` | Create a directory on `/disk` |
| `rm <path>` | Remove a closed file or empty directory |
| `run <program> [args]` | Launch a foreground ELF and wait for its result |
| `snake` | Play ASCII Snake |
| `exit` | Exit and let the kernel restart the shell |
| `recovery` | Enter the kernel recovery shell |

## Files and directories

The root contains embedded RAM files. Changes there disappear on reboot.
The read-only `/rum` directory holds the shell, commands and example programs
from the [system boot image](system-volume.md). It needs no attached disk.
An attached, supported FAT16 image appears at `/disk`; changes there persist.
See [FAT16 disks](fat16.md) to create and attach an image.

```text
/> write notes.txt hello from RAM
/> cat /notes.txt
hello from RAM
/> cd /disk
/disk> mkdir DOCS
/disk> cd DOCS
/disk/DOCS> write NOTE.TXT hello from disk
/disk/DOCS> pwd
/disk/DOCS
/disk/DOCS> cat ./NOTE.TXT
hello from disk
/disk/DOCS> cd /
```

Relative paths start in your working directory. Absolute paths start at `/`.
Repeated `/`, `.`, and `..` work, but `..` cannot climb out of a mounted root:
use `cd /` to leave `/disk` or `/rum`. RAM files retain their flat namespace. Disk names
use uppercase-compatible 8.3 spelling, such as `NOTE.TXT` or `DOCS`.

`ls` adds `/` after directory names. `cat` prints newline and tab normally;
other control and binary bytes appear as `\x00`, `\xFF`, etc. This protects the
console without changing file bytes. Empty files produce no content. A missing
final newline is added for display.

`write` replaces the entire file with the supplied text, without adding a
newline. It does not append. Open files and active working directories cannot
be removed or replaced. Disk errors follow the [FAT16 write guarantees](fat16.md);
an interrupted write may need host repair.

## Running programs

```text
> run hello island guest
Hello from rum userspace!
Program exited with status 0.
> run nonzero
Program exited with status -37.
> run /disk/HELLO.ELF
Hello from rum userspace!
Program exited with status 0.
```

A bare name such as `hello` searches `/rum` first, then the working directory.
You can type `hello` directly; `run hello` also prints its exit status.
A name containing `/` follows normal path rules; use `./HELLO.ELF` for a program
in the working directory. If the exact file is absent, the loader tries `.elf`.
Executables must fit the supported static i386 ELF format and 64 KiB file limit.

The child inherits your working directory, starts with fresh standard streams,
and owns keyboard input until it exits. Its directory changes do not affect the
shell. The `cd` command explicitly requests a session directory change through
a restricted syscall; an ordinary child cannot change the shell's directory.
The shell reports nonzero exit status, a user fault, or cancellation and
then restores the prompt. Fault messages do not display kernel addresses.

Press Ctrl+C to cancel a running child, including one that never makes syscalls.
At the shell prompt, Ctrl+C discards the current line. `run readline` demonstrates
a child reading input; `run fault` and `run spin` demonstrate fault recovery and
cancellation.

## Editing and limits

Enter executes a line, Backspace removes a character, and Tab inserts four
spaces. Lines hold 255 characters; extra input is ignored until you erase some.
Commands are lowercase and exact. Leading whitespace is skipped. `echo` and
`write` preserve spaces inside and after their text.

Arguments split on spaces and tabs. The launch interface allows 32 arguments,
including the program name, and 4096 bytes of packed strings. Background jobs,
quoting, escaping, pipelines, redirection, history and completion are unsupported.

`snake.elf` launches the existing kernel game through a bounded console syscall.
Use WASD, P, R and Q; see [Snake](snake.md). The uptime row continues updating
while the shell waits for input. CPU-bound children can delay its display until
they yield or exit because scheduling is cooperative.

## Recovery and diagnostics

Use `recovery` to enter the kernel shell, or select **rum recovery shell** in
GRUB. Direct ELF boots accept `-append rum.recovery`. Recovery provides `mem`,
`diag`, the original RAM-file commands, `run`, and Snake. Its file commands use
the RAM namespace; disk commands belong to the userspace shell.

If `/rum/shell.elf` or its system image cannot load, boot enters recovery immediately. The kernel restarts
a shell that exits or faults; three exits with each session lasting less than
30 seconds enter recovery. `exit` requests a restart; `recovery` enters recovery
immediately. A restart resets the working directory to `/`, while file edits
survive for the current boot.

For testing another initial executable, pass `-append rum.shell=/rum/name.elf` to
a direct ELF boot. These options do not modify or reformat a disk image.

If keys do not appear, check the serial log for a PS/2 setup failure. If `/disk`
is unavailable, check that QEMU attached the image to the secondary IDE channel
and the volume passed FAT16 validation. A busy prompt during a child program or
Snake means that application owns input; interact with it or press Ctrl+C.

## Changing the shell

Edit `user/programs/shell.c` for line editing or command launch. Command behavior
lives in its own `user/programs/<command>.c`; adding a command does not require
editing the shell. Add it to `USER_PROGRAMS` and rebuild with `make` to update
the ISO, or `make user` to update just the standalone system image.
The shell includes only public headers and links only the user runtime. Drivers, RAMFS
objects and hardware addresses remain inside the kernel. The recovery command
loop lives in `kernel/ui/shell.c`.

Run `make test-userspace-shell` for real keyboard, filesystem and launch tests,
then `make test` for complete regression coverage.
