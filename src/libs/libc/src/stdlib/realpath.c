// realpath: absolute canonical path with lexical . / .. normalization and
// symlink expansion via readlink (at most SYMLOOP_MAX hops, else ELOOP).
// resolved may be NULL (malloc the result) or a caller buffer of PATH_MAX.
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SYMLOOP_MAX 40

static int push_component(char *out, size_t *len, const char *comp,
                          size_t comp_len) {
  if (*len + 1 + comp_len >= PATH_MAX)
    return -1;
  out[*len] = '/';
  memcpy(out + *len + 1, comp, comp_len);
  *len += 1 + comp_len;
  out[*len] = '\0';
  return 0;
}

char *realpath(const char *restrict path, char *restrict resolved) {
  if (!path || !*path) {
    *__errno_location() = ENOENT;
    return 0;
  }
  char *work = malloc(PATH_MAX);
  char *out = malloc(PATH_MAX);
  if (!work || !out) {
    free(work);
    free(out);
    return 0;
  }
  if (path[0] != '/') {
    if (!getcwd(work, PATH_MAX)) {
      free(work);
      free(out);
      return 0;
    }
  } else {
    work[0] = '\0';
  }
  // Seed the remainder with the input path (relative inputs append to cwd).
  size_t seed = strlen(work);
  if (seed + 1 + strlen(path) >= PATH_MAX) {
    free(work);
    free(out);
    *__errno_location() = ENAMETOOLONG;
    return 0;
  }
  if (path[0] != '/') {
    work[seed] = '/';
    strcpy(work + seed + 1, path);
  } else {
    strcpy(work, path);
  }

  int links = 0;
  const char *rest = work;
  size_t out_len = 0;
  out[0] = '\0';

  char link[PATH_MAX];
  for (;;) {
    while (*rest == '/')
      rest++;
    if (!*rest)
      break;
    const char *end = rest;
    while (*end && *end != '/')
      end++;
    size_t comp_len = (size_t)(end - rest);
    if ((comp_len == 1 && rest[0] == '.')) {
      // Skip.
    } else if (comp_len == 2 && rest[0] == '.' && rest[1] == '.') {
      char *slash = strrchr(out, '/');
      if (slash) {
        *slash = '\0';
        out_len = (size_t)(slash - out);
      }
    } else {
      // Stat the candidate to spot symlinks before committing.
      char cand[PATH_MAX];
      if (snprintf(cand, sizeof(cand), "%s/%.*s", out[0] ? out : "",
                   (int)comp_len, rest) >= (int)sizeof(cand)) {
        free(work);
        free(out);
        *__errno_location() = ENAMETOOLONG;
        return 0;
      }
      struct stat st;
      if (lstat(cand, &st) == 0 &&
          (st.st_mode & S_IFMT) == S_IFLNK) {
        if (++links > SYMLOOP_MAX) {
          free(work);
          free(out);
          *__errno_location() = ELOOP;
          return 0;
        }
        ssize_t n = readlink(cand, link, sizeof(link) - 1);
        if (n < 0) {
          free(work);
          free(out);
          return 0;
        }
        link[n] = '\0';
        // Splice the target back: absolute targets restart from root,
        // relative ones splice at the current position.
        char *tail = malloc(PATH_MAX);
        if (!tail) {
          free(work);
          free(out);
          return 0;
        }
        strcpy(tail, end);
        if (link[0] == '/') {
          if (strlen(link) + strlen(tail) >= PATH_MAX) {
            free(tail);
            free(work);
            free(out);
            *__errno_location() = ENAMETOOLONG;
            return 0;
          }
          strcpy(work, link);
          strcat(work, tail);
          rest = work;
        } else {
          size_t need = out_len + 1 + strlen(link) + strlen(tail) + 1;
          if (need > PATH_MAX) {
            free(tail);
            free(work);
            free(out);
            *__errno_location() = ENAMETOOLONG;
            return 0;
          }
          memmove(work + out_len + 1 + strlen(link), tail,
                  strlen(tail) + 1);
          memcpy(work, out, out_len);
          work[out_len] = '/';
          memcpy(work + out_len + 1, link, strlen(link));
          rest = work;
        }
        free(tail);
        out[0] = '\0';
        out_len = 0;
        continue;
      }
      if (push_component(out, &out_len, rest, comp_len) < 0) {
        free(work);
        free(out);
        *__errno_location() = ENAMETOOLONG;
        return 0;
      }
    }
    rest = end;
  }
  if (!out[0]) {
    out[0] = '/';
    out[1] = '\0';
  }
  char *result;
  if (resolved) {
    if (strlen(out) >= PATH_MAX) {
      free(work);
      free(out);
      *__errno_location() = ENAMETOOLONG;
      return 0;
    }
    strcpy(resolved, out);
    result = resolved;
  } else {
    result = strdup(out);
    if (!result) {
      free(work);
      free(out);
      return 0;
    }
  }
  free(work);
  free(out);
  return result;
}
