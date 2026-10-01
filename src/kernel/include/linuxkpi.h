#ifndef LINUXKPI_H
#define LINUXKPI_H

// Kernel-side entry point for the Linux kernel programming interface shim.
// Called once during boot, after PCI is up, so the shim can publish its
// exported symbols to the module loader.
void initiateLinuxKPI(void);

#endif
