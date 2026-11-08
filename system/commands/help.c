#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    tool_print("help              Show commands\n"
        "about             About rum\nclear             Clear the console\n"
        "echo <text>       Print text\nls [path]         List a directory\n"
        "cat <path>        Show a file; omit path to read stdin\n"
        "pwd               Show the working directory\ncd [path]         Change directory\n"
        "write <path> [text] Create or replace a file\n"
        "mkdir <path>      Create a disk directory\nrm <path>         Remove a file or empty directory\n"
        "touch <path>      Create an empty file; preserve an existing file\n"
        "cp [-f] <src> <dst> Copy a regular file; -f allows replacement\n"
        "mv [-f] [--copy] <src> <dst> Move a regular file\n"
        "stat <path>       Show file type, size and volume mode\n"
        "setup             Create /HOME and /APPS if missing\n"
        "sync              Flush the mounted volume\n"
        "run <program> [arguments] Run a program; Ctrl+C cancels\n"
        "start <program> [arguments] Start without waiting\n"
        "ps                List processes\nkill <pid>        Request cancellation\n"
        "jobs              List session jobs\nfg <job>          Bring a job to the foreground\n"
        "wait [job]        Wait and collect one or all jobs\n"
        "sleep <milliseconds> Wait for the specified time\n"
        "snake             Play Snake (WASD, P, R, Q)\n"
        "exit              Restart the shell\nrecovery          Open the recovery shell\n"
        "Pipes: cat IN.BIN | cat > OUT.BIN\n"
        "Redirect with <, > (replace), or >> (append).\n"
        "End a command or pipeline with & to run it as a background job.\n"
        "Quote arguments with '...' or \"...\"; backslash escapes the next character.\n"
        "Edit with arrows, Home/End and Delete; Up/Down recall history, Tab completes.\n"
        "Page Up/Page Down browse output; typing returns to the prompt.\n"
        "Ctrl+L clears the screen; Ctrl+U clears the current command.\n"
        "Programs are available in /rum.\n");
    return 0;
}
