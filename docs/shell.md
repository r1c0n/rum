# Shell and commands

The normal shell runs from `/RUM/SHELL.ELF` on the system disk. Its prompt shows
the current directory, such as `/>` or `/DOCS>`. Type `help` for commands.
Input uses a US keyboard layout. Enter runs a command. Ctrl+C cancels the
foreground program or clears the current line.

## Editing and completion

| Key | Action |
| --- | --- |
| Left / Right | Move within the command |
| Home / End | Move to the start or end of the command |
| Backspace / Delete | Remove the character before or at the cursor |
| Up / Down | Browse history; Down past the newest entry restores the draft and cursor |
| Page Up / Page Down | Browse older output or return toward the live screen |
| Tab | Complete a program or path, or list matching names |

Typing inserts at the cursor. The prompt itself cannot be edited. History keeps
the last 16 nonblank commands in memory, skips consecutive duplicates and resets
when the shell restarts. Editing a recalled command does not change its stored
entry. Background output and job notices appear above a redrawn prompt, keeping
your unfinished command and cursor position.

Scrollback keeps the last 256 scrolled text rows, including their colors, until
reboot. Page Up/Page Down move 23 rows at a time. The cursor is hidden while
viewing old output, and the uptime bar stays live. Typing or another editing key
returns to the current prompt without changing the unfinished command. New job
notifications also return to the live prompt. `clear` clears the visible text
but keeps it in scrollback. Fullscreen user applications do not add their
canvases to shell scrollback.

For focused checks, use `.\rum.ps1 test -Suite editing -Jobs 4` on Windows or
`./rum test --suite editing -j4` in Ubuntu/WSL. These use disposable disks.

Tab works at the end of a word, including an unfinished quoted word. Program
names search `/RUM` and the current directory, with `.ELF` optional; arguments
and redirection targets complete files and directories. This also works after
`run`, `start`, or a pipe. Directory matches end in `/`. A unique file or program
adds a space at the end of the line. Multiple matches extend a common prefix;
press Tab again to list them. Each request examines at most 128 directory entries
and retains at most 16 matches. If a limit is reached, it lists the retained
matches without choosing one. Type a longer prefix or enter a directory to
narrow the search. Filesystem naming, path and traversal limits still apply.

## Quotes and arguments

Spaces and tabs separate arguments. Single quotes preserve their contents
literally. Double quotes also group text; inside them, or outside quotes, a
backslash makes the next printable character literal. It is not a C escape:
`\n` means the letter `n`, not a newline. A single quote cannot contain another
single quote; join quoted and escaped fragments instead. Adjacent fragments
form one argument, and `''` or `""` supplies an empty argument.

```text
echo "an island of our own"
echo 'a  b' "" c
echo \| \& \> \\ \"
write "NOTES.TXT" "keep  these spaces"
cat 'NOTES.TXT' | cat > "COPY.TXT"
```

Unquoted `<`, `>`, `>>`, `|` and a trailing `&` are operators; quote or escape
them to pass literal characters. An unquoted semicolon is rejected. Unclosed
quotes, a trailing backslash, missing commands or redirection targets, and
oversized input are rejected before any file is opened or process launched.
The line holds 255 characters; further typing is ignored until space is freed.
There are at most 32 arguments per stage, four stages and eight redirections.
Quotes do not enable spaces or other unsupported bytes in FAT filenames.
Variables, globbing, command substitution and scripts are not supported.

`echo` joins its parsed arguments with one space and adds a newline. `write`
takes the first argument as the path and joins the rest with one space, without
adding a newline. Spaces inside an argument are preserved; separators between
arguments are normalized. `echo 'a  b' "" c` therefore writes `a  b  c` followed
by a newline. Unlike the earlier raw-tail behavior, extra unquoted separator
spaces no longer survive. Use quotes when that spacing matters. The same rules
apply to ordinary commands, pipes, redirection and nested `run` launches.

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
`cat` displays tabs and both LF and Windows CRLF line endings normally. Binary
bytes and unsupported controls, including a standalone carriage return, are
escaped; the file stays unchanged. `write` creates or replaces a file with the
supplied text.

Use `touch`, `cp`, `mv` and `stat` to manage regular files. Copies and moves
refuse an existing destination unless `-f` is supplied. `setup` explicitly
creates `/HOME` and `/APPS`; see [file tools and workspace](file-tools.md)
for examples, read-only/full-media behavior and running copied applications.

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
when disk errors prevent loading programs. Use `<`, `>`, `>>` and `|` to connect
programs and files; see [redirection and pipelines](shell-workflows.md) for
examples, limits, byte-preserving `cat` output and cancellation rules.
End a command or pipeline with `&`
to create a [background job](jobs.md), then use `jobs`, `fg` and `wait` to manage it.

## Recovery and diagnostics

`recovery` leaves the normal session and enters the kernel recovery shell.
`exit` asks the supervisor to restart the normal shell. Repeated early exits
or a missing/invalid shell executable cause recovery automatically.

Recovery provides `help`, `echo`, `clear`, `about`, `mem`, `diag`, file commands,
program launch and Snake. File commands use the disk when available; without
a usable disk they report that the filesystem is unavailable. Diagnostics and
Snake remain usable. No temporary files are substituted for missing storage.
Recovery retains its basic command input; quoting, cursor editing, history and
completion described above belong to the userspace shell.

`make user` rebuilds programs in `build/user/system/`; it does not update an
existing disk. Create a new image with `prepare-disk`, or back up an existing
image before deliberately replacing installed files using host FAT tools.
