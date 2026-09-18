# User ABI and executables

User sources live in `user/`. They build as separate static i386 executables
with their own startup, runtime, flags, linker script and objects. The normal
boot still runs the kernel shell; loading these programs as processes requires
the user mapping, syscall and process-lifetime interfaces on the roadmap.

## Building

```sh
make user       # Build and validate the runtime assets and debug executables
make test-user  # Also check malformed ELF inputs and symbol/header separation
make test       # Include ring-3 startup fixtures and the existing boot checks
```

In PowerShell, use `.\rum.ps1 user`. A normal `.\rum.ps1 build` or `make`
also builds the user assets. The existing `i686-elf` toolchain supplies GCC,
assembler, linker, `objcopy`, `nm` and `readelf`; `make doctor` checks them.

| Path | Contents |
| --- | --- |
| `include/rum/abi/` | Public headers shared by kernel and user code |
| `user/include/rum/user.h` | C declarations for the user runtime |
| `user/lib/` | Startup and syscall wrappers |
| `user/programs/hello.c` | Console greeting, including partial-write handling |
| `user/linker.ld` | Static ELF32 program layout |
| `build/user/include/` | Generated public include tree; excludes private kernel headers |
| `build/user/debug/` | Executables with symbols, DWARF and linker maps |
| `build/user/ramfs/` | Stripped runtime ELF assets |
| `build/user/embedded-files.c` | Validation of combined source/generated boot assets |

The user link uses `-nostdlib`, its own startup, and target `libgcc` for compiler
helpers. It does not link a host C library, host startup or kernel objects.
The integer-only CPU policy also applies to user sources: no x87, MMX or SSE
state may be used until the kernel can preserve it.

## Syscalls

`rum/abi/syscall.h` defines ABI version 1. Invoke `int 0x80` with the number
in EAX and up to three 32-bit arguments in EBX, ECX and EDX. EAX carries the
result; the kernel return contract preserves the other general registers.
Flags are unspecified. The C primitive preserves EBX for its own caller and
follows the usual i386 callee-saved-register convention.

| Number | Name | Arguments | Result |
| --- | --- | --- | --- |
| 0 | `exit` | Signed status | Does not return |
| 1 | `read` | Handle, writable user address, capacity | Bytes read |
| 2 | `write` | Handle, readable user address, count | Bytes written |
| 3 | `getpid` | None | Positive process ID |

Handles 0, 1 and 2 identify standard input, output and error. A nonnegative
result means success; a negative value is a rum error from `rum/abi/error.h`.
These error numbers are independent of host `errno`. There is no global errno
variable. Addresses, sizes, handles and process IDs are unsigned 32-bit values;
results and exit statuses are signed 32-bit values.
Process IDs exposed through syscalls must not exceed `0x7fffffff`, so they
remain distinguishable from negative errors.

Read/write may complete partially. The caller advances its buffer and retries
as appropriate. A zero-capacity request should return zero without dereferencing
the buffer; a zero-byte read with a nonzero capacity means end of input.
The future kernel dispatcher must validate handles, numbers, ranges and page
permissions, and return `-RUM_ENOSYS` for unsupported calls. Defining this ABI
does not install a syscall gate in the normal kernel.

## Arguments and first entry

The process argument limit is 32, including `argv[0]`, with at most 4096 bytes
of strings including their terminating NULs. The public `struct rum_arguments`
is a fixed 4232-byte launch packet: argc, exact string byte count, 32 offsets
and 4096 inline bytes. Offsets describe tightly packed strings in order, starting
at zero. Validate the copied packet before constructing a stack; these offsets
are not pointers. No launch syscall is assigned yet.

Each process reserves a 64 KiB user stack ending at `0xc0000000`, with the
preceding page left unmapped. At first entry, ESP is 16-byte aligned and points
to this sequence of 32-bit words:

```text
argc
argv[0], ..., argv[argc - 1]  (user addresses of NUL-terminated strings)
0                           (argv terminator)
0                           (empty envp)
alignment padding, then argument strings higher in the stack
```

There is no return address or auxiliary vector at ELF entry. Use fresh user
registers/selectors and EFLAGS `0x202`, as described in [CPU policy](exceptions.md).
`_start` clears DF and EBP, extracts argc/argv, aligns the call stack and calls
`int main(int argc, char **argv)`. C entry has ESP modulo 16 equal to 12 because
CALL pushes a return address. Returning from main passes its full signed result
to `rum_exit`; an unexpectedly returning exit faults with `ud2`.

## ELF subset and asset limits

The initial format is System V little-endian ELF32 `ET_EXEC`, machine i386,
with at most 16 program headers. Programs use fixed addresses beginning at
`0x80000000`, below `0xbfc00000`. No interpreter, dynamic linking, TLS or
constructor/destructor runtime is supported. The linker separates RX text,
read-only constants and RW data/BSS onto distinct pages and supplies a
nonexecutable GNU-stack descriptor.

The host checker uses program headers, not section names, to validate loading.
Nonempty LOAD segments must fit the user program range, be sorted, have no
shared pages, and have congruent file/virtual offsets modulo page size and
their power-of-two alignment. File bytes cannot exceed memory bytes; the entry
must be inside file-backed executable bytes. BSS and unused page bytes must
be zeroed by the future loader. Reject writable executable segments.

The mapped-page budget is 16 MiB including the user stack. Existing i386 paging
can enforce writable/supervisor permissions but has no NX bit; rejecting WX
ELFs does not make writable data physically nonexecutable on this target.

Each runtime asset must fit the RAM filesystem's 64 KiB file limit. Stripping
keeps the debug executable separately, and the checker compares load addresses,
sizes, permissions, alignment and bytes between both copies. `objcopy` may
discard empty LOAD headers without changing the load image.

The asset generator checks the combined `assets/ramfs/` and `build/user/ramfs/`
set for filename collisions, the 64-file limit, names and individual sizes.
This combined C file is a build check; it is not linked into the normal kernel
until process launching is available. Debug ELFs and maps stay out of the
runtime asset directory and release ISO.

## Verification

Host checks cover fixed-width layouts, kernel/public constants, maximum stack
arguments, isolated include paths, symbols and 30 malformed ELF/stripping
cases. Asset tests cover combined limits and duplicate names.

Three isolated QEMU kernels execute the actual separate user ELFs through
production startup, syscall wrappers, GDT/TSS and interrupt entry/return.
Fixture-owned page tables and a mock dispatcher supply services solely for
these tests. They are not the production loader or syscall implementation.

The probe verifies normal and maximum arguments, empty envp, initialized data,
zeroed BSS, C stack alignment, DF/IF, callee-saved EBX, error returns, buffer
arguments, full signed exit status and repeated real PIT returns. A third case
executes `hello.elf` with partial write results. QMP independently audits the
user gate, supervisor kernel mapping, user segment permissions, stack guard
and unchanged executable/constant bytes. Artifacts go to `build/test-artifacts/`.

## Reference

- [ELF program headers and loading](https://gabi.xinuos.com/elf/07-pheader.html)
