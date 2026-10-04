#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "internal/syscall.h"

// Reentrant passwd/group lookup. Same colon-separated parsing style as
// passwd.c/group.c, but each call opens the database itself, scans for the
// key, and packs the result strings into the caller's buffer - no static
// storage, so concurrent uses do not clobber each other. NOTE: the
// non-reentrant getpwent/getgrent family already lives in passwd.c/group.c
// and is deliberately NOT redefined here (that would be a duplicate
// symbol). Conventions: 0 on success, ERANGE when buflen is too small,
// ENOENT when the key is absent (with *result set to NULL).

static int pack_passwd(struct passwd *pwd, char *buf, size_t buflen,
                       struct passwd **result, char *name, char *passwd,
                       uid_t uid, gid_t gid, char *gecos, char *dir,
                       char *shell) {
  size_t need = strlen(name) + 1 + strlen(passwd) + 1 + strlen(gecos) + 1 +
                strlen(dir) + 1 + strlen(shell) + 1;
  if (need > buflen) {
    *result = NULL;
    return ERANGE;
  }
  char *p = buf;
  pwd->pw_name = p;
  p += strlen(name) + 1;
  memcpy(pwd->pw_name, name, p - pwd->pw_name);
  pwd->pw_passwd = p;
  p += strlen(passwd) + 1;
  memcpy(pwd->pw_passwd, passwd, p - pwd->pw_passwd);
  pwd->pw_uid = uid;
  pwd->pw_gid = gid;
  pwd->pw_gecos = p;
  p += strlen(gecos) + 1;
  memcpy(pwd->pw_gecos, gecos, p - pwd->pw_gecos);
  pwd->pw_dir = p;
  p += strlen(dir) + 1;
  memcpy(pwd->pw_dir, dir, p - pwd->pw_dir);
  pwd->pw_shell = p;
  memcpy(pwd->pw_shell, shell, strlen(shell) + 1);
  *result = pwd;
  return 0;
}

// Parse one /etc/passwd line in place. Returns 0 with fields filled, -1
// for malformed lines (caller skips them, like passwd.c).
static int split_passwd(char *line, char **name, char **passwd, char **uid,
                        char **gid, char **gecos, char **dir, char **shell) {
  char *fields[7];
  int   n = 0;
  fields[0] = line;
  for (char *p = line; *p && n < 7; p++) {
    if (*p == ':' || *p == '\n') {
      *p = 0;
      if (++n < 7)
        fields[n] = p + 1;
    }
  }
  if (n < 7)
    return -1;
  *name = fields[0];
  *passwd = fields[1];
  *uid = fields[2];
  *gid = fields[3];
  *gecos = fields[4];
  *dir = fields[5];
  *shell = fields[6];
  return 0;
}

static int scan_passwd(int by_uid, uid_t uid, const char *name,
                       struct passwd *pwd, char *buf, size_t buflen,
                       struct passwd **result) {
  FILE *f = fopen("/etc/passwd", "r");
  if (!f) {
    *result = NULL;
    return ENOENT;
  }
  char line[512];
  int  rc = ENOENT;
  *result = NULL;
  while (fgets(line, sizeof(line), f)) {
    char *pw_name, *pw_passwd, *pw_uid, *pw_gid, *pw_gecos, *pw_dir, *pw_shell;
    if (split_passwd(line, &pw_name, &pw_passwd, &pw_uid, &pw_gid, &pw_gecos,
                     &pw_dir, &pw_shell) < 0)
      continue;
    int match = by_uid ? (uid_t)atoi(pw_uid) == uid : !strcmp(pw_name, name);
    if (!match)
      continue;
    rc = pack_passwd(pwd, buf, buflen, result, pw_name, pw_passwd,
                     (uid_t)atoi(pw_uid), (gid_t)atoi(pw_gid), pw_gecos,
                     pw_dir, pw_shell);
    break; // found: success or ERANGE, either way stop scanning
  }
  fclose(f);
  return rc;
}

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen,
               struct passwd **result) {
  return scan_passwd(1, uid, NULL, pwd, buf, buflen, result);
}

int getpwnam_r(const char *name, struct passwd *pwd, char *buf, size_t buflen,
               struct passwd **result) {
  return scan_passwd(0, 0, name, pwd, buf, buflen, result);
}

// Parse one /etc/group line in place. Members are split on commas into the
// caller-supplied pointer slots; returns member count, or -1 malformed.
static int split_group(char *line, char **name, char **passwd, char **gid,
                       char *members[], int max_members) {
  char *fields[4];
  int   n = 0;
  fields[0] = line;
  for (char *p = line; *p && n < 4; p++) {
    if (*p == ':' || *p == '\n') {
      *p = 0;
      if (++n < 4)
        fields[n] = p + 1;
    }
  }
  if (n < 4)
    return -1;
  *name = fields[0];
  *passwd = fields[1];
  *gid = fields[2];
  if (!fields[3][0])
    return 0;
  int count = 0;
  for (char *p = fields[3]; count < max_members; count++) {
    char *comma = strchr(p, ',');
    if (comma)
      *comma = 0;
    members[count] = p;
    if (!comma)
      return count + 1;
    p = comma + 1;
  }
  return count;
}

static int scan_group(int by_gid, gid_t gid, const char *name,
                      struct group *grp, char *buf, size_t buflen,
                      struct group **result) {
  FILE *f = fopen("/etc/group", "r");
  if (!f) {
    *result = NULL;
    return ENOENT;
  }
  char  line[512];
  char *tmp_members[64];
  int   rc = ENOENT;
  *result = NULL;
  while (fgets(line, sizeof(line), f)) {
    char *gr_name, *gr_passwd, *gr_gid;
    int   nmemb =
        split_group(line, &gr_name, &gr_passwd, &gr_gid, tmp_members, 64);
    if (nmemb < 0)
      continue;
    int match = by_gid ? (gid_t)atoi(gr_gid) == gid : !strcmp(gr_name, name);
    if (!match)
      continue;
    size_t need = strlen(gr_name) + 1 + strlen(gr_passwd) + 1 +
                  (size_t)(nmemb + 1) * sizeof(char *);
    for (int i = 0; i < nmemb; i++)
      need += strlen(tmp_members[i]) + 1;
    if (need > buflen) {
      rc = ERANGE;
      break;
    }
    char *p = buf;
    grp->gr_name = p;
    size_t l = strlen(gr_name) + 1;
    memcpy(p, gr_name, l);
    p += l;
    grp->gr_passwd = p;
    l = strlen(gr_passwd) + 1;
    memcpy(p, gr_passwd, l);
    p += l;
    grp->gr_gid = (gid_t)atoi(gr_gid);
    grp->gr_mem = (char **)p;
    p += (size_t)(nmemb + 1) * sizeof(char *);
    for (int i = 0; i < nmemb; i++) {
      grp->gr_mem[i] = p;
      l = strlen(tmp_members[i]) + 1;
      memcpy(p, tmp_members[i], l);
      p += l;
    }
    grp->gr_mem[nmemb] = NULL;
    *result = grp;
    rc = 0;
    break;
  }
  fclose(f);
  return rc;
}

int getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t buflen,
               struct group **result) {
  return scan_group(1, gid, NULL, grp, buf, buflen, result);
}

int getgrnam_r(const char *name, struct group *grp, char *buf, size_t buflen,
               struct group **result) {
  return scan_group(0, 0, name, grp, buf, buflen, result);
}

// Stream-driven readers on a caller-supplied FILE. Static result storage
// like getpwent/getgrent (overwritten by the next call); malformed lines
// are skipped, matching passwd.c/group.c.
struct passwd *fgetpwent(FILE *stream) {
  static char           line[512];
  static struct passwd  result;
  if (!stream || !fgets(line, sizeof(line), stream))
    return NULL;
  char *name, *passwd, *uid, *gid, *gecos, *dir, *shell;
  if (split_passwd(line, &name, &passwd, &uid, &gid, &gecos, &dir, &shell) <
      0)
    return fgetpwent(stream); // skip malformed, recurse to next line
  result.pw_name = name;
  result.pw_passwd = passwd;
  result.pw_uid = (uid_t)atoi(uid);
  result.pw_gid = (gid_t)atoi(gid);
  result.pw_gecos = gecos;
  result.pw_dir = dir;
  result.pw_shell = shell;
  return &result;
}

struct group *fgetgrent(FILE *stream) {
  static char          line[512];
  static struct group  result;
  static char         *members[64];
  if (!stream || !fgets(line, sizeof(line), stream))
    return NULL;
  char *name, *passwd, *gid;
  int   nmemb = split_group(line, &name, &passwd, &gid, members, 64);
  if (nmemb < 0)
    return fgetgrent(stream); // skip malformed, recurse to next line
  result.gr_name = name;
  result.gr_passwd = passwd;
  result.gr_gid = (gid_t)atoi(gid);
  members[nmemb < 64 ? nmemb : 63] = NULL;
  result.gr_mem = members;
  return &result;
}

int putpwent(const struct passwd *pwd, FILE *stream) {
  if (!pwd || !stream)
    return -1;
  if (fprintf(stream, "%s:%s:%u:%u:%s:%s:%s\n", pwd->pw_name, pwd->pw_passwd,
              (unsigned)pwd->pw_uid, (unsigned)pwd->pw_gid, pwd->pw_gecos,
              pwd->pw_dir, pwd->pw_shell) < 0)
    return -1;
  return 0;
}

int putgrent(const struct group *grp, FILE *stream) {
  if (!grp || !stream)
    return -1;
  if (fprintf(stream, "%s:%s:%u:", grp->gr_name, grp->gr_passwd,
              (unsigned)grp->gr_gid) < 0)
    return -1;
  for (char **m = grp->gr_mem; m && *m; m++) {
    if (fprintf(stream, "%s%s", m == grp->gr_mem ? "" : ",", *m) < 0)
      return -1;
  }
  if (fprintf(stream, "\n") < 0)
    return -1;
  return 0;
}

// Supplementary-group init: primary gid plus every group listing the user.
// Capped at NGROUPS_MAX (32 per <limits.h>). setgroups has no libc wrapper
// yet, so the SYS_setgroups call is issued directly here.
int initgroups(const char *user, gid_t gid) {
  gid_t groups[NGROUPS_MAX];
  int   ngroups = 0;
  groups[ngroups++] = gid;
  setgrent();
  struct group *gr;
  while ((gr = getgrent()) != NULL) {
    if (gr->gr_gid == gid)
      continue; // primary already recorded
    for (char **m = gr->gr_mem; m && *m; m++) {
      if (!strcmp(*m, user)) {
        if (ngroups < NGROUPS_MAX)
          groups[ngroups++] = gr->gr_gid;
        break;
      }
    }
  }
  endgrent();
  return (int)__cavos_ret((unsigned long)__syscall2(SYS_setgroups, ngroups,
                                                   (long)groups));
}
