#include <sys/utsname.h>
#include <unistd.h>

// No SYS_gethostname on this table: nodename from uname() is the same
// string the kernel would report.
int gethostname(char *name, size_t len) {
  struct utsname uts;
  if (uname(&uts) < 0)
    return -1;
  size_t i = 0;
  while (uts.nodename[i] && i + 1 < len) {
    name[i] = uts.nodename[i];
    i++;
  }
  if (!len)
    return -1;
  name[i] = 0;
  return uts.nodename[i] ? -1 : 0;
}
