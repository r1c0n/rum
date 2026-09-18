# ASCII Snake (milestone 7)

rum remains at unreleased `0.1.0`. Start `./rum.ps1 run`, click inside QEMU,
and enter `snake` at the shell prompt. `snake` accepts no arguments.

## Play

| Key | Action |
| --- | --- |
| W / A / S / D | Move up / left / down / right |
| P | Pause or resume |
| R | Restart with a fresh board and zero score |
| Q | Quit and return to the shell |

Uppercase keys work too. This milestone uses the existing character queue;
arrow keys remain ignored by the keyboard decoder. A turn can be queued while
paused. Only one perpendicular turn is accepted per movement step, and directly
reversing direction is rejected, including rapid input that would reverse twice
before the timer advances.

The head is `@`, the body is `o`, food is `*`, and walls are `#`. Eating grows the
snake and adds one point. Hitting a wall or your own body ends the game. A move
into the departing tail is allowed when not growing. Filling all 640 cells wins.
Game over and victory keep the board visible; R restarts and Q leaves.

The 40x16 board fits inside the 80x24 console. The bottom uptime row remains
visible, and the PIT and keyboard IRQs continue running during play and pause.
Movement advances one cell every 150 ms. Delayed updates take one step rather
than a burst of catch-up steps. The VGA hardware cursor is hidden during play
and restored on exit. Quitting clears the board, prints score/best, and returns
a fresh `> ` prompt. RAM files and shell commands continue working.

## Best score

The best score lasts for the current boot and is also saved to `snake.score`
in the RAM filesystem on quit, restart or game over when it increases. Try
`cat snake.score` after earning a point. A valid existing file can seed the
best score; it contains decimal digits with an optional final newline, up to
the maximum score of 637. Malformed or oversized values are ignored.

The file is temporary, like other RAM files. A reboot restores embedded assets.
If saving fails because the filesystem/heap is full, the in-memory best remains
available and the original file stays unchanged. A later quit/restart can retry.
Starting without enough heap space prints an error and leaves the shell usable.

## Implementation

`kernel/snake_model.c` owns deterministic game rules without console, IRQ, heap
or filesystem calls. A fixed-capacity ring stores body coordinates. A seeded
32-bit generator chooses a starting food cell, then a bounded scan finds an
unoccupied cell, including on a nearly full board.

`kernel/snake.c` owns a heap-allocated game state, display, controls and RAM
score file. `terminal_put_at` draws individual colored characters without
changing the console cursor/color, rejects coordinates outside 80x24, and
cannot overwrite uptime. `terminal_cursor_visible` controls the hardware cursor.
Rendering goes to VGA; COM1 receives start/pause/restart/score/end events instead
of a complete board every frame. Play uses the PS/2 keyboard, not serial input.

The shell dispatches characters to the active game and returns to normal parsing
on quit. `shell_tick_due` and `shell_tick` integrate timed movement with the
existing foreground loop. The loop snapshots queued input/ticks with interrupts
disabled, restores flags before handling input/rendering, and idles with `sti; hlt`
when no work is due. Unsigned elapsed-tick arithmetic handles counter wraparound.
Fresh ticks after input prevent a just-started game from seeing an older snapshot.
IRQ handlers never run the game, allocate memory or redraw the screen.

The game allocation is released on quit, including after restart or game over.
Heap pages may remain mapped for reuse under the existing heap policy; the RAM
score file owns its separate node/data allocations.

## Checks

`make test` / `./rum.ps1 test` run model checks for deterministic food, growth,
reversal/rapid-turn rejection, wall/body/tail collisions, ring wraparound,
nearly full food placement and full-board victory. Host shell checks use the real
VGA driver, heap, filesystem and controller to cover timed movement, pause,
restart, quit, tick wrap, malformed score files, saving, launch OOM, full-filesystem
save failure, retry and allocation cleanup. Console tests cover positioned drawing,
boundaries, unchanged cursor/color and status preservation.

Normal GRUB/direct ELF QEMU boots use actual PS/2 keys and PIT ticks to pause,
steer to real food, grow, save/read the score, collide with a wall, restart,
quit and run another shell command. They inspect VGA borders/body/food/score,
verify ticks continue while paused, and walk actual heap headers to check that
game state is freed. Existing memory, file, CPU-fault and IRQ checks remain.
Screenshots/logs/dumps are in `build/test-artifacts/`, including `iso-snake.ppm`
and `elf-snake.ppm`.
