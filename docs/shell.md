# Command loop (milestone 4)

rum now buffers keyboard input into a command line and runs it on Enter.
The kernel remains an unreleased `0.1.0`. Start `./rum.ps1 run`, click inside
QEMU, and enter `help` at the `> ` prompt.

| Command | Behavior |
| --- | --- |
| `help` | List the four commands and their descriptions |
| `clear` | Clear the 24 console rows, reset the cursor, and print a fresh prompt |
| `about` | Show rum's version, unreleased status, and kernel information |
| `echo <text>` | Print the remaining text followed by a newline |

`clear` preserves the bottom uptime row and current text color. On COM1 it also
emits ANSI erase/home sequences for serial terminals that support them. Serial
log files retain those escape bytes and the earlier output.

## Input rules

Command names are lowercase and matched exactly. Leading spaces are ignored;
spaces between the command name and its arguments are skipped. `echo` preserves
spaces within and after its text. Without text, `echo` prints a blank line.
The other three commands accept no arguments and show `Usage: <command>` if
given any. Blank or whitespace-only lines return a prompt. Unknown commands
show their name and suggest `help`.

Backspace removes one buffered character and erases it on VGA and COM1. It can
cross a wrapped line and cannot erase the prompt. Tab inserts four spaces.
The 256-byte buffer holds at most 255 characters plus a terminating zero; at
capacity, further characters are ignored and are not echoed. Backspace makes
room again. Every Enter resets the line for the next command.

Text is literal: there is no quote processing, variable expansion, command
chaining, history, completion, or cursor navigation. There are no external
programs or processes. This is a kernel command loop using the existing US
PS/2 input driver.

## Implementation and checks

`kernel/shell.c` owns editing, parsing, built-ins, and the prompt. The main loop
calls `shell_receive` after restoring CPU interrupt flags. IRQ1 only queues
characters; it never runs commands. The PIT keeps counting during output, and
the main loop refreshes uptime and uses its existing interrupt-safe idle path.
The shell uses fixed storage and does not need a heap or host C library.

`./rum.ps1 test` / `make test` check the real shell and VGA driver with captured
serial output. Cases cover commands, exact name matching, whitespace, usage
errors, empty `echo`, literal text, Backspace, wrapped input, full-buffer editing,
line reset, scrolling, and clear/status preservation. QEMU tests type commands
through the emulated PS/2 keyboard in both GRUB ISO and direct ELF boots,
compare VGA and serial results, and verify input and timer delivery after clear.
Existing CPU-fault and IRQ tests remain included. Generated logs, memory dumps,
and shell screenshots are in `build/test-artifacts/`.
