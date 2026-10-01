#include "task.h"
#include "types.h"

#ifndef KERNEL_HELPER_H
#define KERNEL_HELPER_H

Task *netHelperTask;
void  initiateKernelThreads();

// port-driven services
#if CONFIG_MH_PORTS
struct MachPort;
extern struct MachPort *portNetd;
extern struct MachPort *portReaper;
#endif

Spinlock LOCK_REAPER;
Task        *reaperTask;

#endif