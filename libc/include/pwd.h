/* nlibc - pwd.h: as contas do /etc/passwd. */
#ifndef _PWD_H
#define _PWD_H
#include <sys/types.h>
struct passwd {
    char *pw_name;
    char *pw_passwd;           /* sempre "x": a senha mora no /etc/sombra */
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_gecos;
    char *pw_dir;              /* a pasta pessoal */
    char *pw_shell;
};
struct passwd *getpwnam(const char *nome);
struct passwd *getpwuid(uid_t uid);
#endif
