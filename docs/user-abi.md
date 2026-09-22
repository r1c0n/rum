# User ABI and ELF programs

rum builds freestanding i386 user executables with a separate startup, runtime,
linker script, and public include tree. The kernel validates and maps stripped
ELF files from its RAM filesystem, constructs their initial stacks, enters the
prepared process in ring 3, recovers user faults, and serves ABI version 1
through the production syscall dispatcher. The normal shell launches one
foreground process with `run`, waits for its result, and reclaims its complete
address space and kernel task state before restoring the prompt.

## Building user programs

```sh
make user
```

PowerShell users can run:

```powershell
./rum.ps1 user
```

The build produces:

| Path | Purpose |
| --- | --- |
| `build/user/debug/<name>.elf` | Symbol-rich executable for GDB and `addr2line` |
| `build/user/debug/<name>.map` | Linker map |
| `build/user/ramfs/<name>.elf` | Stripped, validated runtime asset |
| `build/user/include/rum/abi/` | Generated public headers only |

Private kernel headers are deliberately absent from the user include tree.
User code links with `-nostdlib`, rum's startup and syscall wrappers, and target
`libgcc`; it does not link a host C library or any kernel object.

## Adding a program

1. Add `user/programs/name.c` with a normal
   `int main(int argc, char **argv)` entry.
2. Include `<rum/user.h>` for the supported runtime calls.
3. Add `name` to `USER_PROGRAMS` in the Makefile.
4. Run `make user` and fix any compiler or ELF-validator error.
5. Debug with the ELF under `build/user/debug/`, not the stripped asset.

A minimal program looks like this:

```c
#include <rum/user.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static const char text[] = "hello from userspace\n";
    return rum_write(RUM_STDOUT, text, sizeof text - 1) < 0;
}
```

`write` may complete partially. Production programs should loop until all bytes
are written or an error is returned; `user/programs/hello.c` shows the complete
pattern.

## Launching a program

At the rum prompt, use a build name or the full RAM filename:

```text
> run hello first second
Hello from rum userspace!
Process 1 exited with status 0.

> run nonzero
Process 2 exited with status -37.
```

The shell treats spaces and tabs as argument separators and does not implement
quotes or escapes. The typed program name becomes `argv[0]`. If the exact RAM
filename is absent, `run` tries the same name with `.elf` appended.

Only one foreground child is supported. Its parent sleeps on a process-exit
event while the child owns console input. Normal exit preserves the full signed
status. A user exception preserves its vector, error code, fault address, EIP,
and user stack pointer. Ctrl+C records a cancellation request; a blocking child
is woken, and a CPU-bound child observes the request on the next timer or
keyboard return before ring 3 resumes. Every outcome switches away from the
child before releasing its user pages, private tables, directory, guarded
kernel stack, and task record.

## Loading an executable

Normal kernel builds merge the stripped files from `build/user/ramfs/` with the
ordinary files from `assets/ramfs/`. Symbol-rich ELFs and linker maps stay under
`build/user/debug/` and are not embedded.

`elf_load_ramfs` reads one of those borrowed RAM-file images and applies the same
checks as `elf_load_process`. The loader validates the complete ELF and argument
packet before creating an address space. It then allocates distinct zeroed pages
for each LOAD segment, copies only file-backed bytes, applies final read/write
permissions, maps the fixed 64 KiB stack, and builds `argc`, `argv`, and an empty
`envp` at a 16-byte-aligned ESP.

On success, the returned `struct task_process` is complete but unpublished. The
caller transfers its address space to `task_create_process`; if publication is
not attempted or fails, the caller must destroy the space. A load failure leaves
the output empty and rolls back its directory, page tables, and user pages.

## Syscall convention

ABI version 1 uses `int 0x80`:

| Register | Meaning |
| --- | --- |
| EAX | Syscall number on entry; signed result on return |
| EBX | First argument |
| ECX | Second argument |
| EDX | Third argument |

Every general register except EAX is preserved by the kernel return contract.
Flags are unspecified. The user C primitive also preserves the usual i386
callee-saved registers.

| Number | Wrapper | Arguments | Result |
| --- | --- | --- | --- |
| 0 | `rum_exit(status)` | Signed exit status | Does not return |
| 1 | `rum_read(handle, buffer, capacity)` | Writable user buffer | Bytes read |
| 2 | `rum_write(handle, buffer, bytes)` | Readable user buffer | Bytes written |
| 3 | `rum_getpid()` | None | Positive process ID |

Handles 0, 1, and 2 are standard input, output, and error. A nonnegative result
means success. A negative result is the negation of a `RUM_E*` value from
`include/rum/abi/error.h`; these values are rum-specific and do not set a global
`errno`.

Read and write calls may return fewer bytes than requested. A zero-length call
returns zero without using the buffer. The current console backend transfers at
most 128 bytes per call, so callers must handle partial results.

`rum_write` accepts standard output and standard error. It verifies the complete
arithmetic range, then checks and copies only the chunk it will transfer through
a kernel buffer. Console code and drivers never receive a raw user pointer.

`rum_read` accepts standard input and requires a writable user range. If no
decoded character is queued, the calling process sleeps on a keyboard-specific
event. Keyboard IRQs remain enabled while it waits, PIT ticks continue, and
other runnable tasks can execute. The call returns after copying one or more
available characters, up to its 128-byte limit.

An unsupported number returns `-RUM_ENOSYS`, an invalid standard handle returns
`-RUM_EBADF`, and an overflowing, unmapped, supervisor-only, or wrongly
protected buffer returns `-RUM_EFAULT`. These checks return an ABI error rather
than turning bad user input into a kernel fault.

## Process arguments and initial stack

The launch packet `struct rum_arguments` contains copied bytes, never kernel or
host pointers. Limits are:

- 1–32 arguments, including `argv[0]`.
- 4096 total string bytes, including NUL terminators.
- Tightly packed strings described by offsets from the inline byte array.

At ELF entry, ESP is 16-byte aligned and points to:

```text
argc
argv[0] ... argv[argc - 1]   user addresses of NUL-terminated strings
0                            argv terminator
0                            empty envp
padding and argument strings
```

There is no return address or auxiliary vector. `_start` clears DF and EBP,
extracts argc/argv, aligns the C call stack, and invokes `main`. Returning from
`main` passes the full signed result to `rum_exit`.

The user stack occupies `0xbfff0000`–`0xc0000000`; the preceding page at
`0xbffef000` is always unmapped as a guard.

## Supported ELF format

The validator accepts a deliberately small subset:

- System V, little-endian ELF32.
- `ET_EXEC` for machine i386.
- Static fixed-address segments; no interpreter or dynamic linking.
- At most 16 program headers.
- Load addresses in `0x80000000`–`0xbfc00000`.
- Sorted nonempty LOAD segments with no overlapping pages.
- File size no larger than memory size.
- Entry point inside file-backed executable bytes.
- Separate executable, read-only, and writable pages; writable executable
  segments are rejected.
- No TLS, relocation runtime, shared libraries, constructors, or destructors.

The linker places text, constants, and data/BSS on distinct pages and emits a
nonexecutable GNU-stack descriptor. The loader zeroes BSS, segment padding, and
unused stack bytes before publication. i386 non-PAE paging has no NX bit, so
rejecting writable executable input remains a software policy.

Each stripped asset must also fit the RAM filesystem's 64 KiB per-file limit.
The debug and stripped ELFs are compared to ensure stripping did not change the
load image.

## CPU restrictions

User programs are compiled without x87, MMX, SSE, or SSE2. The kernel does not
save extended register state, so instructions that use it fault. Use integer
code and compiler-provided integer helpers from target `libgcc`.

User code cannot access hardware ports or supervisor pages. Initial EFLAGS are
exactly `0x202`: IF is enabled and IOPL, DF, TF, NT, VM, and optional flags are
clear.

## Troubleshooting

- **`run` cannot find a program:** use `ls` to confirm that `<name>.elf` is in the
  RAM filesystem, then rebuild with the name in `USER_PROGRAMS`.
- **`run` rejects an existing file:** the file is not a supported static i386 ELF
  or failed the runtime validator. Inspect the symbol-rich build with `readelf`.
- **A read appears to stop the process:** standard input is blocking. Type a
  supported character in the QEMU window or press Ctrl+C; timer interrupts
  continue meanwhile.
- **A write returns less than requested:** loop over the unconsumed bytes. The
  console backend currently copies at most 128 bytes per call.
- **Private kernel headers are missing:** user code may include only
  `user/include/rum/user.h` and copied `rum/abi/` headers.
- **The ELF checker rejects a segment:** inspect program headers with
  `i686-elf-readelf -l build/user/debug/name.elf` and check ranges, page overlap,
  alignment, and W+X flags.
- **A symbol is missing from the stripped asset:** use the matching debug ELF for
  symbols; stripping is intentional.
- **A floating-point operation faults:** extended CPU state is unsupported; keep
  the program integer-only.

Run `make test-user` after changing the public ABI, linker script, startup,
runtime, or ELF rules. Run `make test` after changing the kernel loader,
CPU entry/return, or syscall assembly.

## Reference

- [System V ELF program headers](https://gabi.xinuos.com/elf/07-pheader.html)
