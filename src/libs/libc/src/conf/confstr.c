#include <errno.h>
#include <stddef.h>
#include <unistd.h>

// Only _CS_PATH is meaningful here; everything else is an empty string like
// musl returns for unknown/empty values. Follows the confstr contract:
// returns the full length regardless of len, truncates the copy.
size_t confstr(int name, char *buf, size_t len) {
  const char *value = "";
  if (name == _CS_PATH)
    value = "/bin:/usr/bin";
  size_t full = 0;
  while (value[full])
    full++;
  if (len) {
    size_t copy = full < len ? full : len - 1;
    for (size_t i = 0; i < copy; i++)
      buf[i] = value[i];
    buf[copy] = 0;
  }
  return full + 1;
}
