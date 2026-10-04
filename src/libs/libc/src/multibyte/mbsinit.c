#include <stdlib.h>
#include <wchar.h>

int mbsinit(const mbstate_t *ps)
{
	(void)ps;
	// Stateless encoding: every state is the initial state.
	return 1;
}
