#ifndef CAVOS_LIBC_H
#define CAVOS_LIBC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAVOS_PRINTF_FORMAT(a, b) __attribute__((format(printf, a, b)))

extern char **environ;

int printf(const char *, ...) CAVOS_PRINTF_FORMAT(1, 2);
int puts(const char *);
int putchar(int);

long read(int, void *, size_t);
long write(int, const void *, size_t);
int open(const char *, int, ...);
int close(int);
long lseek(int, long, int);
int unlink(const char *);
int isatty(int);

long syscall_getpid(void);
long syscall_gettid(void);

void *mmap(void *, size_t, int, int, int, long);
int munmap(void *, size_t);
void *brk(void *);
void *sbrk(intptr_t);

void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);

char *getenv(const char *);
int atoi(const char *);

void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);
size_t strlen(const char *);
char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
char *strcat(char *, const char *);
char *strncat(char *, const char *, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strstr(const char *, const char *);

_Noreturn void exit(int);
_Noreturn void _Exit(int);
_Noreturn void abort(void);

#ifdef __cplusplus
}
#endif

#endif
