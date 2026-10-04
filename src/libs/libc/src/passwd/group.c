#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

// /etc/group parsing. Same one-file-one-buffer pattern as passwd.c.
// Lines: name:passwd:gid:member,member,...
static FILE  *gr_file;
static char   gr_line[512];
static struct group gr_result;
static char  *gr_members[64];

void setgrent(void) {
  if (!gr_file) {
    gr_file = fopen("/etc/group", "r");
    if (!gr_file)
      return;
  }
  fseek(gr_file, 0, SEEK_SET);
  clearerr(gr_file);
}

void endgrent(void) {
  if (gr_file) {
    fclose(gr_file);
    gr_file = 0;
  }
}

struct group *getgrent(void) {
  if (!gr_file && (setgrent(), !gr_file))
    return 0;
  for (;;) {
    if (!fgets(gr_line, sizeof(gr_line), gr_file))
      return 0;
    char *fields[4];
    int   n = 0;
    fields[0] = gr_line;
    for (char *p = gr_line; *p && n < 4; p++) {
      if (*p == ':' || *p == '\n') {
        *p = 0;
        if (++n < 4)
          fields[n] = p + 1;
      }
    }
    if (n < 4)
      continue;
    gr_result.gr_name = fields[0];
    gr_result.gr_passwd = fields[1];
    gr_result.gr_gid = (gid_t)atoi(fields[2]);
    if (!fields[3][0]) {
      gr_members[0] = 0; // no members: empty list, not a list with ""
      gr_result.gr_mem = gr_members;
      return &gr_result;
    }
    size_t members = 0;
    for (char *p = fields[3]; members + 1 < 64; members++) {
      char *comma = strchr(p, ',');
      if (comma)
        *comma = 0;
      gr_members[members] = p;
      if (!comma)
        break;
      p = comma + 1;
    }
    gr_members[members + 1 < 64 ? members + 1 : 63] = 0;
    gr_result.gr_mem = gr_members;
    return &gr_result;
  }
}

struct group *getgrgid(gid_t gid) {
  struct group *gr;
  setgrent();
  while ((gr = getgrent()) != 0) {
    if (gr->gr_gid == gid)
      return gr;
  }
  return 0;
}

struct group *getgrnam(const char *name) {
  struct group *gr;
  setgrent();
  while ((gr = getgrent()) != 0) {
    if (!strcmp(gr->gr_name, name))
      return gr;
  }
  return 0;
}
