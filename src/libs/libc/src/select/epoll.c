#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>

#include "internal/syscall.h"

int epoll_create(int size) {
  // Linux ignores the size beyond requiring it to be positive.
  if (size <= 0) {
    *__errno_location() = EINVAL;
    return -1;
  }
  return (int)__cavos_ret(
      (unsigned long)__syscall1(SYS_epoll_create, size));
}

int epoll_create1(int flags) {
  return (int)__cavos_ret(
      (unsigned long)__syscall1(SYS_epoll_create1, flags));
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
  return (int)__cavos_ret((unsigned long)__syscall4(SYS_epoll_ctl, epfd, op,
                                                   fd, (long)event));
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents,
               int timeout) {
  return (int)__cavos_ret((unsigned long)__syscall4(
      SYS_epoll_wait, epfd, (long)events, maxevents, timeout));
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
                int timeout, const sigset_t *mask) {
  // Like ppoll, the mask plus its size go straight to the kernel.
  return (int)__cavos_ret((unsigned long)__syscall6(
      SYS_epoll_pwait, epfd, (long)events, maxevents, timeout, (long)mask,
      _NSIG / 8));
}
