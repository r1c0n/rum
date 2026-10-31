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
        "run <program> [arguments] Run a program; Ctrl+C cancels\n"
        "start <program> [arguments] Start without waiting\n"
        "ps                List processes\nkill <pid>        Request cancellation\n"
        "sleep <milliseconds> Wait for the specified time\n"
        "snake             Play Snake (WASD, P, R, Q)\n"
        "exit              Restart the shell\nrecovery          Open the recovery shell\n"
        "Programs are available in /rum.\n");
    return 0;
}
