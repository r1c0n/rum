#include "shell.h"
static char fold(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }
static unsigned length(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
static int prefix(const char *word, const char *p)
{
    while (*p) if (fold(*word++) != fold(*p++)) return 0;
    return 1;
}
static int same(const char *a, const char *b)
{
    while (*a && fold(*a) == fold(*b)) { ++a; ++b; } return *a == *b;
}
static void add(struct shell_matches *matches, const char *candidate)
{
    for (unsigned i = 0; i < matches->count; ++i) if (same(matches->values[i], candidate)) return;
    if (matches->count == SHELL_COMPLETION_LIMIT) { matches->truncated = 1; return; }
    unsigned n = length(candidate);
    if (!matches->count) matches->common = n;
    else {
        unsigned i = 0;
        while (i < matches->common && candidate[i] && fold(matches->values[0][i]) == fold(candidate[i])) ++i;
        matches->common = i;
    }
    for (unsigned i = 0; i <= n; ++i) matches->values[matches->count][i] = candidate[i];
    ++matches->count;
}
static rum_result_t directory(const char *path, const char *base, const char *stem, int programs, struct shell_matches *matches)
{
    rum_result_t fd = rum_open(path, RUM_OPEN_READ, RUM_OPEN_DIRECTORY);
    if (fd < 0) return fd;
    rum_result_t result = 0;
    while (matches->scanned < SHELL_COMPLETION_SCAN_LIMIT && !matches->truncated) {
        struct rum_directory_entry entry = { .version = RUM_FS_ABI_VERSION, .size = sizeof entry };
        result = rum_readdir((rum_handle_t)fd, &entry);
        if (result != 1) break;
        ++matches->scanned;
        unsigned n = length(entry.name), base_n = length(base);
        if (programs && entry.kind == RUM_ENTRY_FILE) {
            if (n < 5 || !same(entry.name + n - 4, ".ELF")) continue;
            int extension = 0; for (unsigned i = 0; stem[i]; ++i) extension |= stem[i] == '.';
            if (!extension) entry.name[n -= 4] = 0;
        }
        if (!prefix(entry.name, stem)) continue;
        unsigned slash = entry.kind == RUM_ENTRY_DIRECTORY;
        if (base_n + n + slash >= RUM_COMMAND_TEXT_CAPACITY) { matches->truncated = 1; break; }
        char candidate[RUM_COMMAND_TEXT_CAPACITY];
        unsigned i = 0;
        for (; i < base_n; ++i) candidate[i] = base[i];
        for (unsigned j = 0; j < n; ++j) candidate[i++] = entry.name[j];
        if (slash) candidate[i++] = '/';
        candidate[i] = 0;
        add(matches, candidate);
    }
    if (matches->scanned == SHELL_COMPLETION_SCAN_LIMIT && result == 1) matches->truncated = 1;
    rum_result_t closed = rum_close((rum_handle_t)fd);
    if (result < 0 || closed < 0) matches->truncated = 1;
    return result < 0 ? result : closed;
}
rum_result_t shell_complete(const struct shell_completion_context *context, struct shell_matches *matches)
{
    *matches = (struct shell_matches){0};
    char base[RUM_COMMAND_TEXT_CAPACITY], path[RUM_ABI_PATH_CAPACITY];
    unsigned slash = 0, n = length(context->prefix);
    for (unsigned i = 0; i < n; ++i) if (context->prefix[i] == '/') slash = i + 1;
    for (unsigned i = 0; i < slash; ++i) base[i] = path[i] = context->prefix[i];
    base[slash] = path[slash] = 0;
    const char *stem = context->prefix + slash;
    if (slash) return directory(path, base, stem, context->command, matches);
    rum_result_t result = 0;
    if (context->command) {
        const char *builtins[] = {"exit", "recovery", "start"};
        for (unsigned i = 0; i < sizeof builtins / sizeof builtins[0]; ++i) if (prefix(builtins[i], stem)) add(matches, builtins[i]);
        result = directory("/RUM", "", stem, 1, matches);
    }
    if (!matches->truncated) {
        rum_result_t local = directory(".", "", stem, context->command, matches);
        if (!matches->count) result = local;
    }
    return matches->count ? 0 : result;
}
