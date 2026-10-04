#include <netdb.h>
#include <stdlib.h>

void freeaddrinfo(struct addrinfo *info) { free(info); }
