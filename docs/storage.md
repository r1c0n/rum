# Memory allocation and storage

The kernel heap maps physical pages into a bounded virtual window. `kmalloc`,
`kcalloc`, `krealloc` and `kfree` provide aligned allocations, overflow checks
and block reuse. Freed pages remain mapped for reuse; mapped pages and live
allocations measure different things. `mem` reports heap use and `diag` adds
process and filesystem ownership. See [memory](memory.md).

Files live on FAT16 at `/`; `/RUM` contains compiled programs. There is no RAM
filesystem or embedded-file installer. Executables are read through ordinary
handles into a temporary kernel buffer, validated and copied into a private
process address space. The buffer and file handle are then released.
`RUM_ELF_IMAGE_LIMIT` in the public ELF header bounds executable size separately
from filesystem file sizes.

See [system disk](system-volume.md) for image preparation,
[files and paths](filesystems.md) for the namespace and [FAT16](fat16.md) for
allocation, flush ordering and recovery guarantees.
