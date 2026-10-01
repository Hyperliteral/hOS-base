#include <macho.h>

int main(int argc, char **argv) {
  const char *value = getenv("MACHO_TEST");
  printf("Mach-O argc=%d name=%s env=%s\n", argc, argv[0],
         value ? value : "(unset)");
  return 0;
}
