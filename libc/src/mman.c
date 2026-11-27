/*
 * nlibc - libc/src/mman.c
 * mmap, munmap e a memória compartilhada (shm_open, shm_unlink, ftruncate).
 *
 * Um objeto de memória compartilhada vira um descritor de arquivo com o
 * servidor SERVIDOR_SHM e o id do objeto no canal: dá para passá-lo ao
 * ftruncate (o tamanho) e ao mmap (as páginas), como no POSIX.
 */
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "interno.h"

void *mmap(void *endereco, size_t tam, int prot, int flags, int fd, off_t desloc)
{
    (void)endereco;
    if (!tam || desloc || (flags & MAP_FIXED)) { errno = EINVAL; return MAP_FAILED; }
    mmap_t m;
    m.tam = tam;
    m.prot = (prot & PROT_WRITE) ? (PROT_LER | PROT_ESCREVER) : PROT_LER;
    m.reservado = 0;
    m.objeto = 0;
    if (!(flags & MAP_ANONYMOUS)) {
        nfd_t *f = __fd(fd);
        if (!f) return MAP_FAILED;
        if (f->servidor != SERVIDOR_SHM) { errno = ENODEV; return MAP_FAILED; }   /* arquivo: ainda não */
        m.objeto = f->canal;
    }
    long r = sys_mmap(&m);
    if (r < 0) { errno = r == ERRO_INVALIDO ? EINVAL : __erro_c(r); return MAP_FAILED; }
    return (void *)r;
}

int munmap(void *endereco, size_t tam)
{
    long r = sys_munmap((u64)endereco, tam);
    if (r < 0) { errno = EINVAL; return -1; }
    return 0;
}

int mprotect(void *endereco, size_t tam, int prot) { (void)endereco; (void)tam; (void)prot; return 0; }
int msync(void *endereco, size_t tam, int flags)   { (void)endereco; (void)tam; (void)flags; return 0; }

int shm_open(const char *nome, int oflag, mode_t modo)
{
    shm_pedido_t p;
    p.nome = (u64)nome;
    p.criar = (oflag & O_CREAT) ? ((oflag & O_EXCL) ? 2 : 1) : 0;
    p.modo = modo & 0777;
    long id = sys_shm_abrir(&p);
    if (id < 0) {
        errno = id == ERRO_NAO_ACHADO ? ENOENT : id == ERRO_EXISTE ? EEXIST
              : id == ERRO_SEM_PERMISSAO ? EACCES : id == ERRO_INVALIDO ? EINVAL : __erro_c(id);
        return -1;
    }
    return __fd_novo(SERVIDOR_SHM, (u32)id, 0, 0);
}

int shm_unlink(const char *nome)
{
    long r = sys_shm_apagar(nome);
    if (r < 0) { errno = r == ERRO_NAO_ACHADO ? ENOENT : __erro_c(r); return -1; }
    return 0;
}

/* ftruncate: só para a memória compartilhada (o tamanho do objeto). */
int ftruncate(int fd, off_t tam)
{
    nfd_t *f = __fd(fd);
    if (!f) return -1;
    if (f->servidor != SERVIDOR_SHM) { errno = EINVAL; return -1; }
    long r = sys_shm_tamanho(f->canal, (u64)tam);
    if (r < 0) { errno = r == ERRO_SEM_ESPACO ? ENOSPC : __erro_c(r); return -1; }
    return 0;
}
