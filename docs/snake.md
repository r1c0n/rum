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

Snake saves its best score in `/SNAKE.DAT` on the root disk when you quit,
restart, lose or win. The file contains decimal digits and a newline. It is
read again when the next game starts and survives VM restarts.

```text
cat /SNAKE.DAT
```

Without a usable writable disk, Snake remains playable but cannot save a new
best score. Failed saves leave the current game's best score in memory. Once
storage is full or faulted, preserve the image and use host FAT tools to inspect
or repair a copy rather than repeatedly trying writes.

## Contributor notes

`kernel/recovery/snake_model.c` contains deterministic game rules without display, heap,
or filesystem dependencies. The body uses a fixed-capacity coordinate ring.
Food selection starts from a seeded pseudo-random cell and scans for free space.

`kernel/recovery/snake.c` owns the heap-allocated session, controls, score loading/saving,
and VGA rendering. It draws without moving the shell cursor or overwriting the
uptime row. The userspace shell selects Snake with `rum_console`; its kernel
syscall loop uses PIT ticks and routes input until Q or Ctrl+C returns. Recovery
uses its original foreground loop. IRQ handlers never update game state.

Keep model rules independent from I/O, preserve allocation cleanup on every exit
path, and make score replacement atomic. Run `make test-host` for rule or
controller changes and `make test` for timing, keyboard, and display changes.
