#ifndef _LINUX_VERSION_H
#define _LINUX_VERSION_H

// Drivers generally only gate on this; report a modern kernel line.
#define LINUX_VERSION_CODE 0x060000
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))

#endif
