#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Vendored from musl-1.2.5 (src/env/getenv.c). The only adaptation is the
// internal declarations, which musl takes from its private headers - see
// src/internal/impl.h.
#include "internal/impl.h"

char *getenv(const char *name)
{
	size_t l = __strchrnul(name, '=') - name;
	if (l && !name[l] && __environ)
		for (char **e = __environ; *e; e++)
			if (!strncmp(name, *e, l) && l[*e] == '=')
				return *e + l+1;
	return 0;
}
