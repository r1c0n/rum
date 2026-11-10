# Application runtime

rum applications use a small, single-threaded C runtime built separately from
the kernel. Include `<stdlib.h>`, `<string.h>`, `<stdio.h>` and `<errno.h>` for
the supported C functions, and `<rum/user.h>` for direct OS operations.
The build searches `sdk/include/c/` for the C headers and `sdk/include/` for
the public rum interface. Neither the host C library nor kernel objects are
linked into an application.

## Allocate and resize memory

```c
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char *text = malloc(64);
    if (!text) return 1;
    strcpy(text, "An island of our own.");
    char *grown = realloc(text, 128 * 1024);
    if (!grown) {
        free(text); /* A failed realloc leaves the original allocation intact. */
        return 1;
    }
    free(grown);
    return 0;
}
```

`malloc`, `calloc` and `realloc` return storage aligned to 16 bytes. Allocation
failure returns `NULL` and sets `errno`; it does not terminate the process.
`calloc` checks multiplication overflow and clears the requested bytes even
when it reuses previously written storage. Oversized requests fail with
`ENOMEM` before attempting a mapping.

The zero-size policy is explicit: `malloc(0)` and a zero-element `calloc`
return `NULL` without setting an error. `realloc(NULL, size)` acts as `malloc`;
`realloc(pointer, 0)` frees that allocation and returns `NULL`. `free(NULL)`
does nothing. `free` preserves `errno`. Pass only a live allocation returned
by this allocator to `free` or `realloc`; foreign pointers, interior pointers
and double frees are application errors.

The allocator requests private read/write regions through the
[memory-region API](virtual-memory.md). Small allocations share 64 KiB arenas;
larger ones use page-rounded regions. If the normal arena size cannot fit,
a small allocation retries with only the pages it needs. All backing memory
and allocator metadata consume the existing process page and region budgets.
Other direct region allocations consume those same budgets.

Free blocks are split and adjacent free blocks coalesce. `realloc` can shrink
in place or grow into an adjacent free block. Otherwise it copies the original
contents to a new allocation before freeing the old one. New bytes from
`malloc` or growth are unspecified; use `calloc` when zeroing is required.

A completely empty arena returns to the kernel immediately. If region release
is temporarily denied, it stays available for reuse. Partly occupied arenas
retain their backing pages; shrinking an individual allocation does not
release part of a region. Do not manually protect or release allocator-owned
regions through the direct memory API.

The kernel releases the complete process address space on normal exit, fault
or cancellation, including allocations the program did not free.

## File streams

`FILE` is opaque. The runtime supplies `stdin`, `stdout`, `stderr`, `fopen`,
`fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `rewind`, `fgetc`, `fputc`,
`getchar`, `putchar`, `fgets`, `fputs`, `puts`, `feof`, `ferror`, `clearerr`,
`fflush`, `remove` and `rename`.

```c
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 1;
    unsigned char header[12];
    int failed = fread(header, 1, sizeof header, file) != sizeof header;
    if (fclose(file)) failed = 1;
    return failed;
}
```

Modes `r`, `w`, `a`, `r+`, `w+` and `a+` accept an optional `b` before or after
`+`. Other modes are rejected before opening a descriptor. `w` creates or
truncates a regular file. `a` creates if needed and every write goes to EOF,
including after a seek. Text and binary modes both preserve exact bytes:
the runtime does not translate LF, CRLF, NUL or other controls.

Streams are unbuffered. Reads and writes retry short successful transfers
until the requested amount completes, EOF is reached or an error occurs.
The return value is the number of complete elements; a partial final element
can still consume or write bytes. Size multiplication overflow sets
`EOVERFLOW` without I/O. EOF and errors have separate, sticky indicators.
`clearerr` resets both; successful `fseek` clears EOF, and `rewind` clears both.

`fseek` uses a signed target `long` offset; `ftell` fails with `EOVERFLOW` when
the position cannot fit a target `long` (32 bits). Use `rum_seek` for 64-bit
positions. Seeking a pipe fails with `ESPIPE`. File paths, naming, open-object
rules and permissions follow the [filesystem ABI](filesystem-syscalls.md).
`rename` requests replacement of a closed destination file under those rules.

`fflush` invokes the backend flush operation for regular output files.
Consoles and pipes have no runtime output buffer to drain. `fflush(NULL)`
flushes every open output stream. `fclose` attempts flush and close even when
flush fails, and returns `EOF` on failure; the stream becomes invalid either
way. A failed kernel close remains subject to process teardown. No flush adds
journaling or stronger guarantees than the
[filesystem's documented write ordering](fat16.md).

The stream table has 32 entries, including standard streams. The kernel's
32-descriptor limit also includes descriptors opened directly with `rum_open`,
so mixing the two interfaces can reach that limit earlier. Avoid closing or
replacing a stream's descriptor through direct calls while its `FILE` remains
in use.

## Strings and formatting

The runtime implements byte operations `memcpy`, `memmove`, `memset`, `memcmp`
and `memchr`; string length, copy and comparison through `strlen`, `strnlen`,
`strcpy`, `strncpy`, `strcat`, `strcmp` and `strncmp`; and searching through
`strchr`, `strrchr` and `strstr`. `strdup` allocates a copy with `malloc`.
Lengths and capacities count bytes, not Unicode characters. Overlapping
copies require `memmove`.

`printf`, `fprintf`, `vfprintf`, `snprintf` and `vsnprintf` support `%d`, `%i`,
`%u`, `%o`, `%x`, `%X`, `%p`, `%c`, `%s` and `%%`, flags `-`, `+`, space, `#`
and `0`, width/precision including `*`, and integer lengths `hh`, `h`, `l`,
`ll` and `z`. Floating point, wide characters, `%n` and other conversions are
unsupported and return `-1` with `EINVAL`. Return counts exceeding `INT_MAX`
fail with `EOVERFLOW`. Earlier stream output may already have been written
when a later conversion or I/O fails.

`snprintf` returns the number of bytes the complete output would need, without
the final NUL. A nonzero capacity always leaves a NUL-terminated buffer.
`snprintf(NULL, 0, ...)` measures output without storing it. Formatting does
not allocate heap storage.

This is a C runtime subset, not a complete hosted C library or POSIX layer.
Locale, floating-point formatting, scanning, environment variables, signals,
`atexit`, constructors and CPU-time/wall-clock functions are not provided.

## Timing, errors and exit

Use `rum_monotonic_ms(&milliseconds)` for elapsed milliseconds since PIT
initialization, and `rum_sleep(milliseconds)` for relative waits. The clock
is independent of console ownership and is neither UTC nor process CPU time.
Its 100 Hz resolution is nominal; it follows the programmed PIT frequency.

`rum_clock` exposes the underlying 64-bit tick snapshot through a 32-byte
`rum_clock_request`. Initialize `version = RUM_CLOCK_ABI_VERSION`,
`size = sizeof request` and every other field to zero before each query.
Syscall 35 takes that writable packet in EBX; ECX and EDX must be zero.
Success returns zero and fills `ticks_hi:ticks_lo` and `tick_hz` together.
The kernel extends the scheduler's wrapping 32-bit tick counter; applications
do not have to sample it regularly to detect wraparound. Invalid packets or
inaccessible output leave storage unchanged. Existing syscall numbers retain
their meaning.

Direct `rum_*` calls return negative public error values and do not set
`errno`. C wrappers return their documented sentinel (`NULL`, `EOF`, `-1` or
a short element count) and set positive `errno` on errors. Numeric errno
values match rum's public error constants, not host libc values. Successful
calls need not clear a previous error.

Returning from `main` calls `exit`, which attempts `fflush(NULL)` before
`rum_exit`. `abort` terminates with failure without flushing. A direct
`rum_exit`, fault or cancellation does not run C cleanup, although kernel
resource cleanup still runs. Check `fflush` and `fclose` explicitly when an
application must report write failures; `exit` cannot report flush failures
back to its caller.

Allocator state, stream state and `errno` are private to each process and
currently assume one userspace thread. They have no locks, thread-local
storage or signal-handler reentrancy contract. Thread support must address
those shared states before applications use the runtime concurrently within
one process.

## Try the example

Build user programs, then prepare a new disk or install `HEAPDEMO.ELF` into
`/RUM` on a backed-up disk while the VM is stopped. A build does not update
existing disks. At the shell:

```text
heapdemo
heapdemo /README.TXT
```

The example grows a 256 KiB allocation to 768 KiB, checks its preserved data,
frees it and optionally reads a file through stdio. It prints elapsed time and
returns failure if an operation fails. It does not change the file.
