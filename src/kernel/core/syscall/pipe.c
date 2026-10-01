#include <console.h>
#include <fb.h>
#include <kb.h>
#include <linux.h>
#include <malloc.h>
#include <poll.h>
#include <syscalls.h>
#include <task.h>

// Industrial two-way solid steel pipe()
// Copyright (C) 2024 Panagiotis

#define PIPE_BUFF 65536
#define PIPE_ATOMIC 4096
typedef struct PipeInfo {
  char   buf[PIPE_BUFF];
  size_t head;
  size_t assigned;

  int writeFds;
  int readFds;

  Spinlock LOCK;
} PipeInfo;

typedef struct PipeSpecific PipeSpecific;
struct PipeSpecific {
  bool      write;
  PipeInfo *info;
};

size_t pipeOpen(int *fds) {
  int readFd = fsUserOpen(currentTask, "/dev/stdout", O_RDONLY, 0);
  int writeFd = fsUserOpen(currentTask, "/dev/stdout", O_WRONLY, 0);

  if (readFd < 0 || writeFd < 0)
    goto bad_error;

  OpenFile *read = fsUserGetNode(currentTask, readFd);
  OpenFile *write = fsUserGetNode(currentTask, writeFd);

  if (!read || !write)
    goto bad_error;

  read->handlers = &pipeReadEnd;
  write->handlers = &pipeWriteEnd;

  PipeInfo *info = (PipeInfo *)malloc(sizeof(PipeInfo));
  memset(info, 0, sizeof(PipeInfo));
  info->readFds = 1;
  info->writeFds = 1;

  PipeSpecific *readSpec = (PipeSpecific *)malloc(sizeof(PipeSpecific));
  readSpec->write = false;
  readSpec->info = info;

  PipeSpecific *writeSpec = (PipeSpecific *)malloc(sizeof(PipeSpecific));
  writeSpec->write = true;
  writeSpec->info = info;

  read->dir = readSpec;
  write->dir = writeSpec;

  fds[0] = readFd;
  fds[1] = writeFd;

  dbgSysExtraf("fds{%d, %d}", fds[0], fds[1]);

  return 0;

bad_error:
  debugf("[pipe] Very bad error!\n");
  return -1;
}

bool pipeDuplicate(OpenFile *original, OpenFile *orphan) {
  orphan->dir = malloc(sizeof(PipeSpecific));
  memcpy(orphan->dir, original->dir, sizeof(PipeSpecific));

  PipeSpecific *spec = (PipeSpecific *)original->dir;
  PipeInfo     *pipe = spec->info;

  spinlockAcquire(&pipe->LOCK);
  if (spec->write)
    pipe->writeFds++;
  else
    pipe->readFds++;
  spinlockRelease(&pipe->LOCK);

  return true;
}

size_t pipeRead(OpenFile *fd, uint8_t *out, size_t limit) {
  if (!limit)
    return 0;
  if (limit > PIPE_BUFF)
    limit = PIPE_BUFF;
  PipeSpecific *spec = (PipeSpecific *)fd->dir;
  PipeInfo     *pipe = spec->info;

  // if there are no more write items, don't hang
  while (true) {
    spinlockAcquire(&pipe->LOCK);
    if (pipe->writeFds == 0 || pipe->assigned > 0)
      break;
    if (fd->flags & O_NONBLOCK) {
      spinlockRelease(&pipe->LOCK);
      return ERR(EWOULDBLOCK);
    }
    if (signalsPendingQuick(currentTask)) {
      spinlockRelease(&pipe->LOCK);
      return ERR(EINTR);
    }
    spinlockRelease(&pipe->LOCK);
    pollIndependentAwait(fd, EPOLLIN);
  }

  if (!pipe->assigned) {
    spinlockRelease(&pipe->LOCK);
    return 0;
  }

  // we already have a spinlock!
  size_t toCopy = MIN(pipe->assigned, limit);
  size_t first = MIN(toCopy, PIPE_BUFF - pipe->head);
  memcpy(out, &pipe->buf[pipe->head], first);
  if (toCopy > first)
    memcpy(out + first, pipe->buf, toCopy - first);
  pipe->head = (pipe->head + toCopy) % PIPE_BUFF;
  pipe->assigned -= toCopy;
  spinlockRelease(&pipe->LOCK);
  pollInstanceRing((size_t)pipe, EPOLLOUT);

  return toCopy;
}

size_t pipeWriteInner(OpenFile *fd, uint8_t *in, size_t limit) {
  if (!limit)
    return 0;
  PipeSpecific *spec = (PipeSpecific *)fd->dir;
  PipeInfo     *pipe = spec->info;
  while (true) {
    spinlockAcquire(&pipe->LOCK);
    if (!pipe->readFds) {
      spinlockRelease(&pipe->LOCK);
      atomicBitmapSet(&currentTask->sigPendingList, SIGPIPE);
      return ERR(EPIPE);
    }
    size_t available = PIPE_BUFF - pipe->assigned;
    if (available && (limit > PIPE_ATOMIC || available >= limit)) {
      size_t toCopy = MIN(limit, available);
      size_t tail = pipe->head + pipe->assigned;
      if (tail >= PIPE_BUFF)
        tail -= PIPE_BUFF;
      size_t first = MIN(toCopy, PIPE_BUFF - tail);
      memcpy(&pipe->buf[tail], in, first);
      if (toCopy > first)
        memcpy(pipe->buf, in + first, toCopy - first);
      pipe->assigned += toCopy;
      spinlockRelease(&pipe->LOCK);
      pollInstanceRing((size_t)pipe, EPOLLIN);
      return toCopy;
    }
    if (fd->flags & O_NONBLOCK) {
      spinlockRelease(&pipe->LOCK);
      return ERR(EWOULDBLOCK);
    }
    if (signalsPendingQuick(currentTask)) {
      spinlockRelease(&pipe->LOCK);
      return ERR(EINTR);
    }
    spinlockRelease(&pipe->LOCK);
    pollIndependentAwait(fd, EPOLLOUT);
  }
}

size_t pipeWrite(OpenFile *fd, uint8_t *in, size_t limit) {
  size_t total = 0;
  while (total < limit) {
    size_t innerRet = pipeWriteInner(fd, in + total, limit - total);
    if (RET_IS_ERR(innerRet)) {
      if (total)
        break;
      return innerRet;
    }
    total += innerRet;
  }
  return total;
}

int pipeInternalPoll(OpenFile *fd, int events) {
  PipeSpecific *spec = (PipeSpecific *)fd->dir;
  PipeInfo     *pipe = spec->info;

  int out = 0;

  spinlockAcquire(&pipe->LOCK);
  if (events & EPOLLIN) {
    if (!pipe->writeFds)
      out |= EPOLLHUP;
    if (pipe->assigned > 0)
      out |= EPOLLIN;
  }

  if (events & EPOLLOUT) {
    if (!pipe->readFds)
      out |= EPOLLERR;
    else if (pipe->assigned < PIPE_BUFF)
      out |= EPOLLOUT;
  }
  spinlockRelease(&pipe->LOCK);
  return out;
}

size_t pipeReportKey(OpenFile *fd) {
  PipeSpecific *spec = (PipeSpecific *)fd->dir;
  PipeInfo     *pipe = spec->info;
  return (size_t)pipe;
}

bool pipeCloseEnd(OpenFile *readFd) {
  PipeSpecific *spec = (PipeSpecific *)readFd->dir;
  PipeInfo     *pipe = spec->info;

  spinlockAcquire(&pipe->LOCK);
  if (spec->write)
    pipe->writeFds--;
  else
    pipe->readFds--;

  int pollWith = 0;
  if (!pipe->writeFds) // edge case
    pollWith |= EPOLLHUP;
  if (!pipe->readFds) // edge case (more aggressive)
    pollWith |= EPOLLERR;

  if (!pipe->readFds && !pipe->writeFds)
    free(pipe);
  else
    spinlockRelease(&pipe->LOCK);

  if (pollWith)
    pollInstanceRing((size_t)pipe, pollWith);

  free(spec);

  return true;
}

size_t pipeStat(OpenFile *fd, stat *stat) {
  stat->st_mode = 0x1180;
  stat->st_dev = 70;
  stat->st_ino = rand(); // todo!
  stat->st_mode = S_IFCHR | S_IRUSR | S_IWUSR;
  stat->st_nlink = 1;
  stat->st_uid = 0;
  stat->st_gid = 0;
  stat->st_rdev = 0;
  stat->st_blksize = 0x1000;
  stat->st_size = 0;
  stat->st_blocks = DivRoundUp(stat->st_size, 512);
  stat->st_atime = 69;
  stat->st_mtime = 69;
  stat->st_ctime = 69;
  return 0;
}

size_t pipeBadRead() { return ERR(EBADF); }
size_t pipeBadWrite() { return ERR(EBADF); }
size_t pipeBadIoctl() { return ERR(ENOTTY); }

size_t pipeBadMmap() { return -1; }

VfsHandlers pipeReadEnd = {.open = 0,
                           .close = pipeCloseEnd,
                           .duplicate = pipeDuplicate,
                           .ioctl = pipeBadIoctl,
                           .mmap = pipeBadMmap,
                           .stat = pipeStat,
                           .read = pipeRead,
                           .write = pipeBadWrite,
                           .internalPoll = pipeInternalPoll,
                           .reportKey = pipeReportKey,
                           .getdents64 = 0,
                           .fcntl = NULL}; // <-- Explicitly added

VfsHandlers pipeWriteEnd = {.open = 0,
                            .close = pipeCloseEnd,
                            .duplicate = pipeDuplicate,
                            .ioctl = pipeBadIoctl,
                            .mmap = pipeBadMmap,
                            .stat = pipeStat,
                            .read = pipeBadRead,
                            .write = pipeWrite,
                            .internalPoll = pipeInternalPoll,
                            .reportKey = pipeReportKey,
                            .getdents64 = 0,
                            .fcntl = NULL}; // <-- Explicitly added