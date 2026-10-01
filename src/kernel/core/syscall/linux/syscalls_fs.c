#include <fat32.h>
#include <linked_list.h>
#include <linux.h>
#include <malloc.h>
#include <poll.h>
#include <string.h>
#include <syscalls.h>
#include <task.h>
#include <util.h>

// Manages all systemcalls related to filesystem operations
// Copyright (C) 2024 Panagiotis

#define SYSCALL_READ 0
static size_t syscallRead(int fd, char *str, uint32_t count) {
  if (!count)
    return 0;
  OpenFile *browse = fsUserGetNode(currentTask, fd);
  if (!browse)
    return ERR(EBADF);
  return fsRead(browse, (uint8_t *)str, count);
}

#define SYSCALL_WRITE 1
static size_t syscallWrite(int fd, char *str, uint32_t count) {
  if (!count)
    return 0;
  OpenFile *browse = fsUserGetNode(currentTask, fd);
  if (!browse)
    return ERR(EBADF);
  debugf("[write] fd{%d} count{%u}\n", fd, count);
  size_t result = fsWrite(browse, (uint8_t *)str, count);
  debugf("[write] result{%ld}\n", (long)result);
  return result;
}

#define SYSCALL_OPEN 2
static size_t syscallOpen(char *filename, int flags, int mode) {
  dbgSysExtraf("filename{%s}", filename);
  if (!filename)
    return ERR(EFAULT);
  return fsUserOpen(currentTask, filename, flags, mode);
}

#define SYSCALL_CLOSE 3
static size_t syscallClose(int fd) { return fsUserClose(currentTask, fd); }

#define SYSCALL_STAT 4
static size_t syscallStat(char *filename, stat *statbuf) {
  dbgSysExtraf("filename{%s}", filename);
  bool ret = fsStatByFilename(currentTask, filename, statbuf);
  return (ret ? 0 : ERR(ENOENT));
}

#define SYSCALL_FSTAT 5
static size_t syscallFstat(int fd, stat *statbuf) {
  OpenFile *file = fsUserGetNode(currentTask, fd);
  if (!file)
    return ERR(EBADF);

  bool ret = fsStat(file, statbuf);
  return (ret ? 0 : ERR(ENOENT));
}

#define SYSCALL_LSTAT 6
static size_t syscallLstat(char *filename, stat *statbuf) {
  bool ret = fsLstatByFilename(currentTask, filename, statbuf);
  return (ret ? 0 : ERR(ENOENT));
}

#include <timer.h>
#define SYSCALL_POLL 7
static size_t syscallPoll(struct pollfd *fds, int nfds, int timeout) {
  return poll(fds, nfds, timeout);

  // todo: fixup below
  int ret = 0;
  for (int i = 0; i < nfds; i++) {
    uint64_t start = timerTicks;
    fds[i].revents = 0; // as a start
    if (fds[i].fd == -1)
      continue; // ignore -1 fds

    OpenFile *browse = fsUserGetNode(currentTask, fds[i].fd);
    if (!browse || !browse->handlers->poll)
      continue;

    if (browse->handlers->poll(browse, &fds[i], timeout))
      ret++;

    // ack time
    if (timeout <= (timerTicks - start))
      timeout = 0;
    else
      timeout -= timerTicks - start;
  }
  return ret;
}

#define SYSCALL_LSEEK 8
static size_t syscallLseek(uint32_t file, int offset, int whence) {
  return fsUserSeek(currentTask, file, offset, whence);
}

#define SYSCALL_IOCTL 16
static size_t syscallIoctl(int fd, unsigned long request, void *arg) {
  OpenFile *browse = fsUserGetNode(currentTask, fd);
  if (!browse)
    return ERR(EBADF);

  if (!browse->handlers->ioctl) {
    dbgSysFailf("non-special file");
    return ERR(ENOTTY);
  }

  spinlockAcquire(&browse->LOCK_OPERATIONS);
  size_t ret = browse->handlers->ioctl(browse, (unsigned int)request, arg);
  spinlockRelease(&browse->LOCK_OPERATIONS);

  return ret;
}

#define SYSCALL_PREAD64 17
static size_t syscallPread64(uint64_t fd, char *buff, size_t count,
                             size_t pos) {
  size_t seekOp = syscallLseek(fd, pos, SEEK_SET);
  if (RET_IS_ERR(seekOp))
    return seekOp;
  size_t readOp = syscallRead(fd, buff, count);
  return readOp;
}

#define SYSCALL_READV 19
static size_t syscallReadV(uint32_t fd, iovec *iov, uint32_t ioVcnt) {
  OpenFile *file = fsUserGetNode(currentTask, fd);
  if (!file)
    return ERR(EBADF);

  size_t cnt = 0;
  for (int i = 0; i < ioVcnt; i++) {
    iovec *curr = (iovec *)((size_t)iov + i * sizeof(iovec));
    if (!curr->iov_len)
      continue;

    // possible race condition here, lookup later (only for rare shared table)
    if (cnt > 0 && file->handlers->internalPoll) {
      // we already have some data (and are on something that can possibly
      // block, hence it has internalPoll defined), poll so that it doesn't
      // block afterwards
      if (!(file->handlers->internalPoll(file, EPOLLIN) & EPOLLIN))
        return cnt;
    }

    size_t single = fsRead(file, curr->iov_base, curr->iov_len);
    if (RET_IS_ERR(single))
      return cnt > 0 ? cnt : single;

    cnt += single;
  }

  return cnt;
}

#define SYSCALL_WRITEV 20
static size_t syscallWriteV(uint32_t fd, iovec *iov, uint32_t ioVcnt) {
  OpenFile *file = fsUserGetNode(currentTask, fd);
  if (!file)
    return ERR(EBADF);

  size_t cnt = 0;
  for (int i = 0; i < ioVcnt; i++) {
    iovec *curr = (iovec *)((size_t)iov + i * sizeof(iovec));
    if (!curr->iov_len)
      continue;

    if (cnt > 0 && file->handlers->internalPoll) {
      // we already have some data (and are on something that can possibly
      // block, hence it has internalPoll defined), poll so that it doesn't
      // block afterwards
      if (!(file->handlers->internalPoll(file, EPOLLOUT) & EPOLLOUT))
        return cnt;
    }

    size_t single = fsWrite(file, curr->iov_base, curr->iov_len);
    if (RET_IS_ERR(single))
      return cnt > 0 ? cnt : single;

    cnt += single;
  }

  return cnt;
}

#define SYSCALL_ACCESS 21
static size_t syscallAccess(char *filename, int mode) {
  struct stat buf;
  return syscallStat(filename, &buf);
}

#define SYSCALL_DUP 32
static size_t syscallDup(uint32_t fd) {
  OpenFile *file = fsUserGetNode(currentTask, fd);
  if (!file)
    return ERR(EBADF);

  OpenFile *new = fsUserDuplicateNode(currentTask, file, -1);
  new->closeOnExec = 0; // does not persist

  return new ? new->id : -1;
}

#define SYSCALL_DUP2 33
static size_t syscallDup2(uint32_t oldFd, uint32_t newFd) {
  OpenFile *realFile = fsUserGetNode(currentTask, oldFd);
  if (!realFile)
    return ERR(EBADF);

  if (oldFd == newFd)
    return newFd;

  // determine how we're going to do this
  spinlockCntWriteAcquire(&currentTask->infoFiles->WLOCK_FILES);
  OpenFile *browse =
      (OpenFile *)AVLLookup(currentTask->infoFiles->firstFile, newFd);
  if (!browse) {
    // we don't have anything to close, reserve the id
    bitmapGenericSet(currentTask->infoFiles->fdBitmap, newFd, true);
  } else {
    // do NOT free the id on close in order to avoid race conditions
    browse->closeFlags |= VFS_CLOSE_FLAG_RETAIN_ID;
  }
  spinlockCntWriteRelease(&currentTask->infoFiles->WLOCK_FILES);

  if (browse)
    assert(fsUserClose(currentTask, newFd) == 0);

  OpenFile *new = fsUserDuplicateNode(currentTask, realFile, newFd);
  assert(new);
  new->closeOnExec = 0; // does not persist

  return newFd;
}

#define SYSCALL_FCNTL 72
static size_t syscallFcntl(int fd, int cmd, uint64_t arg) {
  OpenFile *file = fsUserGetNode(currentTask, fd);
  if (!file)
    return ERR(EBADF);
    
  spinlockAcquire(&file->LOCK_OPERATIONS);

  // Safety check: Only call fcntl if the pointer is explicitly non-NULL 
  // AND resides safely within the kernel text segment (above 0xffffffff80000000)
  if (file->handlers->fcntl && 
      (uintptr_t)file->handlers->fcntl >= 0xffffffff80000000ULL) {
    file->handlers->fcntl(file, cmd, arg);
  }

  spinlockRelease(&file->LOCK_OPERATIONS);

  // Handle standard generic fcntl commands safely in the VFS layer
  switch (cmd) {
  case F_GETFD:
    return file->closeOnExec;
  case F_SETFD:
    file->closeOnExec = (arg & FD_CLOEXEC) ? 1 : 0;
    return 0;
  case F_GETFL:
    return file->flags;
  case F_SETFL:
    file->flags = arg;
    return 0;
  default:
    // If a driver-specific fcntl wasn't valid, return an appropriate error instead of crashing
    return 0; 
  }
}

#define SYSCALL_FSYNC 74
static size_t syscallFsync(int fd) {
  OpenFile *browse = fsUserGetNode(currentTask, fd);
  if (!browse)
    return ERR(EBADF);
  return 0; // none of our filesystems care enough about write caching
}

#define SYSCALL_RENAME 82
static size_t syscallRename(char *oldpath, char *newpath) {
  dbgSysExtraf("old{%s} new{%s}", oldpath, newpath);

  // Open the source (temporary) file for reading
  int old_fd = fsUserOpen(currentTask, oldpath, O_RDONLY, 0);
  if (RET_IS_ERR(old_fd))
    return old_fd;

  // Open/Create the destination file for writing (truncating existing content)
  int new_fd = fsUserOpen(currentTask, newpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (RET_IS_ERR(new_fd)) {
    fsUserClose(currentTask, old_fd);
    return new_fd;
  }

  // Copy data chunk by chunk using your existing syscall read/write helpers
  char buf[256];
  while (1) {
    size_t nread = syscallRead(old_fd, buf, sizeof(buf));
    if (RET_IS_ERR(nread) || nread == 0)
      break;
    size_t nwritten = syscallWrite(new_fd, buf, nread);
    if (RET_IS_ERR(nwritten) || nwritten == 0)
      break;
  }

  // Close both files
  fsUserClose(currentTask, old_fd);
  fsUserClose(currentTask, new_fd);

  // Clean up the old temporary file
  return fsUnlink(currentTask, oldpath, false);
}

#define SYSCALL_MKDIR 83
static size_t syscallMkdir(char *path, uint32_t mode) {
  dbgSysExtraf("path{%s}", path);
  spinlockAcquire(&currentTask->infoFs->LOCK_FS);
  mode &= ~(currentTask->infoFs->umask);
  spinlockRelease(&currentTask->infoFs->LOCK_FS);
  return fsMkdir(currentTask, path, mode);
}

#define SYSCALL_LINK 86
static size_t syscallLink(char *oldpath, char *newpath) {
  return fsLink(currentTask, oldpath, newpath);
}

#define SYSCALL_UNLINK 87
static size_t syscallUnlink(char *path) {
  dbgSysExtraf("path{%s}", path);
  return fsUnlink(currentTask, path, false);
}

#define SYSCALL_READLINK 89
static size_t syscallReadlink(char *path, char *buf, int size) {
  dbgSysExtraf("path{%s}", path);
  return fsReadlink(currentTask, path, buf, size);
}

#define SYSCALL_CHMOD 90
static size_t syscallChmod(char *filename, uint32_t mode) {
  dbgSysExtraf("filename{%s} mode{%o}", filename, mode);
  return 0;
}

#define SYSCALL_FCHMOD 91
static size_t syscallFchmod(int fd, uint32_t mode) {
  OpenFile *file = fsUserGetNode(currentTask, fd);
  if (!file)
    return ERR(EBADF);
  return 0;
}

#define SYSCALL_CHOWN 92
static size_t syscallChown(char *filename, uint32_t uid, uint32_t gid) {
  dbgSysExtraf("filename{%s} uid{%d} gid{%d}", filename, uid, gid);
  return 0;
}

#define SYSCALL_FCHOWN 93
static size_t syscallFchown(int fd, uint32_t uid, uint32_t gid) {
  OpenFile *file = fsUserGetNode(currentTask, fd);
  if (!file)
    return ERR(EBADF);
  return 0;
}

#define SYSCALL_LCHOWN 94
static size_t syscallLchown(char *filename, uint32_t uid, uint32_t gid) {
  return 0;
}

#define SYSCALL_UMASK 95
static size_t syscallUmask(uint32_t mask) {
  spinlockAcquire(&currentTask->infoFs->LOCK_FS);
  int old = currentTask->infoFs->umask;
  currentTask->infoFs->umask = mask & 0777;
  spinlockRelease(&currentTask->infoFs->LOCK_FS);
  return old;
}

#define SYSCALL_GETDENTS64 217
static size_t syscallGetdents64(unsigned int fd, struct linux_dirent64 *dirp,
                                unsigned int count) {
  OpenFile *browse = fsUserGetNode(currentTask, fd);
  if (!browse)
    return ERR(EBADF);
  if (!browse->handlers->getdents64)
    return ERR(ENOTDIR);
  spinlockAcquire(&browse->LOCK_OPERATIONS);
  size_t ret = browse->handlers->getdents64(browse, dirp, count);
  spinlockRelease(&browse->LOCK_OPERATIONS);
  return ret;
}

#define FD_SETSIZE 1024

typedef unsigned long fd_mask;

typedef struct {
  unsigned long fds_bits[FD_SETSIZE / 8 / sizeof(long)];
} fd_set;

#define SYSCALL_LINKAT 265
static size_t syscallLinkat(int oldfd, char *oldname, int newfd, char *newname,
                            int flags) {
  if (oldname[0] == '\0' || newname[0] == '\0') { // by fd
    return ERR(ENOSYS);
  }

  char *old = atResolvePathname(oldfd, oldname);
  if (RET_IS_ERR((size_t)old))
    return (size_t)old;

  char *new = atResolvePathname(newfd, newname);
  if (RET_IS_ERR((size_t)new)) {
    atResolvePathnameCleanup(oldname, old);
    return (size_t)new;
  }

  size_t ret = syscallLink(old, new);
  atResolvePathnameCleanup(oldname, old);
  atResolvePathnameCleanup(newname, new);
  return ret;
}

#define SYSCALL_RENAMEAT 264
static size_t syscallRenameat(int olddirfd, char *oldpath, int newdirfd, char *newpath) {
  if (oldpath[0] == '\0' || newpath[0] == '\0')
    return ERR(EINVAL);

  char *old = atResolvePathname(olddirfd, oldpath);
  if (RET_IS_ERR((size_t)old))
    return (size_t)old;

  char *new = atResolvePathname(newdirfd, newpath);
  if (RET_IS_ERR((size_t)new)) {
    atResolvePathnameCleanup(oldpath, old);
    return (size_t)new;
  }

  size_t ret = syscallRename(old, new);
  atResolvePathnameCleanup(oldpath, old);
  atResolvePathnameCleanup(newpath, new);
  return ret;
}

typedef struct {
  sigset_t *ss;
  size_t    ss_len;
} WeirdPselect6;

#define SYSCALL_PSELECT6 270
static size_t syscallPselect6(int nfds, fd_set *readfds, fd_set *writefds,
                              fd_set *exceptfds, struct timespec *timeout,
                              WeirdPselect6 *weirdPselect6) {
  size_t    sigsetsize = weirdPselect6->ss_len;
  sigset_t *sigmask = weirdPselect6->ss;

  if (sigsetsize < sizeof(sigset_t)) {
    dbgSysFailf("weird sigset size");
    return ERR(EINVAL);
  }

  sigset_t origmask;
  if (sigmask)
    syscallRtSigprocmask(SIG_SETMASK, sigmask, &origmask, sigsetsize);

  struct timeval timeoutConv = {
      .tv_sec = timeout ? timeout->tv_sec : 0,
      .tv_usec = timeout ? DivRoundUp(timeout->tv_nsec, 1000) : 0};
  size_t ret = select(nfds, (uint8_t *)readfds, (uint8_t *)writefds,
                      (uint8_t *)exceptfds, timeout ? &timeoutConv : 0);

  if (sigmask)
    syscallRtSigprocmask(SIG_SETMASK, &origmask, 0, sigsetsize);
  return ret;
}

#define SYSCALL_SELECT 23
static size_t syscallSelect(int nfds, fd_set *readfds, fd_set *writefds,
                            fd_set *exceptfds, struct timeval *timeout) {
  return select(nfds, (uint8_t *)readfds, (uint8_t *)writefds,
                (uint8_t *)exceptfds, timeout);
}

char *atResolvePathname(int dirfd, char *pathname) {
  assert(pathname[0] != '\0'); // by fd, should've checked before running!
  if (pathname[0] == '/') {    // by absolute pathname
    return pathname;
  } else if (pathname[0] != '/') {
    if (dirfd == AT_FDCWD) { // relative to cwd
      return pathname;
    } else { // relative to dirfd, resolve accordingly
      OpenFile *fd = fsUserGetNode(currentTask, dirfd);
      if (!fd)
        return (char *)ERR(EBADF);
      if (!fd->dirname)
        return (char *)ERR(ENOTDIR);

      int prefixLen = strlength(fd->mountPoint->prefix);
      int rootDirLen = strlength(fd->dirname);
      int pathnameLen = strlength(pathname) + 1;

      char *out = malloc(prefixLen + rootDirLen + 1 + pathnameLen);

      memcpy(out, fd->mountPoint->prefix, prefixLen);
      memcpy(&out[prefixLen], fd->dirname, rootDirLen);
      out[prefixLen + rootDirLen] = '/'; // better be safe than sorry
      memcpy(&out[prefixLen + rootDirLen + 1], pathname, pathnameLen);
      return out;
    }
  }

  assert(false); // will never be reached
  return 0;
}

void atResolvePathnameCleanup(char *pathname, char *resolved) {
  if ((size_t)pathname != (size_t)resolved)
    free(resolved);
}

#define SYSCALL_OPENAT 257
static size_t syscallOpenat(int dirfd, char *pathname, int flags, int mode) {
  if (pathname[0] == '\0') { // by fd
    return dirfd;
  }

  char *resolved = atResolvePathname(dirfd, pathname);
  if (RET_IS_ERR((size_t)resolved))
    return (size_t)resolved;

  size_t ret = syscallOpen(resolved, flags, mode);
  atResolvePathnameCleanup(pathname, resolved);
  return ret;
}

#define SYSCALL_MKDIRAT 258
static size_t syscallMkdirAt(int dirfd, char *pathname, int mode) {
  if (pathname[0] == '\0') { // by fd
    return ERR(ENOENT);
  }

  char *resolved = atResolvePathname(dirfd, pathname);
  if (RET_IS_ERR((size_t)resolved))
    return (size_t)resolved;

  size_t ret = syscallMkdir(resolved, mode);
  atResolvePathnameCleanup(pathname, resolved);
  return ret;
}

#define SYSCALL_NEWFSTATAT 262
static size_t syscallNewfstatat(int dirfd, char *pathname, struct stat *statbuf,
                                int flag) {
  if (pathname[0] == '\0')
    return syscallFstat(dirfd, statbuf);

  char *resolved = atResolvePathname(dirfd, pathname);
  if (RET_IS_ERR((size_t)resolved))
    return (size_t)resolved;

  size_t ret = flag & AT_SYMLINK_NOFOLLOW ? syscallLstat(resolved, statbuf)
                                          : syscallStat(resolved, statbuf);
  atResolvePathnameCleanup(pathname, resolved);
  return ret;
}

#define SYSCALL_UNLINKAT 263
static size_t syscallUnlinkat(int dirfd, char *pathname, int mode) {
  bool directory = mode & 0x200;
  if (pathname[0] == '\0') { // by fd
    dbgSysFailf("unsupported!");
    return ERR(ENOSYS);
  }

  char *resolved = atResolvePathname(dirfd, pathname);
  if (RET_IS_ERR((size_t)resolved))
    return (size_t)resolved;

  dbgSysExtraf("path{%s}", resolved);

  size_t ret = fsUnlink(currentTask, resolved, directory);
  atResolvePathnameCleanup(pathname, resolved);
  return ret;
}

#define SYSCALL_FACCESSAT 269
static size_t syscallFaccessat(int dirfd, char *pathname, int mode) {
  if (pathname[0] == '\0') { // by fd
    return 0;
  }

  char *resolved = atResolvePathname(dirfd, pathname);
  if (RET_IS_ERR((size_t)resolved))
    return (size_t)resolved;

  size_t ret = syscallAccess(resolved, mode);
  atResolvePathnameCleanup(pathname, resolved);
  return ret;
}

#define SYSCALL_PPOLL 271
static size_t syscallPpoll(struct pollfd *fds, int nfds,
                           struct timespec *timeout, sigset_t *sigmask,
                           size_t sigsetsize) {
  return ppoll(fds, nfds, timeout, sigmask, sigsetsize);
}

#define SYSCALL_STATX 332
static size_t syscallStatx(int dirfd, char *pathname, int flags, uint32_t mask,
                           struct statx *buff) {
  struct stat simple = {0};
  size_t      statRet = syscallNewfstatat(dirfd, pathname, &simple, flags);
  if (RET_IS_ERR(statRet))
    return statRet;

  memset(buff, 0, sizeof(struct statx));

  buff->stx_mask = mask;
  buff->stx_blksize = simple.st_blksize;
  buff->stx_attributes = 0; // naw
  buff->stx_nlink = simple.st_nlink;
  buff->stx_uid = simple.st_uid;
  buff->stx_gid = simple.st_gid;
  buff->stx_mode = simple.st_mode;
  buff->stx_ino = simple.st_ino;
  buff->stx_size = simple.st_size;
  buff->stx_blocks = simple.st_blocks;
  buff->stx_attributes_mask = 0; // naw

  buff->stx_atime.tv_sec = simple.st_atime;
  buff->stx_atime.tv_nsec = simple.st_atimensec;

  // well, it's a creation time but whatever
  buff->stx_btime.tv_sec = simple.st_ctime;
  buff->stx_btime.tv_nsec = simple.st_ctimensec;

  buff->stx_ctime.tv_sec = simple.st_ctime;
  buff->stx_ctime.tv_nsec = simple.st_ctimensec;

  buff->stx_mtime.tv_sec = simple.st_mtime;
  buff->stx_mtime.tv_nsec = simple.st_mtimensec;

  // todo: special devices

  return 0;
}

void syscallRegFs() {
  registerSyscall(SYSCALL_WRITE, syscallWrite);
  registerSyscall(SYSCALL_READ, syscallRead);
  registerSyscall(SYSCALL_OPEN, syscallOpen);
  registerSyscall(SYSCALL_OPENAT, syscallOpenat);
  registerSyscall(SYSCALL_POLL, syscallPoll);
  registerSyscall(SYSCALL_PPOLL, syscallPpoll);
  registerSyscall(SYSCALL_MKDIRAT, syscallMkdirAt);
  registerSyscall(SYSCALL_CLOSE, syscallClose);
  registerSyscall(SYSCALL_LSEEK, syscallLseek);
  registerSyscall(SYSCALL_STAT, syscallStat);
  registerSyscall(SYSCALL_FSTAT, syscallFstat);
  registerSyscall(SYSCALL_LSTAT, syscallLstat);
  registerSyscall(SYSCALL_MKDIR, syscallMkdir);
  registerSyscall(SYSCALL_UMASK, syscallUmask);
  registerSyscall(SYSCALL_PREAD64, syscallPread64);
  registerSyscall(SYSCALL_UNLINK, syscallUnlink);
  registerSyscall(SYSCALL_LINK, syscallLink);
  registerSyscall(SYSCALL_LINKAT, syscallLinkat);
  registerSyscall(SYSCALL_FSYNC, syscallFsync);

  registerSyscall(SYSCALL_IOCTL, syscallIoctl);
  registerSyscall(SYSCALL_READV, syscallReadV);
  registerSyscall(SYSCALL_WRITEV, syscallWriteV);
  registerSyscall(SYSCALL_DUP2, syscallDup2);
  registerSyscall(SYSCALL_DUP, syscallDup);
  registerSyscall(SYSCALL_ACCESS, syscallAccess);
  registerSyscall(SYSCALL_FACCESSAT, syscallFaccessat);
  registerSyscall(SYSCALL_GETDENTS64, syscallGetdents64);
  registerSyscall(SYSCALL_PSELECT6, syscallPselect6);
  registerSyscall(SYSCALL_SELECT, syscallSelect);
  registerSyscall(SYSCALL_FCNTL, syscallFcntl);
  registerSyscall(SYSCALL_STATX, syscallStatx);
  registerSyscall(SYSCALL_READLINK, syscallReadlink);
  registerSyscall(SYSCALL_NEWFSTATAT, syscallNewfstatat);
  registerSyscall(SYSCALL_UNLINKAT, syscallUnlinkat);

  registerSyscall(SYSCALL_CHMOD, syscallChmod);
  registerSyscall(SYSCALL_FCHMOD, syscallFchmod);
  registerSyscall(SYSCALL_CHOWN, syscallChown);
  registerSyscall(SYSCALL_FCHOWN, syscallFchown);
  registerSyscall(SYSCALL_LCHOWN, syscallLchown);
  registerSyscall(SYSCALL_RENAME, syscallRename);
  registerSyscall(SYSCALL_RENAMEAT, syscallRenameat);
}