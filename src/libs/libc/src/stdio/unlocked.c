// Unlocked stdio family: this libc's FILE has no locks (see
// src/internal/stdio_impl.h), so every unlocked entry is a direct alias for
// its locked twin. One file holds the whole family; flockfile/funlockfile
// are no-ops, ftrylockfile always succeeds, and __fsetlocking keeps a plain
// global mode (no threads) with BYCALLER semantics.
#include <stdio.h>
#include <stdio_ext.h>

int getc_unlocked(FILE *file) { return getc(file); }
int getchar_unlocked(void) { return getchar(); }
int putc_unlocked(int c, FILE *file) { return putc(c, file); }
int putchar_unlocked(int c) { return putchar(c); }

int fgetc_unlocked(FILE *file) { return fgetc(file); }
int fputc_unlocked(int c, FILE *file) { return fputc(c, file); }
char *fgets_unlocked(char *s, int n, FILE *file) {
  return fgets(s, n, file);
}
int fputs_unlocked(const char *s, FILE *file) { return fputs(s, file); }
size_t fread_unlocked(void *ptr, size_t size, size_t nmemb, FILE *file) {
  return fread(ptr, size, nmemb, file);
}
size_t fwrite_unlocked(const void *ptr, size_t size, size_t nmemb,
                       FILE *file) {
  return fwrite(ptr, size, nmemb, file);
}
int fflush_unlocked(FILE *file) { return fflush(file); }
void clearerr_unlocked(FILE *file) { clearerr(file); }
int feof_unlocked(FILE *file) { return feof(file); }
int ferror_unlocked(FILE *file) { return ferror(file); }
int fileno_unlocked(FILE *file) { return fileno(file); }

void flockfile(FILE *file) { (void)file; }
void funlockfile(FILE *file) { (void)file; }
int ftrylockfile(FILE *file) {
  (void)file;
  return 0;
}

static int lock_mode = FSETLOCKING_BYCALLER;

int __fsetlocking(FILE *file, int type) {
  (void)file;
  int old = lock_mode;
  if (type == FSETLOCKING_INTERNAL || type == FSETLOCKING_BYCALLER)
    lock_mode = type;
  return old;
}
