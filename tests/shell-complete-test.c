#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../system/shell/shell.h"

static unsigned opened, closed, position, reads, source, many, fail_at;
static int close_error;
rum_result_t rum_open(const char *path, uint32_t access, uint32_t flags)
{
    assert(access == RUM_OPEN_READ && flags == RUM_OPEN_DIRECTORY);
    if (!strcmp(path, "/MISSING/")) return -RUM_ENOENT;
    assert(!strcmp(path, "/RUM") || !strcmp(path, ".") || !strcmp(path, "/DOCS/") || !strcmp(path, "../DOCS/"));
    source = !strcmp(path, "/RUM"); position = 0; ++opened; return 3;
}
rum_result_t rum_close(rum_handle_t fd) { assert(fd == 3); ++closed; return close_error ? -RUM_EIO : 0; }
rum_result_t rum_readdir(rum_handle_t fd, struct rum_directory_entry *entry)
{
    assert(fd == 3 && entry->version == RUM_FS_ABI_VERSION && entry->size == sizeof *entry);
    ++reads;
    if (fail_at && position == fail_at) return -RUM_EIO;
    if (many) {
        if (position == many) return 0;
        snprintf(entry->name, sizeof entry->name, "F%03u.TXT", position++); entry->kind = RUM_ENTRY_FILE; return 1;
    }
    const char *programs[] = {"HELLO.ELF", "HELP.ELF", "NOTES.TXT"};
    const char *files[] = {"HELLO.ELF", "HELIX.ELF", "NOTES.TXT", "NORTH.TXT", "DOCS"};
    unsigned count = source ? 3 : 5;
    if (position == count) return 0;
    strcpy(entry->name, source ? programs[position] : files[position]);
    entry->kind = !source && position == 4 ? RUM_ENTRY_DIRECTORY : RUM_ENTRY_FILE;
    ++position; return 1;
}
static struct shell_matches complete(const char *line)
{
    struct shell_completion_context c; struct shell_matches m;
    assert(!shell_completion_context(line, strlen(line), &c));
    (void)shell_complete(&c, &m); assert(opened == closed); return m;
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    struct shell_matches m = complete("he");
    assert(m.count == 3 && m.common == 3 && !m.truncated && !strcmp(m.values[0], "HELLO"));
    m = complete("run he"); assert(m.count == 3);
    m = complete("start he"); assert(m.count == 3);
    m = complete("cat | he"); assert(m.count == 3);
    m = complete("hellO.e"); assert(m.count == 1 && !strcmp(m.values[0], "HELLO.ELF"));
    m = complete("cat no"); assert(m.count == 2 && !strcmp(m.values[0], "NOTES.TXT"));
    m = complete("cat \"/DOCS/NO"); assert(m.count == 2 && !strcmp(m.values[0], "/DOCS/NOTES.TXT"));
    m = complete("echo >../DOCS/no"); assert(m.count == 2 && !strcmp(m.values[0], "../DOCS/NOTES.TXT"));
    m = complete("cat DO"); assert(m.count == 1 && !strcmp(m.values[0], "DOCS/"));
    m = complete("cat /MISSING/N"); assert(!m.count);
    many = 200; reads = 0; m = complete("cat Z");
    assert(m.truncated && !m.count && reads == SHELL_COMPLETION_SCAN_LIMIT);
    reads = 0; m = complete("cat F");
    assert(m.truncated && m.count == SHELL_COMPLETION_LIMIT && reads == SHELL_COMPLETION_LIMIT + 1);
    many = 0; fail_at = 1; m = complete("cat H"); assert(m.truncated && m.count == 1);
    fail_at = 0; close_error = 1; m = complete("cat NO"); assert(m.truncated && m.count == 2);
    puts("PASS: contextual program/path completion, case folding, deduplication, scan/match caps and descriptor cleanup on I/O failure");
    return 0;
}
