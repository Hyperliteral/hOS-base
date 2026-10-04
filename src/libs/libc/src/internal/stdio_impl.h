// Internal FILE layout: byte-for-byte musl-compatible.
//
// struct _IO_FILE is musl's PUBLIC layout - musl-compiled binaries reach
// directly into it (bash reads stdin->wend at +0x20), so these offsets are
// ABI, not implementation detail. The layout below mirrors musl-1.2.5's
// src/internal/stdio_impl.h exactly, enforced by the static asserts.
//
// Semantics stay ours: UNBUFFERED. All buffer pointers remain NULL with
// rpos==rend and wpos==wbase (empty), so foreign buffer checks observe "no
// buffered data" - the truthful state. Flags use musl's values (F_PERM,
// F_NORD/F_NOWR, F_EOF/F_ERR, F_APP) for the same reason. Pushback lives in
// a side table (see pushback.c), never in the struct.
#ifndef CAVOS_INTERNAL_STDIO_IMPL_H
#define CAVOS_INTERNAL_STDIO_IMPL_H

#include <stddef.h>
#include <stdio.h>

struct __locale_struct;

struct _IO_FILE {
  unsigned       flags;        // 0x00
  unsigned char *rpos, *rend; // 0x08, 0x10
  int (*close)(FILE *);       // 0x18
  unsigned char *wend, *wpos;  // 0x20, 0x28
  unsigned char *mustbezero_1; // 0x30
  unsigned char *wbase;        // 0x38
  size_t (*read)(FILE *, unsigned char *, size_t);       // 0x40
  size_t (*write)(FILE *, const unsigned char *, size_t); // 0x48
  off_t (*seek)(FILE *, off_t, int);                     // 0x50
  unsigned char *buf;          // 0x58
  size_t         buf_size;     // 0x60
  FILE          *prev, *next;  // 0x68, 0x70
  int            fd;           // 0x78
  int            pipe_pid;     // 0x7c
  long           lockcount;    // 0x80
  int            mode;         // 0x88
  volatile int   lock;         // 0x8c
  int            lbf;          // 0x90
  void          *cookie;       // 0x98
  off_t          off;          // 0xa0
  char          *getln_buf;    // 0xa8
  void          *mustbezero_2; // 0xb0
  unsigned char *shend;        // 0xb8
  off_t          shlim, shcnt; // 0xc0, 0xc8
  FILE *prev_locked, *next_locked; // 0xd0, 0xd8
  struct __locale_struct *locale;  // 0xe0
};

_Static_assert(sizeof(struct _IO_FILE) == 0xe8, "FILE layout drifted");
_Static_assert(__builtin_offsetof(struct _IO_FILE, wend) == 0x20,
               "wend offset drifted");
_Static_assert(__builtin_offsetof(struct _IO_FILE, fd) == 0x78,
               "fd offset drifted");
_Static_assert(__builtin_offsetof(struct _IO_FILE, flags) == 0x00,
               "flags offset drifted");

// musl flag values (src/internal/stdio_impl.h). Readable unless F_NOWR,
// writable unless F_NORD; F_PERM marks the three immortal streams.
#define CAVOS_F_PERM 1
#define CAVOS_F_NORD 4
#define CAVOS_F_NOWR 8
#define CAVOS_F_EOF  16
#define CAVOS_F_ERR  32
#define CAVOS_F_APP  128

// fopen() mode parsing shared by nothing else, but kept visible for tests.
int __cavos_parse_mode(const char *mode);

// Single-character pushback without touching the musl-ABI FILE layout (see
// pushback.c). Peek/consume/store/drop by stream.
int  __cavos_pushback_peek(FILE *file);
void __cavos_pushback_take(FILE *file);
int  __cavos_pushback_put(FILE *file, int c);
void __cavos_pushback_drop(FILE *file);

#endif
