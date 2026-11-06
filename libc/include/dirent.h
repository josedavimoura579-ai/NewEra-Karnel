/* nlibc - dirent.h: ler as pastas (opendir, readdir, closedir). */
#ifndef _DIRENT_H
#define _DIRENT_H
#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
    unsigned char d_type;      /* DT_DIR ou DT_REG */
    char          d_name[64];
};

typedef struct __dir DIR;

DIR           *opendir(const char *caminho);
struct dirent *readdir(DIR *d);
void           rewinddir(DIR *d);
int            closedir(DIR *d);
#endif
