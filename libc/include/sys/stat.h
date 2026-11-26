/* nlibc - sys/stat.h: tipo, tamanho, dono e permissões de um arquivo. */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#include <sys/types.h>

#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

#define S_ISUID  04000
#define S_ISGID  02000
#define S_ISVTX  01000          /* o "sticky" (/tmp) */
#define S_IRWXU  0700
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRWXG  0070
#define S_IRGRP  0040
#define S_IWGRP  0020
#define S_IXGRP  0010
#define S_IRWXO  0007
#define S_IROTH  0004
#define S_IWOTH  0002
#define S_IXOTH  0001

struct stat {
    mode_t st_mode;            /* S_IFDIR ou S_IFREG, com as permissões (0755, 0644...) */
    off_t  st_size;
    uid_t  st_uid;             /* o dono */
    gid_t  st_gid;             /* o grupo */
    unsigned st_nlink;
};

int stat(const char *caminho, struct stat *st);
int lstat(const char *caminho, struct stat *st);
int mkdir(const char *caminho, mode_t modo);
int chmod(const char *caminho, mode_t modo);
mode_t umask(mode_t mascara);
#endif
