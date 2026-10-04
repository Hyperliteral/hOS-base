#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal/impl.h"

// Single-threaded environ editing: no locks (no threads in this libc).
// environ and __environ always alias the same vector; every mutation
// publishes through both names (see src/env/environ.c).

static void sync_environ(char **env) {
  environ = env;
  __environ = env;
}

static size_t env_len(char **env) {
  size_t n = 0;
  if (env)
    while (env[n])
      n++;
  return n;
}

int clearenv(void) {
  static char *empty[1] = {0};
  sync_environ(empty);
  return 0;
}

int unsetenv(const char *name) {
  size_t l;
  char **env;
  if (!name || !*name || strchr(name, '=')) {
    errno = EINVAL;
    return -1;
  }
  env = __environ;
  if (!env)
    return 0;
  l = strlen(name);
  for (size_t i = 0; env[i]; i++) {
    if (!strncmp(name, env[i], l) && env[i][l] == '=') {
      for (size_t j = i;; j++) {
        env[j] = env[j + 1];
        if (!env[j])
          break;
      }
      i--;
    }
  }
  return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
  size_t l;
  char **env;
  char *entry;
  if (!name || !*name || strchr(name, '=') || !value) {
    errno = EINVAL;
    return -1;
  }
  env = __environ;
  l = strlen(name);
  if (env) {
    for (size_t i = 0; env[i]; i++) {
      if (strncmp(name, env[i], l) || env[i][l] != '=')
        continue;
      if (!overwrite)
        return 0;
      size_t vlen = strlen(value);
      char *rep = malloc(l + 1 + vlen + 1);
      if (!rep)
        return -1;
      memcpy(rep, name, l);
      rep[l] = '=';
      memcpy(rep + l + 1, value, vlen + 1);
      env[i] = rep;
      return 0;
    }
  }
  size_t n = env_len(env);
  char **nenv = malloc((n + 2) * sizeof(*nenv));
  if (!nenv)
    return -1;
  for (size_t i = 0; i < n; i++)
    nenv[i] = env[i];
  size_t vlen = strlen(value);
  entry = malloc(l + 1 + vlen + 1);
  if (!entry) {
    free(nenv);
    return -1;
  }
  memcpy(entry, name, l);
  entry[l] = '=';
  memcpy(entry + l + 1, value, vlen + 1);
  nenv[n] = entry;
  nenv[n + 1] = 0;
  sync_environ(nenv);
  return 0;
}
