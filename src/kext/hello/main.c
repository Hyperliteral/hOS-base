#include <serial.h>

int init_module(void) {
debugf("Hello from kernel module!\n");
return 0;
}