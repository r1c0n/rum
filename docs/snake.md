# ASCII Snake

Type `snake` at the shell prompt to start the game.

| Key | Action |
| --- | --- |
| W / A / S / D | Move up / left / down / right |
| P | Pause or resume |
| R | Restart |
| Q | Save the best score and return to the shell |

Uppercase keys also work. Arrow keys are unsupported. The snake cannot reverse
direction, and only one turn is queued for each movement step. You may queue a
turn while paused.

## Rules

| Symbol | Meaning |
| --- | --- |
| `@` | Head |
| `o` | Body |
| `*` | Food |
| `#` | Wall |

Eating food grows the snake and adds one point. Hitting a wall or the body ends
the game. Filling every playable cell wins. Moving into the cell being vacated
by the tail is allowed when the snake is not growing.

The board is 40×16 cells and advances one cell every 150 ms. The uptime row
continues to update while the game is running or paused. After a loss or win,
press R to start again or Q to leave.

## Best score

The best score for the current boot is stored in the RAM file `snake.score`.
The game saves it when you quit, restart, lose, or win. Read it from the shell:

```text
> cat snake.score
```

The file contains decimal digits and may end with a newline. The largest valid
score is 637. Invalid existing contents are ignored.

Because the filesystem is RAM-only, the score resets after reboot unless you
place a valid `snake.score` in `assets/ramfs/` and rebuild the ISO.

If saving fails because the heap or RAM filesystem is full, the in-memory best
score remains available and a later quit or restart retries the write. The old
file is preserved by an unsuccessful replacement.

## Troubleshooting

- **Arrow keys do nothing:** use W, A, S, and D.
- **A turn was ignored:** one turn is accepted per movement step, and direct
  reversals are rejected.
- **The score disappeared:** runtime scores do not persist across reboot.
- **Q does not return immediately after a key burst:** queued keyboard input is
  processed in order; wait for the foreground loop to consume it.
- **The game cannot start:** a heap allocation failed. The shell remains usable;
  remove RAM files or reboot to restore the initial state.

## Contributor notes

`kernel/snake_model.c` contains deterministic game rules without display, heap,
or filesystem dependencies. The body uses a fixed-capacity coordinate ring.
Food selection starts from a seeded pseudo-random cell and scans for free space.

`kernel/snake.c` owns the heap-allocated session, controls, score loading/saving,
and VGA rendering. It draws without moving the shell cursor or overwriting the
uptime row. The foreground loop uses PIT ticks for movement and routes keyboard
input to the game while it is active; IRQ handlers never update game state.

Keep model rules independent from I/O, preserve allocation cleanup on every exit
path, and make score replacement atomic. Run `make test-host` for rule or
controller changes and `make test` for timing, keyboard, and display changes.
