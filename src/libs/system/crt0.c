#include "system.h"

#define MAX_ARGS 64

extern int main(int argc, char *argv[]);

void _start(void) {
  int argc = syscallGetArgc();
  
  // Guard against corrupted argc causing out-of-bounds stack writes
  if (argc < 0) argc = 0;
  if (argc > MAX_ARGS) argc = MAX_ARGS;

  char *argv_buf[MAX_ARGS + 1]; // Safe fixed-size array

  for (int i = 0; i < argc; i++) {
    argv_buf[i] = syscallGetArgv(i);
  }
  argv_buf[argc] = (char *)0; // Ensure null-termination

  int returnCode = main(argc, argv_buf);
  syscallExitTask(returnCode);
}