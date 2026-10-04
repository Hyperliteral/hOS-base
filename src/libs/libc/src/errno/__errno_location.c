#include <errno.h>

// Single global errno. No threads exist on cavOS yet, so there is no thread
// pointer to hang it off (musl keeps errno in __pthread_self()->errno_val).
// If threading ever lands, this is the one place to change.
static int errno_value;

int *__errno_location(void) { return &errno_value; }

// Three-underscore spelling some musl-built objects reference. Same cell,
// plain definition: Mach-O cannot express the weak alias musl uses.
int *___errno_location(void) { return &errno_value; }
