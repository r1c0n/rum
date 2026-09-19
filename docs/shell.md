# Shell

Boot rum, click inside the QEMU window, and type `help` at the `> ` prompt.
The shell uses a US QWERTY layout and runs commands when you press Enter.

If QEMU captures your mouse or keyboard, press `Ctrl+Alt+G` to release it.

## Commands

| Command | Description |
| --- | --- |
| `help` | List commands and short descriptions |
| `clear` | Clear the console and print a fresh prompt |
| `about` | Show the rum version and platform |
| `echo <text>` | Print text followed by a newline |
| `ls` | List RAM files and sizes |
| `cat <name>` | Display a RAM file |
| `write <name> [text]` | Create or replace a file; omit text for an empty file |
| `rm <name>` | Remove a RAM file |
| `mem` | Show heap and RAM-file usage |
| `diag` | Show tasks, processes, paging, stacks, and ownership counts |
| `snake` | Start ASCII Snake |

Example session:

```text
> write notes.txt hello from rum
> cat notes.txt
hello from rum
> ls
> rm notes.txt
```

Files are temporary. Restarting rum restores the embedded files and discards
interactive edits. See [Heap and RAM files](storage.md) for filename and size
limits.

## Editing input

- Backspace removes the previous character, including across a wrapped line.
- Tab inserts four spaces.
- Shift and Caps Lock work for supported US QWERTY keys.
- A command line can hold 255 characters. Extra input is ignored until you
  remove characters.
- Blank or whitespace-only lines simply print another prompt.

Command names are lowercase and exact. Leading whitespace and whitespace before
arguments are skipped. `echo` and `write` preserve spaces inside and after their
text argument.

The shell does not implement quoting, variables, wildcard expansion, pipelines,
redirection, command chaining, history, completion, or cursor navigation. Every
command is a kernel built-in.

## File display

`cat` prints newline and tab normally. Other control and binary bytes appear as
dots so they cannot disrupt the VGA console. This changes only the display; the
RAM filesystem preserves the original bytes. A final newline is added to the
screen when the file does not already end with one.

## Diagnostics

Use `mem` for a quick storage summary. Use `diag` when investigating task or
memory behavior. In a normal shell session, boot is the running task, idle is
runnable or sleeping, and no user process is published. See
[Kernel diagnostics](diagnostics.md) for every field.

The bottom VGA row shows uptime and remains visible while the console scrolls or
is cleared.

## Snake input

While Snake is active, normal shell editing is suspended and keys go to the
game. Press Q to leave and return to a new prompt. See [ASCII Snake](snake.md)
for the complete controls.

## Troubleshooting

- **No keys appear:** check the serial boot output for a PS/2 setup failure. The
  timer and serial console can still work while IRQ1 remains masked.
- **A command says “Usage”:** run `help`; commands other than `echo` and `write`
  accept only the arguments shown in the table.
- **A file is gone after reboot:** runtime files are RAM-only. Add permanent boot
  files under `assets/ramfs/` and rebuild.
- **`cat` shows dots:** the file contains binary or control bytes.
- **The prompt appears frozen during Snake:** press Q; game mode owns keyboard
  input until it exits.

## Contributor notes

`kernel/shell.c` owns line editing, parsing, command dispatch, and the prompt.
IRQ1 only queues decoded characters. The foreground loop calls `shell_receive`
with interrupts restored, so commands may allocate and use task-aware APIs.

Keep parsing bounded by the fixed line buffer, keep command errors explicit,
and never move command execution into the IRQ handler. Run `make test-host`
after changing parsing or editing and `make test` after changing keyboard or
foreground-loop integration.
