/*
 * usuario/bin/chown.c - muda o dono (e o grupo) de um arquivo. Só o root.
 *
 *   chown joao arq          o dono passa a ser o joao
 *   chown joao:users arq    o dono e o grupo
 *   chown :users arq        só o grupo
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: chown USUARIO[:GRUPO] ARQUIVO...\n"); return 1; }
    char dono[64];
    strncpy(dono, argv[1], sizeof dono - 1);
    dono[sizeof dono - 1] = '\0';
    char *grupo = strchr(dono, ':');
    if (grupo) *grupo++ = '\0';
    uid_t u = (uid_t)-1;
    gid_t g = (gid_t)-1;
    if (dono[0]) {
        struct passwd *p = getpwnam(dono);
        if (p) u = p->pw_uid;
        else if (dono[0] >= '0' && dono[0] <= '9') u = (uid_t)atoi(dono);
        else { fprintf(stderr, "chown: usuário desconhecido: %s\n", dono); return 1; }
    }
    if (grupo && grupo[0]) {
        struct group *gr = getgrnam(grupo);
        if (gr) g = gr->gr_gid;
        else if (grupo[0] >= '0' && grupo[0] <= '9') g = (gid_t)atoi(grupo);
        else { fprintf(stderr, "chown: grupo desconhecido: %s\n", grupo); return 1; }
    }
    int erros = 0;
    for (int i = 2; i < argc; i++) {
        if (chown(argv[i], u, g) < 0) {
            fprintf(stderr, "chown: %s: %s\n", argv[i],
                    errno == EPERM ? "só o root muda o dono" : strerror(errno));
            erros++;
        }
    }
    return erros != 0;
}
