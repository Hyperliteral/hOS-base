#ifndef _LINUX_SLAB_H
#define _LINUX_SLAB_H

#include <linux/types.h>

#define GFP_KERNEL 0x00000000u
#define GFP_ATOMIC 0x00000001u
#define GFP_USER 0x00000002u
#define __GFP_ZERO 0x00008000u

void *kmalloc(size_t size, gfp_t flags);
void *kzalloc(size_t size, gfp_t flags);
void *kcalloc(size_t n, size_t size, gfp_t flags);
void  kfree(const void *ptr);

#define kmalloc_array(n, size, flags)                                                                                  \
  ((size) != 0 && (n) > SIZE_MAX / (size) ? NULL : kmalloc((n) * (size), flags))
#define kzalloc_array(n, size, flags) kcalloc((n), (size), flags)

#endif
