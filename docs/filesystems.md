# Files and paths

FAT16 is mounted at `/`. `/RUM` is an ordinary directory containing installed
programs. rum has no RAM filesystem, `/disk` mount or compatibility alias.

```text
mkdir /DOCS
cd /DOCS
write NOTES.TXT hello from rum
cat notes.txt
ls .
pwd
cd ..
```

The prompt shows the working directory. Absolute and relative paths, repeated
separators, `.` and `..` are supported. `..` stops at `/`. The resolver checks
original components: `/MISSING/../README.TXT` fails instead of skipping the
missing directory, and a file cannot be traversed as a directory.

Names use case-insensitive 8.3 spelling: a base of 1 through 8 characters and
an optional extension of 1 through 3 characters. Letters, digits, underscores
and hyphens are supported; spaces, multiple dots and long names are rejected.
The parser validates all bytes, component sizes, path length and depth before
backend I/O. Public capacities are in `sdk/include/rum/abi/filesystem.h`;
private limits are in `kernel/include/rum/fs_limits.h`. A larger public buffer
does not enable long FAT filenames.

Each process owns a working-directory reference and a bounded handle table.
Objects are tracked by mount generation and backend identity, so aliases pin
the same object. Removal and replacement report busy while an open handle or
working directory retains the target. Process teardown closes all references.
The root mount cannot be detached while referenced.

Reads at EOF return zero bytes. Partial I/O returns transferred bytes; errors
without progress return the translated syscall error. Seek checks arithmetic
overflow. See [filesystem syscalls](filesystem-syscalls.md).

Filesystem work is bounded, synchronous and restricted to task or boot context.
IRQs only record events. A rejected disk leaves recovery available without a
substitute filesystem. Use `make test-fs`, `make test-fat16`,
`make test-fat16-write` and `make test-userspace-shell` for focused checks.
