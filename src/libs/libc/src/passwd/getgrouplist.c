#include <grp.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>

// Supplementary-group list for a user: primary gid first, then every group
// listing the user as a member, capped at NGROUPS_MAX (32 per <limits.h>).
// Size dance per man: when *ngroups is too small, set it to the required
// count and return -1.
int getgrouplist(const char *user, gid_t gid, gid_t *groups, int *ngroups) {
  gid_t tmp[NGROUPS_MAX];
  int   count = 0;
  tmp[count++] = gid;
  setgrent();
  struct group *gr;
  while ((gr = getgrent()) != NULL) {
    if (gr->gr_gid == gid)
      continue; // primary already recorded
    for (char **m = gr->gr_mem; m && *m; m++) {
      if (!strcmp(*m, user)) {
        if (count < NGROUPS_MAX)
          tmp[count++] = gr->gr_gid;
        break;
      }
    }
  }
  endgrent();
  if (*ngroups < count) {
    *ngroups = count;
    return -1;
  }
  for (int i = 0; i < count; i++)
    groups[i] = tmp[i];
  *ngroups = count;
  return count;
}
