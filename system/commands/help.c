#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    tool_print("help              Show commands\n"
        "about             About rum\nclear             Clear the console\n"
        "echo <text>       Print text\nls [path]         List a directory\n"
        "cat <path>        Show a file (binary bytes escaped)\n"
        "pwd               Show the working directory\ncd [path]         Change directory\n"
        "write <path> [text] Create or replace a file\n"
        "mkdir <path>      Create a disk directory\nrm <path>         Remove a file or empty directory\n"
        "run <program> [arguments] Launch a foreground ELF; Ctrl+C cancels\n"
        "start <program> [arguments] Start without waiting\n"
        "ps                List user processes\nkill <pid>        Request cancellation\n"
        "sleep <milliseconds> Wait for a bounded duration\n"
        "snake             Play Snake (WASD, P, R, Q)\n"
        "exit              Restart the shell\nrecovery          Enter the kernel recovery shell\n"
        "Tools are programs in /rum; start is shell syntax. The prompt shows your directory.\n");
    return 0;
}
