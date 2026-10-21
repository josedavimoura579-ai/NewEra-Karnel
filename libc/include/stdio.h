/* nlibc - stdio.h
 * Um FILE do NovaEra é um servidor (terminal, pipes ou arquivos) mais um
 * canal, com um buffer na frente. stdin e stdout são a entrada e a saída
 * padrão do processo; stderr vai sempre para o terminal. */
#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>

#define EOF      (-1)
#define BUFSIZ   1024
#define FILENAME_MAX 64
#define FOPEN_MAX    32
#define L_tmpnam 64
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

typedef struct _FILE FILE;
typedef long fpos_t;

extern FILE *stdin, *stdout, *stderr;

FILE  *fopen(const char *caminho, const char *modo);
FILE  *fdopen(int fd, const char *modo);
int    fclose(FILE *f);
int    fflush(FILE *f);
size_t fread(void *buf, size_t tam, size_t n, FILE *f);
size_t fwrite(const void *buf, size_t tam, size_t n, FILE *f);
int    fgetc(FILE *f);
int    getc(FILE *f);
int    getchar(void);
int    ungetc(int c, FILE *f);
char  *fgets(char *s, int n, FILE *f);
int    fputc(int c, FILE *f);
int    putc(int c, FILE *f);
int    putchar(int c);
int    fputs(const char *s, FILE *f);
int    puts(const char *s);
int    feof(FILE *f);
int    ferror(FILE *f);
void   clearerr(FILE *f);
int    fileno(FILE *f);
int    setvbuf(FILE *f, char *buf, int modo, size_t tam);
void   setbuf(FILE *f, char *buf);
void   perror(const char *s);
int    remove(const char *caminho);

int    printf(const char *fmt, ...);
int    fprintf(FILE *f, const char *fmt, ...);
int    sprintf(char *s, const char *fmt, ...);
int    snprintf(char *s, size_t n, const char *fmt, ...);
int    vprintf(const char *fmt, va_list ap);
int    vfprintf(FILE *f, const char *fmt, va_list ap);
int    vsprintf(char *s, const char *fmt, va_list ap);
int    vsnprintf(char *s, size_t n, const char *fmt, va_list ap);

int    scanf(const char *fmt, ...);
int    fscanf(FILE *f, const char *fmt, ...);
int    sscanf(const char *s, const char *fmt, ...);
int    vfscanf(FILE *f, const char *fmt, va_list ap);
int    vsscanf(const char *s, const char *fmt, va_list ap);
#endif
