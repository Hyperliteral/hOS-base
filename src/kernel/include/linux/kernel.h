#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <linux/printk.h>
#include <linux/types.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#define container_of(ptr, type, member)                                                                                \
  ({                                                                                                                                                   \
        const __typeof__(((type *)0)->member) *__mptr = (ptr);                                         \
        (type *)((char *)__mptr - offsetof(type, member));                                                 \
  })

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min_t(type, a, b) ((type)min(a, b))
#define max_t(type, a, b) ((type)max(a, b))
#define clamp(v, lo, hi) min(max(v, lo), hi)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define BUG() __builtin_trap()
#define BUG_ON(cond)                                                                                                                   \
  do {                                                                                                                                                 \
        if (unlikely(cond))                                                                                                                \
          BUG();                                                                                                                                   \
  } while (0)
#define WARN_ON(cond) ((void)unlikely(cond))
#define WARN(cond, fmt, ...) WARN_ON(cond)
#define BUILD_BUG_ON(cond) ((void)sizeof(char[1 - 2 * !!(cond)]))

#define MSEC_PER_SEC 1000

#endif
