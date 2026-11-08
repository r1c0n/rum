# Terminal API

User programs can draw colored text, position the cursor and wait for decoded
keyboard characters through `<rum/user.h>`. The kernel owns VGA memory, the
keyboard queue, hardware ports and input routing. Applications receive geometry,
palette indices, characters and clock observations instead of device addresses.

The current surface is 80 columns by 24 rows. The last physical display row is
reserved for the uptime/status bar and cannot be drawn by a user program. Query
capabilities before starting; do not assume a future terminal has the same size.

## An interactive session

```c
struct rum_terminal_capabilities caps;
if (rum_terminal_query(&caps) < 0) return 1;
if (rum_terminal_begin() < 0) return 1;
struct rum_terminal_cell cells[] = {
    {'h', RUM_COLOR_LIGHT_GREEN, RUM_COLOR_BLACK, 0},
    {'i', RUM_COLOR_LIGHT_GREEN, RUM_COLOR_BLACK, 0}
};
rum_result_t result = rum_terminal_cells(0, 0, cells, 2);
if (result >= 0) {
    struct rum_terminal_event event;
    result = rum_terminal_read(&event, 1, 1000, 0);
}
rum_result_t ended = rum_terminal_end();
return result < 0 || ended < 0;
```

BEGIN requires foreground input ownership and actual console descriptions at
standard input and output. Background programs and programs with either stream
redirected cannot acquire an interactive session. QUERY remains available to
all user processes. Other terminal operations require the caller's active
session and current foreground ownership.

BEGIN saves the text cells, cursor position, visibility and stream colors, then
clears the text area and hides the cursor. A nested BEGIN returns EBUSY without
replacing that saved state. END restores the saved screen and attributes.
Normal exit, a user fault, Ctrl+C and losing foreground input also restore it
automatically. A nested foreground launch ends the parent's terminal session;
after it returns, the parent must BEGIN again if it wants another session.
A failed launch that never transfers ownership leaves the session intact.

Restoration discards unread application characters without resetting keyboard
modifiers. This prevents queued game input from becoming a shell command. It
does not overwrite the reserved status row, including updates made while the
application was running.

Console output from the owner reaches VGA and COM1 normally. While an interactive
session owns the display, other processes' console writes still reach COM1 but
do not change the application's VGA cells or cursor. These bytes are not queued
for later replay. This keeps background output from corrupting a live drawing.
When no interactive session exists, normal console output behavior is unchanged.

## Runtime calls

| Call | Behavior |
| --- | --- |
| `rum_terminal_query(caps)` | Fill geometry, features, limits, clock and cursor/color state |
| `rum_terminal_begin()` | Acquire a foreground interactive session |
| `rum_terminal_end()` | Restore and release the caller's session |
| `rum_terminal_cells(x, y, cells, count)` | Draw a bounded horizontal span without moving the cursor |
| `rum_terminal_cursor(x, y, visible)` | Move within the text area and set visibility to 0 or 1 |
| `rum_terminal_color(foreground, background)` | Set palette indices for ordinary stream output |
| `rum_terminal_read(events, capacity, milliseconds, ticks)` | Read character events, optionally waiting for a bounded duration |

Cells contain character, foreground, background and a zero reserved byte. The
character must be printable ASCII (32–126), and colors use the 16 `RUM_COLOR_*`
palette indices. A request cannot cross a row or exceed 80 cells. Every cell is
validated before any of that request is drawn. Zero cells validates the
position/session but ignores the cell pointer. Drawing and cursor operations
reject the status row, out-of-range positions and arithmetic wraparound.

Input capacity is 1–32 events and the timeout is 0–60,000 milliseconds. Zero
polls once without waiting. A positive timeout blocks only the caller and
returns as soon as at least one event is available, or returns zero at its
deadline. A short read is normal; no events are fabricated to fill the capacity.
On zero or error, event storage stays unchanged. Nonempty output buffers are
validated in full before a character is consumed.

Each 16-byte event contains type, value, tick and zero reserved. The current
type is `RUM_TERM_TEXT_EVENT`: a decoded character press, including Enter,
Backspace and Tab, without kernel echo or line buffering. Arrow/navigation
keys, key releases and modifier-event records are not yet exposed. Ctrl+C
cancels a foreground child or job according to console policy; the supervised
shell instead receives character value 3.

Capabilities and successful reads provide a wrapping 32-bit tick observation
and tick rate (currently 100 Hz). Event ticks record dequeue time, rather than
hardware arrival time. Use unsigned subtraction for short elapsed intervals.
Positive waits round up to PIT ticks with a phase guard. Sequence-based waits
avoid lost key notifications and retain one absolute deadline after a wakeup;
unrelated PIT ticks do not poll the keyboard queue. Scheduling and bounded
synchronous kernel I/O can delay a return beyond its nominal deadline.

## Packet layout and validation

Syscall 29 uses `RUM_TERMINAL_ABI_VERSION` 1 and a 48-byte
`rum_terminal_request`: version, size, operation, flags, x, y, count,
timeout_ms, data address, now_ticks and two reserved words. The whole packet
must be writable. Initialize only the operation's input fields; every unused
field, reserved word and output-only now_ticks must be zero. Unused syscall
registers must also be zero.

- QUERY uses a writable 60-byte `rum_terminal_capabilities` output at data.
- BEGIN and END take no additional fields.
- CELLS uses x, y, count and readable cell storage at data.
- CURSOR uses x/y and flags 0 or 1 for visibility.
- COLOR uses x/y for foreground/background indices.
- READ uses count for event capacity, timeout_ms and writable event storage.

QUERY/READ output storage cannot overlap the request. Complete buffers may
cross accessible user pages. Invalid versions, fields, cells and geometry
return EINVAL; inaccessible or overlapping output ranges return EFAULT;
oversized waits return ERANGE. A missing input lease returns EACCES. Invalid
requests neither draw a partial span nor consume input, and errors leave user
outputs unchanged. Every success fills now_ticks. No public structure contains
a kernel pointer or hardware address.

The kernel uses one fixed saved screen and no per-operation allocations.
Updates run in task context; IRQ handlers only decode/queue input, signal
events and account time. Snapshot restoration runs with interrupts enabled
outside registry updates. `diag` reports `Terminal: normal` after cleanup.

The earlier `rum_console(RUM_CONSOLE_CLEAR)` call remains supported. The retired
`RUM_CONSOLE_SNAKE` selector returns ENOSYS to a foreground caller. Install the
current `/RUM/SNAKE.ELF` to run the [userspace game](snake.md).

## Focused checks

```powershell
.\rum.ps1 test -Suite terminal -Jobs 4
```

```sh
./rum test --suite terminal -j4
make test-terminal-host
```

Host tests exercise the actual VGA driver with a mapped test screen, including
saved cells, cursor/colors, status-row preservation, rejected contexts and
repeated cleanup. QEMU checks both ELF/16 MiB and GRUB ISO/64 MiB: invalid and
cross-page packets, untouched input after validation errors, protected canvas
bounds, timeouts, background denial, nested launches, user faults and Ctrl+C.
It plays user Snake under CPU load, compares score bytes through host FAT tools,
reboots to recover them, checks failed score saving on read-only media, and
boots recovery Snake without a disk. Artifacts are under
`build/test-artifacts/terminal/`; developer disks are never attached.
