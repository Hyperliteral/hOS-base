#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Linux-style scalar aliases, so unmodified Linux driver sources compile.
// Most of these are for cosmetic compatibility only.

typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

typedef int8_t  __s8;
typedef int16_t __s16;
typedef int32_t __s32;
typedef int64_t __s64;

typedef __u8  u8;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;

typedef __s8  s8;
typedef __s16 s16;
typedef __s32 s32;
typedef __s64 s64;

typedef __u16 __le16;
typedef __u32 __le32;
typedef __u64 __le64;
typedef __u16 __be16;
typedef __u32 __be32;
typedef __u64 __be64;

typedef unsigned int  gfp_t;
typedef unsigned long ulong;
typedef unsigned long dma_addr_t;
typedef uint64_t          phys_addr_t;
typedef uint64_t          resource_size_t;
typedef long long         loff_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
