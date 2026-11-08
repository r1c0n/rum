# ASCII Snake

Type `snake` at the shell prompt to start `/RUM/SNAKE.ELF`. The game runs as a
protected user process using the public [terminal API](terminal.md).

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

The board is 40×16 cells and nominally advances one cell every 150 ms. CPU load
and synchronous kernel I/O can delay a move; the game does not burst through
missed moves to catch up. The uptime row
continues to update while the game is running or paused. After a loss or win,
press R to start again or Q to leave.

Q restores the previous shell screen, cursor and colors. Ctrl+C cancels the
program and performs the same terminal cleanup. Background or redirected game
launches are denied interactive access. Other programs can continue running
while Snake is active; their console output still reaches the serial stream.

## Best score

Snake saves its best score in `/SNAKE.DAT` on the root disk when you quit,
restart, lose or win. The file contains decimal digits and a newline. It is
read again when the next game starts and survives VM restarts.

```text
cat /SNAKE.DAT
```

With a read-only disk, the user game remains playable but cannot save a new
best score. The kernel recovery shell also retains its own Snake controller
for diskless or rejected-volume boots. Failed saves leave the current game's
best score in memory and do not prevent terminal restoration. Once
storage is full or faulted, preserve the image and use host FAT tools to inspect
or repair a copy rather than repeatedly trying writes.

## Contributor notes

`apps/snake/model.c` contains deterministic game rules without display, heap,
filesystem or kernel dependencies. The body uses a fixed-capacity coordinate ring.
Food selection starts from a seeded pseudo-random cell and scans for free space.

`apps/snake/main.c` owns the normal game loop, controls, frame buffer, score I/O
and rendering. It compiles with the separate user toolchain and SDK headers.
Timed character events drive movement; file operations use filesystem syscalls.
No game logic runs in an IRQ handler or through a kernel game syscall.

`kernel/recovery/snake.c` retains the fallback controller. The pure model is
compiled independently into the recovery kernel and user ELF; no game state or
function pointers are shared across the privilege boundary.

Keep model rules independent from I/O and follow the filesystem's documented
interrupted-write guarantees for score replacement. Run `make test-terminal`
for focused rule, timing, keyboard, cleanup and display checks. Existing system
disks need the rebuilt programs installed; `make user` alone does not replace
files on a disk. See [system disk setup](system-volume.md).
