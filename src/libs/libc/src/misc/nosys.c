#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <signal.h>
#include <stddef.h>
#include <sys/sendfile.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

// Catch-all for declared-but-kernel-less operations. Each stub exists only
// so musl-ABI binaries link; every one fails loudly at runtime. Missing
// kernel ops: SYS_sendfile, SYS_splice, SYS_copy_file_range, SYS_signalfd,
// SYS_timerfd_create, and any ifaddrs/index ioctls.

// No SYS_sendfile in the kernel table path we serve.
ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
  (void)out_fd;
  (void)in_fd;
  (void)offset;
  (void)count;
  *__errno_location() = ENOSYS;
  return -1;
}

// No SYS_splice in the kernel.
ssize_t splice(int fd_in, off_t *off_in, int fd_out, off_t *off_out,
               size_t len, unsigned flags) {
  (void)fd_in;
  (void)off_in;
  (void)fd_out;
  (void)off_out;
  (void)len;
  (void)flags;
  *__errno_location() = ENOSYS;
  return -1;
}

// No SYS_copy_file_range in the kernel.
ssize_t copy_file_range(int fd_in, off_t *off_in, int fd_out, off_t *off_out,
                        size_t len, unsigned flags) {
  (void)fd_in;
  (void)off_in;
  (void)fd_out;
  (void)off_out;
  (void)len;
  (void)flags;
  *__errno_location() = ENOSYS;
  return -1;
}

// No SYS_signalfd in the kernel.
int signalfd(int fd, const sigset_t *mask, int flags) {
  (void)fd;
  (void)mask;
  (void)flags;
  *__errno_location() = ENOSYS;
  return -1;
}

// No SYS_timerfd_create in the kernel.
int timerfd_create(int clockid, int flags) {
  (void)clockid;
  (void)flags;
  *__errno_location() = ENOSYS;
  return -1;
}

// No interface-address enumeration in the kernel.
int getifaddrs(struct ifaddrs **ifap) {
  (void)ifap;
  *__errno_location() = ENOSYS;
  return -1;
}

void freeifaddrs(struct ifaddrs *ifa) {
  (void)ifa; // nothing was ever allocated: getifaddrs always fails
}

unsigned int if_nametoindex(const char *name) {
  (void)name;
  *__errno_location() = ENOSYS;
  return 0;
}

char *if_indextoname(unsigned int index, char *name) {
  (void)index;
  (void)name;
  *__errno_location() = ENOSYS;
  return NULL;
}
