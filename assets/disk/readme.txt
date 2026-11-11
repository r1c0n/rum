rum OS v0.7.0

help                  Show available commands.
ls [path]             List files and directories.
cat /WELCOME.TXT      Read a file.
write /NOTES.TXT hello Create or replace a text file.
mkdir /DOCS           Create a directory.
pwd, cd [path]        Show or change the working directory.
run hello             Run a program.
ls /RUM               List installed programs.
recovery              Open the recovery shell for diagnostics.
setup                 Create /HOME and /APPS for writable application files.

Files are stored on disk and persist across restarts.
System programs are installed in /RUM.
Applications can read /RUM; system updates use the kernel recovery installer.
Filenames use 8.3 spelling, such as NOTES.TXT; paths are case-insensitive.
Wait for a command to finish before shutting down the virtual machine.
