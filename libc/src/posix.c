/*
 * nlibc - libc/src/posix.c
 * Descritores de arquivo e o pedaço do POSIX que o cproc usa: pipe,
 * posix_spawnp, waitpid, mkstemp, unlink...
 *
 * Um fd é um servidor + um canal. 0 e 1 são a entrada e a saída padrão
 * que o núcleo guarda para o processo; 2 é sempre o terminal.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "interno.h"

nfd_t __fds[MAX_FDS];

static int e_tubo(const nfd_t *f) { return f->servidor == PID_TUBOS; }

int __fd_novo(u32 servidor, u32 canal, u32 ponta, int dono)
{
    for (int i = 0; i < MAX_FDS; i++) {
        if (__fds[i].usado) continue;
        __fds[i].usado    = 1;
        __fds[i].servidor = servidor;
        __fds[i].canal    = canal & ~CANAL_DONO;
        __fds[i].ponta    = ponta;
        __fds[i].dono     = dono;
        return i;
    }
    errno = EMFILE;
    return -1;
}

nfd_t *__fd(int fd)
{
    if (fd < 0 || fd >= MAX_FDS || !__fds[fd].usado) { errno = EBADF; return 0; }
    return &__fds[fd];
}

/* Chamado antes de main(): 0, 1 e 2. Um pipe que veio com CANAL_DONO é
 * uma referência deste processo: exit() a devolve. */
void __fds_iniciar(void)
{
    ambiente_t *a = ambiente();
    __fds[0].usado = 1;
    __fds[0].servidor = a->entrada;
    __fds[0].canal = a->entrada_canal & ~CANAL_DONO;
    __fds[0].ponta = TUBO_LEITURA;
    __fds[0].dono  = (a->entrada_canal & CANAL_DONO) != 0;
    __fds[1].usado = 1;
    __fds[1].servidor = a->saida;
    __fds[1].canal = a->saida_canal & ~CANAL_DONO;
    __fds[1].ponta = TUBO_ESCRITA;
    __fds[1].dono  = (a->saida_canal & CANAL_DONO) != 0;
    __fds[2].usado = 1;
    __fds[2].servidor = PID_TERMINAL;
}

int close(int fd)
{
    nfd_t *f = __fd(fd);
    if (!f) return -1;
    if (f->dono && e_tubo(f)) tubo_fechar(f->canal, f->ponta);
    else if (f->dono && f->servidor == PID_ARQUIVOS) arq_fechar(f->canal);
    f->usado = 0;
    return 0;
}

void __fds_fechar_todos(void)
{
    for (int i = 0; i < MAX_FDS; i++) if (__fds[i].usado) close(i);
}

ssize_t read(int fd, void *buf, size_t n)
{
    nfd_t *f = __fd(fd);
    if (!f) return -1;
    if (n > 0x7FFFFFFF) n = 0x7FFFFFFF;
    long r = ler_de(f->servidor, f->canal, buf, (u32)n);
    if (r < 0) { errno = __erro_c(r); return -1; }
    return r;
}

ssize_t write(int fd, const void *buf, size_t n)
{
    nfd_t *f = __fd(fd);
    if (!f) return -1;
    long r = escreve_em(f->servidor, f->canal, buf, n, COR(COR_BRANCO));
    if (r < 0) { errno = e_tubo(f) ? EPIPE : EIO; return -1; }
    return r;
}

int pipe(int fds[2])
{
    long canal = tubo_criar();
    if (canal < 0) { errno = EMFILE; return -1; }
    fds[0] = __fd_novo(PID_TUBOS, (u32)canal, TUBO_LEITURA, 1);
    fds[1] = __fd_novo(PID_TUBOS, (u32)canal, TUBO_ESCRITA, 1);
    if (fds[0] < 0 || fds[1] < 0) return -1;
    return 0;
}

/* Uma cópia do fd: mais uma referência, se for pipe. */
static int duplicar(const nfd_t *f, int destino)
{
    if (e_tubo(f)) tubo_abrir(f->canal, f->ponta);
    if (destino < 0) return __fd_novo(f->servidor, f->canal, f->ponta, e_tubo(f) || f->dono);
    __fds[destino] = *f;
    __fds[destino].dono = e_tubo(f) || f->dono;
    return destino;
}

int dup(int fd)
{
    nfd_t *f = __fd(fd);
    return f ? duplicar(f, -1) : -1;
}

int dup2(int fd, int novo)
{
    nfd_t *f = __fd(fd);
    if (!f || novo < 0 || novo >= MAX_FDS) { errno = EBADF; return -1; }
    if (fd == novo) return novo;
    nfd_t copia = *f;
    if (__fds[novo].usado) close(novo);
    return duplicar(&copia, novo);
}

int open(const char *caminho, int flags, ...)
{
    u32 tam;
    long tipo = arq_info(caminho, &tam);
    if (tipo == ARQ_TIPO_PASTA) { errno = EISDIR; return -1; }
    if (tipo < 0 && !(flags & O_CREAT)) { errno = ENOENT; return -1; }
    if (tipo >= 0 && (flags & O_CREAT) && (flags & O_EXCL)) { errno = EEXIST; return -1; }
    u32 modo = ARQ_MODO_LER;
    if ((flags & 3) != O_RDONLY)
        modo = (flags & O_APPEND) ? ARQ_MODO_ANEXAR
             : (flags & O_TRUNC) || tipo < 0 ? ARQ_MODO_ESCREVER : ARQ_MODO_ANEXAR;
    long canal = arq_abrir(caminho, modo);
    if (canal < 0) { errno = __erro_c(canal); return -1; }
    return __fd_novo(PID_ARQUIVOS, (u32)canal, 0, 1);
}

int unlink(const char *caminho)
{
    if (arq_info(caminho, 0) == ARQ_TIPO_PASTA) { errno = EISDIR; return -1; }
    long r = arq_apagar(caminho);
    if (r < 0) { errno = __erro_c(r); return -1; }
    return 0;
}

int rmdir(const char *caminho)
{
    long r = arq_apagar(caminho);
    if (r < 0) { errno = r == ERRO_INVALIDO ? ENOTEMPTY : __erro_c(r); return -1; }
    return 0;
}

int remove(const char *caminho) { return arq_apagar(caminho) < 0 ? (errno = ENOENT, -1) : 0; }

int access(const char *caminho, int modo)
{
    (void)modo;
    if (arq_info(caminho, 0) < 0) { errno = ENOENT; return -1; }
    return 0;
}

int chdir(const char *caminho)
{
    char abs[CAMINHO_MAX];
    if (caminho_absoluto(caminho, abs) < 0 || arq_info(abs, 0) != ARQ_TIPO_PASTA) {
        errno = ENOENT;
        return -1;
    }
    sys_pasta(abs);
    str_copia(ambiente()->pasta, abs, CAMINHO_MAX);
    return 0;
}

char *getcwd(char *buf, size_t tam)
{
    const char *p = ambiente()->pasta;
    if (strlen(p) + 1 > tam) { errno = ERANGE; return NULL; }
    strcpy(buf, p);
    return buf;
}

int isatty(int fd)
{
    nfd_t *f = __fd(fd);
    return f && f->servidor == PID_TERMINAL;
}

pid_t getpid(void) { return (pid_t)sys_pid(); }

int kill(pid_t pid, int sinal) { (void)pid; (void)sinal; errno = ENOSYS; return -1; }

static char *ambiente_vazio[1];
char **environ = ambiente_vazio;

int fcntl(int fd, int cmd, ...)
{
    nfd_t *f = __fd(fd);
    if (!f) { errno = EBADF; return -1; }
    switch (cmd) {
    case F_GETFD: return FD_CLOEXEC;
    case F_SETFD: return 0;
    case F_GETFL: return O_RDWR;
    case F_SETFL: return 0;
    }
    errno = EINVAL;
    return -1;
}

/* O NovaEra não tem links simbólicos (nem /proc/self/exe). */
ssize_t readlink(const char *caminho, char *buf, size_t tam)
{
    (void)caminho; (void)buf; (void)tam;
    errno = EINVAL;
    return -1;
}
sighandler_t signal(int sinal, sighandler_t f) { (void)sinal; (void)f; return SIG_DFL; }

int mkstemp(char *modelo)
{
    size_t n = strlen(modelo);
    if (n < 6 || strcmp(modelo + n - 6, "XXXXXX")) { errno = EINVAL; return -1; }
    static unsigned contador;
    for (int tentativa = 0; tentativa < 1000; tentativa++) {
        unsigned v = (unsigned)sys_pid() * 7919u + contador++ * 104729u + sys_tempo();
        for (int i = 0; i < 6; i++) { modelo[n - 6 + i] = "abcdefghijklmnopqrstuvwxyz0123456789"[v % 36]; v /= 36; }
        if (arq_info(modelo, 0) >= 0) continue;
        long canal = arq_abrir(modelo, ARQ_MODO_ESCREVER);
        if (canal < 0) { errno = __erro_c(canal); return -1; }
        return __fd_novo(PID_ARQUIVOS, (u32)canal, 0, 1);
    }
    errno = EEXIST;
    return -1;
}

/* --------------------------------- Processos ------------------------------ */

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *a) { a->n = 0; return 0; }
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *a) { a->n = 0; return 0; }

static int acao(posix_spawn_file_actions_t *a, int tipo, int fd, int novo)
{
    if (a->n == POSIX_SPAWN_MAX_ACOES) return ENOMEM;
    a->acoes[a->n].tipo = tipo;
    a->acoes[a->n].fd   = fd;
    a->acoes[a->n].novo = novo;
    a->n++;
    return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *a, int fd, int novo)
{
    return acao(a, 1, fd, novo);
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *a, int fd)
{
    return acao(a, 2, fd, -1);
}

/* O filho recebe só a entrada (0) e a saída (1) padrão. Para cada uma que
 * for pipe, ele ganha uma referência própria (CANAL_DONO): o exit() dele
 * a devolve. */
int posix_spawn(pid_t *pid, const char *caminho, const posix_spawn_file_actions_t *acoes,
                const posix_spawnattr_t *atrib, char *const argv[], char *const envp[])
{
    (void)atrib; (void)envp;
    nfd_t es[2] = { __fds[0], __fds[1] };
    for (int i = 0; acoes && i < acoes->n; i++) {
        const __acao_spawn *a = &acoes->acoes[i];
        if (a->tipo == 1 && (a->novo == 0 || a->novo == 1)) {
            nfd_t *f = __fd(a->fd);
            if (!f) return EBADF;
            es[a->novo] = *f;
        }
    }
    u32 canal[2];
    for (int i = 0; i < 2; i++) {
        canal[i] = es[i].canal;
        if (es[i].servidor == PID_TUBOS) {
            tubo_abrir(es[i].canal, es[i].ponta);
            canal[i] |= CANAL_DONO;
        }
    }
    long r = executar(caminho, (char **)argv, es[0].servidor, canal[0], es[1].servidor, canal[1]);
    if (r < 0) {
        for (int i = 0; i < 2; i++)
            if (es[i].servidor == PID_TUBOS) tubo_fechar(es[i].canal, es[i].ponta);
        return __erro_c(r);
    }
    if (pid) *pid = (pid_t)r;
    return 0;
}

int posix_spawnp(pid_t *pid, const char *arquivo, const posix_spawn_file_actions_t *acoes,
                 const posix_spawnattr_t *atrib, char *const argv[], char *const envp[])
{
    char caminho[CAMINHO_MAX];
    if (strchr(arquivo, '/')) {
        if (caminho_absoluto(arquivo, caminho) < 0) return ENOENT;
    } else {
        if (strlen(arquivo) + 6 > sizeof(caminho)) return ENOENT;
        strcpy(caminho, "/bin/");
        strcat(caminho, arquivo);
    }
    return posix_spawn(pid, caminho, acoes, atrib, argv, envp);
}

pid_t waitpid(pid_t pid, int *status, int opcoes)
{
    (void)opcoes;
    long r;
    pid_t quem = pid;
    if (pid <= 0) {                             /* qualquer filho */
        r = sys_esperar(0);
        if (r < 0) { errno = ECHILD; return -1; }
        quem = (pid_t)(r >> 16);
        r &= 0xFFFF;
    } else {
        r = sys_esperar((u32)pid);
        if (r < 0) { errno = ECHILD; return -1; }
    }
    if (status) *status = (int)((r & 0xFF) << 8);
    return quem;
}

pid_t wait(int *status) { return waitpid(-1, status, 0); }
