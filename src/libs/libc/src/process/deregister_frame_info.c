#include <stdlib.h>

// See register_frame_info.c: unwinding does not exist here.
void *__deregister_frame_info(const void *begin) {
  (void)begin;
  return 0;
}
