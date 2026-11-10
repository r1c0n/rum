#ifndef RUM_C_STDIO_H
#define RUM_C_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define FOPEN_MAX 32
typedef struct rum_c_file FILE;
extern FILE *const stdin;
extern FILE *const stdout;
extern FILE *const stderr;
FILE *fopen(const char *restrict, const char *restrict);
int fclose(FILE *);
int fflush(FILE *);
size_t fread(void *restrict, size_t, size_t, FILE *restrict);
size_t fwrite(const void *restrict, size_t, size_t, FILE *restrict);
int fseek(FILE *, long, int);
long ftell(FILE *);
void rewind(FILE *);
int feof(FILE *);
int ferror(FILE *);
void clearerr(FILE *);
int fgetc(FILE *);
int getchar(void);
int fputc(int, FILE *);
int putchar(int);
char *fgets(char *restrict, int, FILE *restrict);
int fputs(const char *restrict, FILE *restrict);
int puts(const char *);
int remove(const char *);
int rename(const char *, const char *);
int vsnprintf(char *restrict, size_t, const char *restrict, va_list);
int snprintf(char *restrict, size_t, const char *restrict, ...);
int vfprintf(FILE *restrict, const char *restrict, va_list);
int fprintf(FILE *restrict, const char *restrict, ...);
int printf(const char *restrict, ...);
#endif
