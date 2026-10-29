# System programs

`shell/main.c` implements the userspace command loop, input editing, executable
lookup, and child launch. Individual commands live in `commands/` and use only
the public SDK. The working-directory prompt and command behavior are described
in [Shell](../docs/shell.md).

To add a command:

1. Create `system/commands/name.c` with a normal C `main` entry.
2. Include `<rum/user.h>` or `<rum/tools.h>` for shared command helpers.
3. Add `name` to `SYSTEM_COMMANDS` in the root Makefile.
4. Run `make user` to update the standalone system image, or `make` to rebuild
   the bootable ISO as well.
5. Run `make test-user` and the relevant shell tests.

The command builds as a separate ELF and is installed in the read-only `/rum`
boot volume. Adding a command does not require adding parser cases to the shell.
Applications belong under `apps/`, and small SDK demonstrations belong under
`sdk/examples/`.

The kernel recovery command loop lives separately in `kernel/recovery/shell.c`.
The `recovery` command requests that fallback through the existing restricted
session interface.
