/* Vendored from musl-1.2.5 (src/search/tsearch.h) with musl-internal
 * includes/macros removed. Included by the vendored tsearch family. */
#ifndef CAVOS_INTERNAL_TSEARCH_H
#define CAVOS_INTERNAL_TSEARCH_H

#include <search.h>

/* AVL tree height < 1.44*log2(nodes+2)-0.3, MAXH is a safe upper bound.  */
#define MAXH (sizeof(void*)*8*3/2)

struct node {
	const void *key;
	void *a[2];
	int h;
};

/* musl marks this hidden; plain global here (see strchrnul.c). */
int __tsearch_balance(void **);

#endif
