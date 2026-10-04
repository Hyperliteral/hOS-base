#include <signal.h>

int sigemptyset(sigset_t *set) {
  if (!set)
    return -1;
  for (size_t i = 0; i < sizeof(set->__bits) / sizeof(set->__bits[0]); i++)
    set->__bits[i] = 0;
  return 0;
}
