# Shell

Start rum, click inside QEMU, and enter `help` at the `> ` prompt.
The shell reads a command line from the PS/2 keyboard and runs it on Enter.

| Command | Description |
| --- | --- |
| `help` | List commands and their descriptions |
| `clear` | Clear the console and print a fresh prompt |
| `about` | Show version and kernel information |
| `echo <text>` | Print text followed by a newline |
| `ls` | List RAM files and their sizes in bytes |
| `cat <name>` | Display a RAM file |
| `write <name> [text]` | Create or replace a file; omit text for an empty file |
| `rm <name>` | Remove a RAM file |
| `mem` | Show heap and filesystem usage |
| `snake` | Play ASCII Snake |

For example:

```text
write notes.txt hello from rum
cat notes.txt
rm notes.txt
```

Files are temporary. Rebooting restores the embedded files from `assets/ramfs/`.
See [heap and RAM files](storage.md) for filename rules and capacity.

## Input

Command names are lowercase and matched exactly. Leading spaces and spaces
before arguments are skipped. `echo` preserves spaces within and after its
text; without text it prints a blank line. `write` treats the text after the
filename the same way.

`help`, `clear`, `about`, `ls`, `mem`, and `snake` accept no arguments. `cat`
and `rm` require one filename. Incorrect arguments display a usage message.
Blank lines return a prompt, and unknown commands suggest `help`.

Backspace removes one character, including across wrapped lines, and stops at
the prompt. Tab inserts four spaces. A line holds up to 255 characters; further
input is ignored until Backspace makes room. Shift and Caps Lock work with the
US QWERTY keyboard layout.

Text is literal. The shell has no quoting, variable expansion, command chaining,
history, completion, or cursor navigation. All commands are kernel built-ins.

`cat` displays control and binary bytes as dots, except for newline and tab,
and adds a final newline when needed. The filesystem API preserves the original
bytes. `clear` resets the console cursor and preserves the uptime row and text
color. Serial output uses ANSI erase/home sequences, which remain in saved logs.

During [Snake](snake.md), keyboard input goes to the game. Press Q to leave it
and return to a fresh shell prompt.

## Implementation

`kernel/shell.c` handles editing, parsing, commands, and the prompt. The
foreground loop calls `shell_receive` with interrupts restored. IRQ1 only
queues characters, and the PIT continues counting while commands run.

Line editing uses a fixed 256-byte buffer. File commands use the kernel heap
and RAM filesystem. Timed game updates use `shell_tick_due` and `shell_tick`
in the same foreground loop.

## Tests

Host tests use the real shell and VGA driver with captured serial output. They
cover parsing, usage errors, literal text, line limits, wrapped editing,
scrolling, and clear/status preservation. QEMU tests enter commands through
the emulated PS/2 keyboard in GRUB and direct ELF boots and compare VGA and
serial output. Logs and screenshots are in `build/test-artifacts/`.
