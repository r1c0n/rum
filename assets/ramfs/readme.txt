rum OS v0.5.0

ls [path]              List RAM or disk files.
cat welcome.txt        Read an embedded file.
write notes.txt hello  Create or replace a text file.
cat notes.txt          Read your file.
rm notes.txt           Remove your file.
pwd, cd [path]         Show or change the working directory.
run hello              Run a program.
ls /rum                List installed programs.
recovery               Open the recovery shell for diagnostics.

Names: up to 63 ASCII letters, digits, dots, underscores or hyphens.
RAM root: up to 64 files, each up to 64 KiB.
Embedded files are copied into RAM at boot and can be edited.
Changes are temporary. Restarting rum restores the embedded files.
An attached FAT16 volume at /disk preserves disk files across boots.
System programs are installed in /rum (read-only).
The prompt shows your working directory, for example /disk/DOCS>.
Disk names use uppercase-compatible 8.3 spelling, such as NOTE.TXT.
