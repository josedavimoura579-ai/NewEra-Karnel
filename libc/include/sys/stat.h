/* nlibc - sys/stat.h: o tipo e o tamanho de um arquivo; criar pastas. */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#include <sys/types.h>

#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

struct stat {
    mode_t st_mode;            /* S_IFDIR ou S_IFREG, com as permissões 0755/0644 */
    off_t  st_size;
};

int stat(const char *caminho, struct stat *st);
int mkdir(const char *caminho, mode_t modo);
#endif
