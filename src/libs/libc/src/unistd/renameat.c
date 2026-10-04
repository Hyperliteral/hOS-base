#include <stdio.h>

#include "internal/syscall.h"

int renameat(int olddirfd, const char *oldpath, int newdirfd,
             const char *newpath) {
  return (int)__cavos_ret((unsigned long)__syscall4(SYS_renameat, olddirfd,
                                                   (long)oldpath, newdirfd,
                                                   (long)newpath));
}
