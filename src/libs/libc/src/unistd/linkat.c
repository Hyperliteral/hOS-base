#include <unistd.h>

#include "internal/syscall.h"

int linkat(int olddirfd, const char *oldpath, int newdirfd,
           const char *newpath, int flags) {
  return (int)__cavos_ret((unsigned long)__syscall5(SYS_linkat, olddirfd,
                                                   (long)oldpath, newdirfd,
                                                   (long)newpath, flags));
}
