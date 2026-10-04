#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// /etc/services parsing. Same one-file-one-buffer pattern as passwd.c.
// Lines: name port/proto [aliases...] [# comment]. Port "http 80/tcp".
static FILE          *se_file;
static char           se_line[512];
static struct servent se_result;
static char          *se_aliases[16];

void setservent(int stayopen) {
  (void)stayopen; // unbuffered file: nothing to keep open for
  if (!se_file) {
    se_file = fopen("/etc/services", "r");
    if (!se_file)
      return;
  }
  fseek(se_file, 0, SEEK_SET);
  clearerr(se_file);
}

void endservent(void) {
  if (se_file) {
    fclose(se_file);
    se_file = 0;
  }
}

struct servent *getservent(void) {
  if (!se_file) {
    setservent(0);
    if (!se_file)
      return 0;
  }
  for (;;) {
    if (!fgets(se_line, sizeof(se_line), se_file))
      return 0;
    // Strip comments and leading space.
    char *hash = strchr(se_line, '#');
    if (hash)
      *hash = 0;
    char *name = se_line;
    while (*name == ' ' || *name == '\t')
      name++;
    if (!*name || *name == '\n')
      continue;
    // name, port/proto, then aliases.
    char *end = name;
    while (*end && *end != ' ' && *end != '\t' && *end != '\n')
      end++;
    if (!*end)
      continue;
    *end++ = 0;
    while (*end == ' ' || *end == '\t')
      end++;
    char *port = end;
    while (*end && *end != ' ' && *end != '\t' && *end != '\n')
      end++;
    if (end == port)
      continue;
    int has_end = *end != 0;
    if (has_end)
      *end++ = 0;
    char *slash = strchr(port, '/');
    if (!slash)
      continue;
    *slash++ = 0;
    se_result.s_name = name;
    se_result.s_port = htons((uint16_t)atoi(port)); // network byte order
    se_result.s_proto = slash;
    size_t aliases = 0;
    while (has_end && aliases + 1 < 16) {
      while (*end == ' ' || *end == '\t')
        end++;
      if (!*end || *end == '\n')
        break;
      se_aliases[aliases++] = end;
      while (*end && *end != ' ' && *end != '\t' && *end != '\n')
        end++;
      if (!*end)
        break;
      *end++ = 0;
      has_end = 1;
    }
    se_aliases[aliases] = 0;
    return &se_result;
  }
}

struct servent *getservbyname(const char *name, const char *proto) {
  struct servent *se;
  setservent(0);
  while ((se = getservent()) != 0) {
    if (strcmp(se->s_name, name) != 0)
      continue;
    if (proto && strcmp(se->s_proto, proto) != 0)
      continue;
    return se;
  }
  return 0;
}

struct servent *getservbyport(int port, const char *proto) {
  struct servent *se;
  setservent(0);
  while ((se = getservent()) != 0) {
    if (se->s_port != port)
      continue;
    if (proto && strcmp(se->s_proto, proto) != 0)
      continue;
    return se;
  }
  return 0;
}
