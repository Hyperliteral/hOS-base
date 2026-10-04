#include <stddef.h>
#include <stdlib.h>

// C locale: single-byte encoding, so the maximum multibyte length is 1.
// Matches musl's answer under the C locale exactly.
size_t __ctype_get_mb_cur_max(void) { return 1; }
