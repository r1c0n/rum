# ASCII Snake

Enter `snake` at the shell prompt to play. The command takes no arguments.

| Key | Action |
| --- | --- |
| W / A / S / D | Move up / left / down / right |
| P | Pause or resume |
| R | Restart |
| Q | Return to the shell |

Uppercase keys also work. Arrow keys are unsupported. You can queue a turn
while paused, but only one turn is accepted per movement step. The snake cannot
reverse direction.

The head is `@`, the body is `o`, food is `*`, and walls are `#`. Eating food
grows the snake and adds a point. Hitting a wall or your body ends the game;
filling the board wins. Press R to play again or Q to leave.

The board is 40×16 cells, and movement advances one cell every 150 ms.
The uptime row stays visible. Quitting clears the board, shows your score,
restores the cursor, and returns to the shell.

## Best score

Best scores last for the current boot. A new record is written to the RAM file
`snake.score` on quit, restart, or game over. Use `cat snake.score` to read it
after earning a point. Rebooting discards the score along with other RAM edits.

An existing score file can seed the record. Its format is decimal digits with
an optional final newline, with a maximum value of 637. Invalid values are
ignored. If the heap or filesystem is full, the best score remains in memory
and the original file is preserved; a later quit or restart retries the save.
An allocation failure when starting the game leaves the shell usable.

## Implementation

`kernel/snake_model.c` contains the game rules independently of the display,
heap, and filesystem. A fixed-capacity ring stores body coordinates. Food
placement starts at a cell chosen by a seeded 32-bit generator and scans for
free space. Moving into the departing tail is legal when the snake is not
growing.

`kernel/snake.c` manages the heap-allocated game state, controls, score file,
and VGA display. `terminal_put_at` draws without changing the console cursor or
color and cannot overwrite uptime. The hardware cursor is hidden during play.
COM1 receives game events and scores rather than a board redraw on every step.
Input comes from the PS/2 keyboard.

The foreground loop routes keys to the active game and checks elapsed PIT ticks
for movement. Unsigned tick arithmetic handles wraparound. Delayed updates
advance one step without a burst of catch-up movement. The loop reads fresh
ticks after handling input so start and resume use the correct time.

IRQ handlers only update counters or queue input. Game state is freed on quit;
heap pages remain available for reuse, and the score file owns its separate
allocations.

## Tests

Model tests cover food placement, growth, turns, collisions, ring wraparound,
and full-board victory. Host tests exercise the controller, VGA, heap, and
filesystem together, including pause, restart, quit, tick wrap, failed saves,
retry, and allocation cleanup.

QEMU tests use real keyboard input and PIT ticks to reach food, grow, save a
score, hit a wall, restart, and return to the shell. They check the screen,
timer delivery during pause, and released game allocations. Screenshots are
saved as `build/test-artifacts/iso-snake.ppm` and `elf-snake.ppm`.
