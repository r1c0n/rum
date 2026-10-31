# Shell and commands

The normal shell runs from `/RUM/SHELL.ELF` on the system disk. Its prompt shows
the current directory, such as `/>` or `/DOCS>`. Type `help` for commands.
Input uses a US keyboard layout. Enter runs a command, Backspace edits, and
Ctrl+C cancels the foreground program or clears the current line.

## Files

```text
ls /
mkdir /DOCS
cd /DOCS
write NOTES.TXT hello from rum
cat notes.txt
pwd
cd ..
rm DOCS/NOTES.TXT
rm DOCS
```

Files are persistent on the root FAT16 disk. `/RUM` holds compiled programs
on the same writable volume. Names use case-insensitive 8.3 spelling and
listings use uppercase. `..` stops at `/`; there are no separate `/disk` or
memory-backed mounts. See [paths](filesystems.md) and [system disk](system-volume.md).
Binary file bytes are escaped by `cat` rather than interpreted as console
control codes. `write` creates or replaces a file with the supplied text.

## Programs and processes

Bare command names search `/RUM` first, then the current directory. Explicit
paths bypass this search. A name without an extension may omit `.elf`.

```text
hello
run hello
start spin
ps
kill <pid>
start sleep 2000
```

Foreground commands own input until they exit, fault or are cancelled. The
shell then reports their status and restores its prompt. `start` launches
without waiting; completion is reported once. `kill` requests cancellation of
a process owned by the shell session. See [process tools](process-tools.md).

Command programs use only the public syscall ABI. `start` is shell syntax;
`exit` and `recovery` are built-in session controls so they remain available
when disk errors prevent loading programs. Quoting, pipelines, redirection,
history and completion are not implemented.

## Recovery and diagnostics

`recovery` leaves the normal session and enters the kernel recovery shell.
`exit` asks the supervisor to restart the normal shell. Repeated early exits
or a missing/invalid shell executable cause recovery automatically.

Recovery provides `help`, `echo`, `clear`, `about`, `mem`, `diag`, file commands,
program launch and Snake. File commands use the disk when available; without
a usable disk they report that the filesystem is unavailable. Diagnostics and
Snake remain usable. No temporary files are substituted for missing storage.

`make user` rebuilds programs in `build/user/system/`; it does not update an
existing disk. Create a new image with `prepare-disk`, or back up an existing
image before deliberately replacing installed files using host FAT tools.
