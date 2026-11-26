/* nlibc - grp.h: os grupos do /etc/group. */
#ifndef _GRP_H
#define _GRP_H
#include <sys/types.h>
struct group {
    char  *gr_name;
    char  *gr_passwd;
    gid_t  gr_gid;
    char **gr_mem;             /* os membros, terminado em NULL */
};
struct group *getgrnam(const char *nome);
struct group *getgrgid(gid_t gid);
#endif
