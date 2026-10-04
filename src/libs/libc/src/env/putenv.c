#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal/impl.h"

// putenv takes ownership of `string` ("NAME=value") on success, matching
// POSIX: the pointer itself becomes part of the environment.
int putenv(char *string) {
  char *eq;
  size_t l;
  char **env;
  if (!string || !(eq = strchr(string, '=')) || eq == string) {
    return -1;
  }
  l = (size_t)(eq - string);
  env = __environ;
  if (env) {
    for (size_t i = 0; env[i]; i++) {
      if (strncmp(string, env[i], l) || env[i][l] != '=')
        continue;
      env[i] = string;
      return 0;
    }
  }
  size_t n = 0;
  if (env)
    while (env[n])
      n++;
  char **nenv = malloc((n + 2) * sizeof(*nenv));
  if (!nenv)
    return -1;
  for (size_t i = 0; i < n; i++)
    nenv[i] = env[i];
  nenv[n] = string;
  nenv[n + 1] = 0;
  environ = nenv;
  __environ = nenv;
  return 0;
}
