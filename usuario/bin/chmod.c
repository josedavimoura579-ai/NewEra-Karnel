/*
 * usuario/bin/chmod.c - muda as permissões (só o dono ou o root).
 *
 *   chmod 644 arq      rw-r--r--   (dono lê e escreve; os outros, só leem)
 *   chmod 755 prog     rwxr-xr-x   (todos rodam; só o dono muda)
 *   chmod 600 segredo  rw-------   (só o dono)
 *   chmod +x prog      liga o x de todos
 *   chmod go-w arq     tira o w do grupo e dos outros
 *
 * Os números: cada dígito é dono / grupo / outros, somando 4 (ler),
 * 2 (escrever) e 1 (executar; numa pasta, entrar).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

static long aplicar(const char *expr, unsigned atual)
{
    if (expr[0] >= '0' && expr[0] <= '7') {
        char *fim;
        long v = strtol(expr, &fim, 8);
        return *fim || v > 07777 ? -1 : v;
    }
    unsigned modo = atual & 07777;
    for (const char *p = expr; *p; ) {                 /* [ugoa]*[+-=][rwxst]* , ... */
        unsigned quem = 0;
        for (; *p && strchr("ugoa", *p); p++)
            quem |= *p == 'u' ? 04700 : *p == 'g' ? 02070 : *p == 'o' ? 01007 : 07777;
        if (!quem) quem = 07777;
        char op = *p++;
        if (op != '+' && op != '-' && op != '=') return -1;
        unsigned bits = 0;
        for (; *p && *p != ','; p++) {
            switch (*p) {
            case 'r': bits |= 0444; break;
            case 'w': bits |= 0222; break;
            case 'x': bits |= 0111; break;
            case 's': bits |= 06000; break;
            case 't': bits |= 01000; break;
            default: return -1;
            }
        }
        bits &= quem;
        if (op == '+') modo |= bits;
        else if (op == '-') modo &= ~bits;
        else modo = (modo & ~quem) | bits;
        if (*p == ',') p++;
    }
    return modo;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: chmod MODO ARQUIVO...   (ex.: chmod 755 prog, chmod +x prog)\n"); return 1; }
    int erros = 0;
    for (int i = 2; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) < 0) { fprintf(stderr, "chmod: %s: não existe\n", argv[i]); erros++; continue; }
        long m = aplicar(argv[1], st.st_mode);
        if (m < 0) { fprintf(stderr, "chmod: modo inválido: %s\n", argv[1]); return 1; }
        if (chmod(argv[i], (mode_t)m) < 0) {
            fprintf(stderr, "chmod: %s: %s\n", argv[i],
                    errno == EPERM ? "só o dono (ou o root) muda as permissões" : strerror(errno));
            erros++;
        }
    }
    return erros != 0;
}
