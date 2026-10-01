#ifndef _LINUX_ERRNO_H
#define _LINUX_ERRNO_H

// Minimal set of error codes Linux drivers commonly use
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define ENOTDIR 20
#define EINVAL 22
#define ENOSPC 28
#define ERANGE 34
#define ENOSYS 38
#define EOPNOTSUPP 95
#define ESOCKTNOSUPPORT 94
#define EAFNOSUPPORT 97
#define EBADF 9
#define ENOTSOCK 88
#define ENOTSUP EOPNOTSUPP

#endif
