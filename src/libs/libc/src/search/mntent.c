#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct mntent __mntent_entry;
static char __mntent_line[4096];

static char *next_field(char **pp) {
  char *p = *pp;
  char *start;
  while (*p == ' ' || *p == '\t') p++;
  if (*p == '\0' || *p == '\n') {
    *pp = p;
    return 0;
  }
  start = p;
  while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n') p++;
  if (*p != '\0') *p++ = '\0';
  *pp = p;
  return start;
}

FILE *setmntent(const char *file, const char *type) {
  return fopen(file, type);
}

int endmntent(FILE *fp) {
  if (fp) fclose(fp);
  return 1;
}

struct mntent *getmntent_r(FILE *fp, struct mntent *mnt, char *buf,
                           int buflen) {
  char *p;
  char *fsname;
  char *dir;
  char *type;
  char *opts;
  char *freq;
  char *passno;
  if (!fp || !mnt || !buf || buflen <= 0) return 0;
  for (;;) {
    if (!fgets(buf, buflen, fp)) return 0;
    p = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '#') continue;
    break;
  }
  p = buf;
  fsname = next_field(&p);
  dir = next_field(&p);
  type = next_field(&p);
  opts = next_field(&p);
  freq = next_field(&p);
  passno = next_field(&p);
  if (!fsname || !dir || !type || !opts) return 0;
  mnt->mnt_fsname = fsname;
  mnt->mnt_dir = dir;
  mnt->mnt_type = type;
  mnt->mnt_opts = opts;
  mnt->mnt_freq = freq ? atoi(freq) : 0;
  mnt->mnt_passno = passno ? atoi(passno) : 0;
  return mnt;
}

struct mntent *getmntent(FILE *fp) {
  return getmntent_r(fp, &__mntent_entry, __mntent_line,
                     (int)sizeof(__mntent_line));
}

int addmntent(FILE *fp, const struct mntent *mnt) {
  if (fseek(fp, 0, SEEK_END) != 0) return 1;
  if (fprintf(fp, "%s %s %s %s %d %d\n", mnt->mnt_fsname, mnt->mnt_dir,
              mnt->mnt_type, mnt->mnt_opts, mnt->mnt_freq,
              mnt->mnt_passno) < 0)
    return 1;
  return 0;
}

char *hasmntopt(const struct mntent *mnt, const char *opt) {
  const char *s = mnt->mnt_opts;
  size_t n = strlen(opt);
  while (*s != '\0') {
    const char *tok = s;
    const char *comma = strchr(s, ',');
    size_t len = comma ? (size_t)(comma - s) : strlen(s);
    if (len == n && strncmp(tok, opt, n) == 0) return (char *)tok;
    if (len > n && strncmp(tok, opt, n) == 0 && tok[n] == '=')
      return (char *)tok;
    if (!comma) break;
    s = comma + 1;
  }
  return 0;
}
