/* nlibc - sys/mman.h: mmap (memória direto do núcleo) e memória
 * compartilhada entre programas (shm_open). */
#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H
#include <sys/types.h>

#define PROT_NONE     0
#define PROT_READ     1
#define PROT_WRITE    2
#define PROT_EXEC     4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10          /* (não há: o núcleo escolhe o endereço) */
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)
#define MS_SYNC       4
#define MS_ASYNC      1

void *mmap(void *endereco, size_t tam, int prot, int flags, int fd, off_t desloc);
int   munmap(void *endereco, size_t tam);
int   mprotect(void *endereco, size_t tam, int prot);
int   msync(void *endereco, size_t tam, int flags);
int   shm_open(const char *nome, int oflag, mode_t modo);
int   shm_unlink(const char *nome);
#endif
