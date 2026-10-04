// Internal DIR layout for opendir/readdir/closedir.
//
// getdents64 fills an internal byte chunk; entries are parsed out of it one
// at a time into reusable return storage. struct dirent ends in a flexible
// d_name[], so the storage is sized explicitly (offset + NAME_MAX + NUL)
// rather than embedding the struct.
#ifndef CAVOS_INTERNAL_DIRENT_IMPL_H
#define CAVOS_INTERNAL_DIRENT_IMPL_H

#include <dirent.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

// Exact kernel layout of one getdents64 record header.
struct cavos_linux_dirent64 {
  uint64_t      d_ino;
  int64_t       d_off;
  unsigned short d_reclen;
  unsigned char  d_type;
  char           d_name[];
};

typedef struct CavosDir {
  int    fd;
  char   chunk[2048];
  size_t chunk_len;
  size_t chunk_pos;
  union {
    struct dirent entry;
    char space[__builtin_offsetof(struct dirent, d_name) + NAME_MAX + 1];
  } storage;
} CavosDir;

#endif
