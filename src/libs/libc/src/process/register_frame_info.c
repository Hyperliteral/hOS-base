#include <stdlib.h>

// Exception-frame registration for compilers that emit .eh_frame with
// runtime registration (Alpine's toolchain does for some objects). There is
// no unwinder here - nothing throws across these frames - so registration
// is correctly a no-op. If C++ exceptions ever land, this is one of the
// first places to revisit.
void __register_frame_info(const void *begin, void *object) {
  (void)begin;
  (void)object;
}
