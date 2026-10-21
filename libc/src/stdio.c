/*
 * nlibc - libc/src/stdio.c
 * FILE = um fd (servidor + canal) com um buffer na frente.
 *   stdout: buffer de linha no terminal, buffer cheio em pipes e arquivos
 *   stderr: sem buffer, sempre no terminal
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "interno.h"

#define MAX_ARQUIVOS FOPEN_MAX

static struct _FILE arquivos[MAX_ARQUIVOS];
FILE *stdin  = &arquivos[0];
FILE *stdout = &arquivos[1];
FILE *stderr = &arquivos[2];

void __fds_iniciar(void);

static void preparar(FILE *f, int fd, int ler, int escrever)
{
    memset(f, 0, sizeof(*f));
    f->usado     = 1;
    f->fd        = fd;
    f->ler       = ler;
    f->escrever  = escrever;
    f->devolvido = -1;
    f->modo_buffer = isatty(fd) ? _IOLBF : _IOFBF;
}

/* Chamado por _start (libc/crt0.asm) antes de main(). */
void __libc_iniciar(void)
{
    __fds_iniciar();
    preparar(stdin, 0, 1, 0);
    preparar(stdout, 1, 0, 1);
    preparar(stderr, 2, 0, 1);
    stderr->modo_buffer = _IONBF;
}

/* ----------------------------- Abrir e fechar ----------------------------- */

FILE *fdopen(int fd, const char *modo)
{
    if (!__fd(fd)) return NULL;
    for (int i = 3; i < MAX_ARQUIVOS; i++) {
        if (arquivos[i].usado) continue;
        int ler = modo[0] == 'r' || strchr(modo, '+');
        int escrever = modo[0] != 'r' || strchr(modo, '+');
        preparar(&arquivos[i], fd, ler, escrever);
        return &arquivos[i];
    }
    errno = EMFILE;
    return NULL;
}

FILE *fopen(const char *caminho, const char *modo)
{
    int flags;
    switch (modo[0]) {
    case 'r': flags = strchr(modo, '+') ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (strchr(modo, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': flags = (strchr(modo, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default:  errno = EINVAL; return NULL;
    }
    int fd = open(caminho, flags);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, modo);
    if (!f) close(fd);
    return f;
}

static int esvaziar(FILE *f)
{
    if (!f->escrevendo || !f->n) { f->escrevendo = 0; return 0; }
    int ok = write(f->fd, f->buf, (size_t)f->n) == f->n;
    f->n = 0;
    f->escrevendo = 0;
    if (!ok) { f->erro = 1; return EOF; }
    return 0;
}

int fflush(FILE *f)
{
    if (!f) { __stdio_esvaziar_todos(); return 0; }
    return esvaziar(f);
}

void __stdio_esvaziar_todos(void)
{
    for (int i = 0; i < MAX_ARQUIVOS; i++) if (arquivos[i].usado) esvaziar(&arquivos[i]);
}

int fclose(FILE *f)
{
    int r = esvaziar(f);
    if (f->fd > 2) close(f->fd);
    f->usado = 0;
    return r;
}

int setvbuf(FILE *f, char *buf, int modo, size_t tam)
{
    (void)buf; (void)tam;
    esvaziar(f);
    f->modo_buffer = modo;
    return 0;
}

void setbuf(FILE *f, char *buf) { setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ); }

/* ---------------------------------- Escrita ------------------------------- */

static int pede_esvaziar(FILE *f, char c)
{
    return f->n == NLIBC_BUF || f->modo_buffer == _IONBF ||
           (f->modo_buffer == _IOLBF && c == '\n');
}

int fputc(int c, FILE *f)
{
    if (!f->escrever) { f->erro = 1; errno = EBADF; return EOF; }
    if (!f->escrevendo) { f->pos = f->n = 0; f->escrevendo = 1; }
    f->buf[f->n++] = (char)c;
    if (pede_esvaziar(f, (char)c) && esvaziar(f) == EOF) return EOF;
    return (unsigned char)c;
}

int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c)       { return fputc(c, stdout); }

size_t fwrite(const void *buf, size_t tam, size_t n, FILE *f)
{
    const char *p = buf;
    size_t total = tam * n;
    for (size_t i = 0; i < total; i++)
        if (fputc(p[i], f) == EOF) return i / (tam ? tam : 1);
    return n;
}

int fputs(const char *s, FILE *f)
{
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

/* ---------------------------------- Leitura ------------------------------- */

int fgetc(FILE *f)
{
    if (f->devolvido >= 0) { int c = f->devolvido; f->devolvido = -1; return c; }
    if (!f->ler) { f->erro = 1; errno = EBADF; return EOF; }
    if (f->escrevendo) esvaziar(f);
    if (f->pos >= f->n) {
        if (f == stdin) fflush(stdout);         /* o prompt aparece antes de esperar */
        ssize_t r = read(f->fd, f->buf, NLIBC_BUF);
        if (r <= 0) { if (r < 0) f->erro = 1; else f->eof = 1; return EOF; }
        f->pos = 0;
        f->n = (int)r;
    }
    return (unsigned char)f->buf[f->pos++];
}

int getc(FILE *f)  { return fgetc(f); }
int getchar(void)  { return fgetc(stdin); }

int ungetc(int c, FILE *f)
{
    if (c == EOF || f->devolvido >= 0) return EOF;
    f->devolvido = (unsigned char)c;
    f->eof = 0;
    return f->devolvido;
}

char *fgets(char *s, int n, FILE *f)
{
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = '\0';
    return s;
}

size_t fread(void *buf, size_t tam, size_t n, FILE *f)
{
    char *p = buf;
    size_t total = tam * n, i = 0;
    for (; i < total; i++) {
        int c = fgetc(f);
        if (c == EOF) break;
        p[i] = (char)c;
    }
    return tam ? i / tam : 0;
}

int  feof(FILE *f)     { return f->eof; }
int  ferror(FILE *f)   { return f->erro; }
void clearerr(FILE *f) { f->eof = f->erro = 0; }
int  fileno(FILE *f)   { return f->fd; }

/* ------------------------------ printf e scanf ----------------------------- */

static void para_arquivo(void *ctx, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) fputc(s[i], (FILE *)ctx);
}

int vfprintf(FILE *f, const char *fmt, va_list ap) { return __formatar(para_arquivo, f, fmt, ap); }
int vprintf(const char *fmt, va_list ap)            { return vfprintf(stdout, fmt, ap); }

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

typedef struct { char *s; size_t max, n; } texto_t;

static void para_texto(void *ctx, const char *s, size_t n)
{
    texto_t *t = ctx;
    for (size_t i = 0; i < n; i++, t->n++)
        if (t->n + 1 < t->max) t->s[t->n] = s[i];
}

int vsnprintf(char *s, size_t max, const char *fmt, va_list ap)
{
    texto_t t = { s, max, 0 };
    int r = __formatar(para_texto, &t, fmt, ap);
    if (max) s[t.n < max ? t.n : max - 1] = '\0';
    return r;
}

int vsprintf(char *s, const char *fmt, va_list ap) { return vsnprintf(s, (size_t)-1 / 2, fmt, ap); }

int snprintf(char *s, size_t max, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, max, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}

static int le_arquivo(void *ctx)             { return fgetc((FILE *)ctx); }
static void devolve_arquivo(void *ctx, int c) { ungetc(c, (FILE *)ctx); }

int vfscanf(FILE *f, const char *fmt, va_list ap) { return __varrer(le_arquivo, devolve_arquivo, f, fmt, ap); }

int fscanf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(f, fmt, ap);
    va_end(ap);
    return r;
}

int scanf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(stdin, fmt, ap);
    va_end(ap);
    return r;
}

typedef struct { const char *s; } leitura_t;
static int le_texto(void *ctx) { leitura_t *l = ctx; return *l->s ? (unsigned char)*l->s++ : EOF; }
static void devolve_texto(void *ctx, int c) { leitura_t *l = ctx; if (c != EOF) l->s--; }

int vsscanf(const char *s, const char *fmt, va_list ap)
{
    leitura_t l = { s };
    return __varrer(le_texto, devolve_texto, &l, fmt, ap);
}

int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}
