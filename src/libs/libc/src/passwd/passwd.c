#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

// /etc/passwd parsing. One open file, one reusable result buffer: like
// musl this is NOT thread-safe (no threads exist) and the returned pointer
// is overwritten by the next call. Lines: name:passwd:uid:gid:gecos:dir:shell
static FILE   *pw_file;
static char    pw_line[512];
static struct passwd pw_result;
static char   *pw_fields[7];

static int pw_open(void) {
  if (!pw_file) {
    pw_file = fopen("/etc/passwd", "r");
    if (!pw_file)
      return -1;
  }
  return 0;
}

void setpwent(void) {
  if (pw_open() < 0)
    return;
  fseek(pw_file, 0, SEEK_SET);
  clearerr(pw_file);
}

void endpwent(void) {
  if (pw_file) {
    fclose(pw_file);
    pw_file = 0;
  }
}

struct passwd *getpwent(void) {
  if (pw_open() < 0)
    return 0;
  for (;;) {
    if (!fgets(pw_line, sizeof(pw_line), pw_file))
      return 0;
    // Split into 7 fields; skip malformed lines like musl does.
    int fields = 0;
    pw_fields[0] = pw_line;
    for (char *p = pw_line; *p && fields < 7; p++) {
      if (*p == ':' || *p == '\n') {
        *p = 0;
        if (++fields < 7)
          pw_fields[fields] = p + 1;
      }
    }
    if (fields < 7)
      continue;
    pw_result.pw_name = pw_fields[0];
    pw_result.pw_passwd = pw_fields[1];
    pw_result.pw_uid = (uid_t)atoi(pw_fields[2]);
    pw_result.pw_gid = (gid_t)atoi(pw_fields[3]);
    pw_result.pw_gecos = pw_fields[4];
    pw_result.pw_dir = pw_fields[5];
    pw_result.pw_shell = pw_fields[6];
    return &pw_result;
  }
}

struct passwd *getpwuid(uid_t uid) {
  struct passwd *pw;
  setpwent();
  while ((pw = getpwent()) != 0) {
    if (pw->pw_uid == uid)
      return pw;
  }
  return 0;
}

struct passwd *getpwnam(const char *name) {
  struct passwd *pw;
  setpwent();
  while ((pw = getpwent()) != 0) {
    if (!strcmp(pw->pw_name, name))
      return pw;
  }
  return 0;
}
