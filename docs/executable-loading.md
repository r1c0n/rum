# Executable loading

rum loads static, little-endian System V ELF32 executables for i386 from the
root filesystem. The normal shell resolves programs in `/RUM` or by path;
applications use the public launch calls. Dynamic linking, interpreters,
relocations and thread-local storage are not supported.

## Limits

| Resource | Limit |
| --- | --- |
| Complete executable file | 16 MiB, including ELF tables and unused trailing bytes |
| Program headers | 16 total, including the required stack descriptor |
| One LOAD segment's memory size | 16 MiB minus the 64 KiB user stack |
| Committed memory | 16 MiB per process, including all rounded LOAD pages, the stack and subsequent heap/region allocations |
| Kernel read request | At most 512 bytes |
| User stack | 64 KiB with an unmapped 4 KiB guard |

These are ceilings, not RAM reservations. A large image may leave little room
for heap growth, and physical memory can run out before the process quota does.
Allocation failure is recoverable; it does not change the parent's mappings
or publish an incomplete child. The supported 16 MiB boot configuration remains
usable, but cannot hold a process using the entire 16 MiB quota plus the kernel.

The public definitions live in `sdk/include/rum/abi/elf.h` and
`sdk/include/rum/abi/layout.h`. File size and committed memory are separate:
BSS consumes pages without file data, and unused file tails consume no user
pages. Gaps between segments remain unmapped.

## Validation

The kernel and SDK validator share the policy in `sdk/include/rum/elf-image.h`.
Before constructing an address space, the loader copies the ELF header and
program headers into bounded kernel storage and checks:

- The ELF identification, target, version, header sizes and program count.
- Program-table multiplication, offsets and complete file spans.
- Supported segment types: LOAD, NULL and one empty read/write GNU-stack
  descriptor. Interpreter, dynamic, TLS and unknown types are rejected.
- `p_filesz <= p_memsz`, segment size limits and the aggregate page budget.
- Power-of-two alignment, including the ELF rule that file and virtual
  offsets are congruent modulo page size. A LOAD may start inside a page.
- Ascending, nonoverlapping virtual page ranges inside the program window.
  Kernel, reserved stack-window and guard ranges cannot be mapped by a LOAD.
- A readable permission set with no unknown bits or writable/executable
  combination, and an entry point inside file-backed executable bytes.

LOAD entries may reference overlapping file bytes, but their virtual pages
must remain distinct. Each mapping gets private physical frames. Physical
addresses from program headers are ignored, and section headers do not govern
loading.

The non-PAE i386 paging mode enforces user/supervisor and write permissions,
not execute-disable permissions. Read-only LOADs become read-only after copying.
The GNU-stack and writable/executable restrictions describe accepted ELF input;
they do not promise hardware NX or W^X enforcement. See
[virtual-memory protections](virtual-memory.md).

The ELF segment rules follow the
[System V program-loading specification](https://gabi.xinuos.com/elf/07-pheader.html),
with the narrower static-executable subset and limits described here.

## Reading and construction

The launcher opens its own kernel filesystem reference and retains that
identity until loading finishes. It reads the header and program table first,
then reads only validated file-backed LOAD spans. Requests are capped at
512 bytes, successful short reads are retried, and unexpected EOF is an I/O
failure. File gaps, BSS and unused tails are never read.

There is no allocation sized to the complete file and no complete-image kernel
stack buffer. The loader keeps a fixed-size plan and scratch buffer. It creates
an inactive address space, allocates zeroed private pages, copies file data in
chunks, applies final write permissions, and constructs the argument stack.
BSS, first-page padding and unused final-page bytes remain zero.

The retained file identity prevents replacement or removal during loading under
the filesystem's open-object rules. It is not a content snapshot: applications
should finish writes before launching a file. Geometry and permissions always
come from the saved, validated headers; later payload changes cannot redirect
the loader's destination mappings.

Read errors, malformed input, memory exhaustion, cancellation checks and failed
publication discard the unpublished address space and release temporary
references. Inherited descriptors prepared for a failed launch are released;
the parent's descriptors, shared offsets and private data remain unchanged.
The executable reference closes before successful task publication.

The kernel source interface `elf_load_process_from_source` returns zero or a
negative public error. Memory-backed test fixtures use `elf_load_process`, a
Boolean wrapper over that same implementation. Neither publishes a task.

| Failure | Result |
| --- | --- |
| Invalid ELF, unsupported layout or size/budget violation | `ENOEXEC` |
| Physical memory, page tables, metadata or task construction exhausted | `ENOMEM` |
| Unexpected EOF or invalid backend transfer count | `EIO` |
| Backend I/O error or timeout | Translated filesystem error |
| Caller cancellation observed during loading | `EINTR` |

Kernel entry stacks stay guarded and CPU policy remains integer-only. File
reads and construction execute in task context. Bounded synchronous disk I/O
and mapping work can still delay scheduling; this does not add arbitrary
kernel preemption.

## Building and checking

`make user` builds the runtime and application assets. The SDK linker separates
text, constants and data/BSS, emits the stack descriptor and asserts segment
and memory limits. The host validator checks the actual file size and compares
file-backed LOAD bytes between debug and stripped ELFs in bounded chunks.
It uses public SDK headers only and allows debug files up to 64 MiB; installed
assets retain the 16 MiB file limit.

```sh
build/tools/check-user-elf build/user/system/heapdemo.elf build/user/debug/heapdemo.elf
./rum test --suite executables -j4
```

On Windows:

```powershell
.\rum.ps1 test -Suite executables -Jobs 4
```

The focused suite checks streamed reads and rollback on the host, then validates
ELF construction and a program larger than 64 KiB through ELF boot at 16 MiB and
ISO boot at 64 MiB. The shell suite also launches the large fixture from a
disposable FAT16 disk. Fixtures exercise initialized data, large BSS, heap
growth, permissions, stack guards, concurrent address spaces, failed nested
launches, unchanged parent descriptors, faults and cancellation.
