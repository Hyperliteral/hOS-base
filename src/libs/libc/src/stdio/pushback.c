#include <stdio.h>

#include "internal/stdio_impl.h"

// Single-character pushback WITHOUT touching struct _IO_FILE: that layout
// is musl ABI (see stdio_impl.h) and has no spare field. A small open table
// is plenty - processes hold few streams, and entries die with fclose().
#define PUSHBACK_SLOTS 32

static struct {
  FILE *file;
  int   c;
} pushbacks[PUSHBACK_SLOTS];

// Returns the pushed-back char without consuming, or -1 when empty.
int __cavos_pushback_peek(FILE *file) {
  for (size_t i = 0; i < PUSHBACK_SLOTS; i++) {
    if (pushbacks[i].file == file)
      return pushbacks[i].c;
  }
  return -1;
}

// Consumes the pushback. Call only after peek reported one.
void __cavos_pushback_take(FILE *file) {
  for (size_t i = 0; i < PUSHBACK_SLOTS; i++) {
    if (pushbacks[i].file == file) {
      pushbacks[i].file = 0;
      return;
    }
  }
}

// Stores one pushback char; -1 when the slot is occupied or the table full.
int __cavos_pushback_put(FILE *file, int c) {
  for (size_t i = 0; i < PUSHBACK_SLOTS; i++) {
    if (pushbacks[i].file == file)
      return -1;
  }
  for (size_t i = 0; i < PUSHBACK_SLOTS; i++) {
    if (!pushbacks[i].file) {
      pushbacks[i].file = file;
      pushbacks[i].c = c;
      return 0;
    }
  }
  return -1;
}

void __cavos_pushback_drop(FILE *file) {
  for (size_t i = 0; i < PUSHBACK_SLOTS; i++) {
    if (pushbacks[i].file == file)
      pushbacks[i].file = 0;
  }
}
