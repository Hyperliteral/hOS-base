#include <signal.h>
#include <sys/types.h>

#include "internal/syscall.h"

int killpg(int pgrp, int sig) { return kill(-pgrp, sig); }
