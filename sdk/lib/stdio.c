#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <rum/user.h>

struct rum_c_file { rum_handle_t handle; unsigned access, used, eof, error, disk; };
static FILE files[FOPEN_MAX] = {
    { .handle = RUM_STDIN, .access = RUM_OPEN_READ, .used = 1 },
    { .handle = RUM_STDOUT, .access = RUM_OPEN_WRITE, .used = 1 },
    { .handle = RUM_STDERR, .access = RUM_OPEN_WRITE, .used = 1 }
};
FILE *const stdin = &files[0];
FILE *const stdout = &files[1];
FILE *const stderr = &files[2];

static int valid(FILE *file)
{
    uintptr_t address = (uintptr_t)file, start = (uintptr_t)files;
    if (address < start || address - start >= sizeof files ||
        (address - start) % sizeof *file || !file->used) { errno = EBADF; return 0; }
    return 1;
}
static int failure(FILE *file, rum_result_t error)
{
    file->error = 1; errno = (int)-error; return EOF;
}
FILE *fopen(const char *restrict path, const char *restrict mode)
{
    if (!path || !mode || (*mode != 'r' && *mode != 'w' && *mode != 'a')) { errno = EINVAL; return NULL; }
    unsigned access = *mode == 'r' ? RUM_OPEN_READ : RUM_OPEN_WRITE;
    uint32_t flags = *mode == 'r' ? 0 : RUM_OPEN_CREATE |
                     (*mode == 'w' ? RUM_OPEN_TRUNCATE : RUM_OPEN_APPEND);
    unsigned binary = 0, update = 0;
    for (const char *suffix = mode + 1; *suffix; ++suffix) {
        if (*suffix == 'b' && !binary) binary = 1;
        else if (*suffix == '+' && !update) { update = 1; access = RUM_OPEN_READ | RUM_OPEN_WRITE; }
        else { errno = EINVAL; return NULL; }
    }
    unsigned i;
    for (i = 3; i < FOPEN_MAX; ++i) if (!files[i].used) break;
    if (i == FOPEN_MAX) { errno = EMFILE; return NULL; }
    rum_result_t handle = rum_open(path, access, flags);
    if (handle < 0) { errno = (int)-handle; return NULL; }
    files[i] = (FILE){ .handle = (rum_handle_t)handle, .access = access, .used = 1, .disk = 1 };
    return &files[i];
}
int fflush(FILE *file)
{
    if (!file) {
        int result = 0;
        for (unsigned i = 0; i < FOPEN_MAX; ++i)
            if (files[i].used && (files[i].access & RUM_OPEN_WRITE) && fflush(&files[i]) == EOF) result = EOF;
        return result;
    }
    if (!valid(file)) return EOF;
    if (!(file->access & RUM_OPEN_WRITE)) return 0;
    /* There is no userspace output buffer. Flush regular-file backends;
       consoles and pipes have already received all successful writes. */
    struct rum_file_information info;
    if (!file->disk && rum_fstat(file->handle, &info) < 0) return 0;
    rum_result_t result = rum_flush(file->handle);
    return result < 0 ? failure(file, result) : 0;
}
int fclose(FILE *file)
{
    if (!valid(file)) return EOF;
    int result = fflush(file);
    rum_result_t closed = rum_close(file->handle);
    if (closed < 0) { errno = (int)-closed; result = EOF; }
    *file = (FILE){0}; return result;
}
static size_t transfer(FILE *file, void *buffer, size_t size, size_t count, int writing)
{
    if (!size || !count) return 0;
    if (!valid(file)) return 0;
    if (!(file->access & (writing ? RUM_OPEN_WRITE : RUM_OPEN_READ))) { failure(file, -RUM_EBADF); return 0; }
    if (count > SIZE_MAX / size) { failure(file, -RUM_EOVERFLOW); return 0; }
    if (!writing && file->eof) return 0;
    size_t total = size * count, done = 0;
    while (done < total) {
        uint32_t chunk = total - done > 65536u ? 65536u : (uint32_t)(total - done);
        unsigned char *cursor = (unsigned char *)buffer + done;
        rum_result_t result = writing ? rum_write(file->handle, cursor, chunk) : rum_read(file->handle, cursor, chunk);
        if (result < 0) { failure(file, result); break; }
        if (!result) { if (writing) failure(file, -RUM_EIO); else file->eof = 1; break; }
        done += (size_t)result;
    }
    return done / size;
}
size_t fread(void *restrict out, size_t size, size_t count, FILE *restrict file) { return transfer(file, out, size, count, 0); }
size_t fwrite(const void *restrict in, size_t size, size_t count, FILE *restrict file) { return transfer(file, (void *)in, size, count, 1); }
int fseek(FILE *file, long offset, int whence)
{
    if (!valid(file)) return -1;
    if (whence < SEEK_SET || whence > SEEK_END) return failure(file, -RUM_EINVAL);
    uint64_t position;
    rum_result_t result = rum_seek(file->handle, offset, (uint32_t)whence, &position);
    if (result < 0) return failure(file, result);
    file->eof = 0; return 0;
}
long ftell(FILE *file)
{
    if (!valid(file)) return -1;
    uint64_t position;
    rum_result_t result = rum_seek(file->handle, 0, RUM_SEEK_CUR, &position);
    if (result < 0) return failure(file, result);
    if (position > LONG_MAX) { errno = EOVERFLOW; return -1; }
    return (long)position;
}
void rewind(FILE *file) { (void)fseek(file, 0, SEEK_SET); clearerr(file); }
int feof(FILE *file) { return valid(file) ? (int)file->eof : 0; }
int ferror(FILE *file) { return valid(file) ? (int)file->error : 1; }
void clearerr(FILE *file) { if (valid(file)) file->eof = file->error = 0; }
int fgetc(FILE *file) { unsigned char byte; return fread(&byte, 1, 1, file) == 1 ? byte : EOF; }
int getchar(void) { return fgetc(stdin); }
int fputc(int value, FILE *file) { unsigned char byte = (unsigned char)value; return fwrite(&byte, 1, 1, file) == 1 ? byte : EOF; }
int putchar(int value) { return fputc(value, stdout); }
char *fgets(char *restrict out, int capacity, FILE *restrict file)
{
    if (capacity <= 0) { errno = EINVAL; return NULL; }
    int used = 0;
    while (used < capacity - 1) {
        int value = fgetc(file);
        if (value == EOF) { if (!used || ferror(file)) return NULL; break; }
        out[used++] = (char)value; if (value == '\n') break;
    }
    out[used] = 0; return out;
}
int fputs(const char *restrict text, FILE *restrict file) { size_t n = strlen(text); return fwrite(text, 1, n, file) == n ? 0 : EOF; }
int puts(const char *text) { return fputs(text, stdout) == EOF ? EOF : fputc('\n', stdout); }
int remove(const char *path) { rum_result_t r = rum_remove(path); if (r < 0) { errno = (int)-r; return -1; } return 0; }
int rename(const char *source, const char *target) { rum_result_t r = rum_rename(source, target, RUM_RENAME_REPLACE); if (r < 0) { errno = (int)-r; return -1; } return 0; }
