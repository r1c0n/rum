rum OS v0.2.0

ls                     List RAM files and sizes.
cat welcome.txt        Read an embedded file.
write notes.txt hello  Create or replace a text file.
cat notes.txt          Read your file.
rm notes.txt           Remove your file.
mem                    Show heap and filesystem usage.

Names: up to 63 ASCII letters, digits, dots, underscores or hyphens.
One root directory; up to 64 files, each up to 64 KiB.
Embedded files are copied into RAM at boot and can be edited.
Changes are temporary. Restarting rum restores the embedded files.
