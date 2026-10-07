rum OS v0.4.0

ls [path]              List RAM or disk files.
cat welcome.txt        Read an embedded file.
write notes.txt hello  Create or replace a text file.
cat notes.txt          Read your file.
rm notes.txt           Remove your file.
pwd, cd [path]         Show or change the working directory.
run hello              Launch a userspace program.
ls /rum                List standalone command executables.
recovery               Enter the kernel shell for mem and diag.

Names: up to 63 ASCII letters, digits, dots, underscores or hyphens.
RAM root: up to 64 files, each up to 64 KiB.
Embedded files are copied into RAM at boot and can be edited.
Changes are temporary. Restarting rum restores the embedded files.
An attached FAT16 volume at /disk preserves disk files across boots.
The read-only /rum system folder is loaded separately from the kernel.
The prompt shows your working directory, for example /disk/DOCS>.
Disk names use uppercase-compatible 8.3 spelling, such as NOTE.TXT.
