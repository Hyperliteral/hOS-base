#define _BSD_SOURCE
#include <stdlib.h>

// Vendored from musl-1.2.5 (src/stdlib/qsort_nr.c). The internal
// header stands in for musl's private declarations - see
// src/internal/impl.h.
#include "internal/impl.h"

typedef int (*cmpfun)(const void *, const void *);

static int wrapper_cmp(const void *v1, const void *v2, void *cmp)
{
	return ((cmpfun)cmp)(v1, v2);
}

void qsort(void *base, size_t nel, size_t width, cmpfun cmp)
{
	__qsort_r(base, nel, width, wrapper_cmp, (void *)cmp);
}
