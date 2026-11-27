/*
 * nlibc - libc/src/contas.c
 * Quem sou eu (getuid...) e as contas do sistema (getpwnam, getgrgid...),
 * lidas do /etc/passwd e do /etc/group a cada chamada.
 */
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include "interno.h"

static identidade_t eu(void)
{
    identidade_t id;
    memset(&id, 0, sizeof id);
    sys_identidade(0, &id);
    return id;
}

uid_t getuid(void)  { return eu().uid; }
uid_t geteuid(void) { return eu().uid; }
gid_t getgid(void)  { return eu().gid; }
gid_t getegid(void) { return eu().gid; }

int getgroups(int n, gid_t lista[])
{
    identidade_t id = eu();
    int total = (int)id.n + 1;
    if (n == 0) return total;
    if (n < total) { errno = EINVAL; return -1; }
    lista[0] = id.gid;
    for (u32 i = 0; i < id.n; i++) lista[i + 1] = id.grupos[i];
    return total;
}

/* Só o root muda de usuário (e o servidor de contas, com a senha). */
int setuid(uid_t uid)
{
    identidade_t id = eu();
    if (id.uid != UID_ROOT) { errno = EPERM; return -1; }
    id.uid = uid;
    return sys_identidade_mudar(0, &id) < 0 ? (errno = EPERM, -1) : 0;
}

int setgid(gid_t gid)
{
    identidade_t id = eu();
    if (id.uid != UID_ROOT) { errno = EPERM; return -1; }
    id.gid = gid;
    return sys_identidade_mudar(0, &id) < 0 ? (errno = EPERM, -1) : 0;
}

static mode_t mascara = 022;
mode_t umask(mode_t m) { mode_t a = mascara; mascara = m & 0777; return a; }

/* Uma linha "a:b:c" partida em campos (no próprio buffer). */
static int partir(char *linha, char **campos, int max)
{
    int n = 0;
    char *p = linha;
    while (n < max) {
        campos[n++] = p;
        while (*p && *p != ':' && *p != '\n') p++;
        if (*p != ':') { *p = '\0'; break; }
        *p++ = '\0';
    }
    return n;
}

static struct passwd pw;
static char pw_linha[256];

static struct passwd *procura_pw(const char *nome, uid_t uid)
{
    FILE *f = fopen("/etc/passwd", "r");
    if (!f) return NULL;
    while (fgets(pw_linha, sizeof pw_linha, f)) {
        char *c[7];
        if (pw_linha[0] == '#' || partir(pw_linha, c, 7) < 7) continue;
        if (nome ? strcmp(c[0], nome) : (uid_t)atoi(c[2]) != uid) continue;
        pw.pw_name = c[0];   pw.pw_passwd = c[1];
        pw.pw_uid  = (uid_t)atoi(c[2]);
        pw.pw_gid  = (gid_t)atoi(c[3]);
        pw.pw_gecos = c[4];  pw.pw_dir = c[5];  pw.pw_shell = c[6];
        fclose(f);
        return &pw;
    }
    fclose(f);
    return NULL;
}

struct passwd *getpwnam(const char *nome) { return procura_pw(nome, 0); }
struct passwd *getpwuid(uid_t uid)        { return procura_pw(NULL, uid); }

char *getlogin(void)
{
    struct passwd *p = getpwuid(getuid());
    return p ? p->pw_name : NULL;
}

static struct group gr;
static char  gr_linha[512];
static char *gr_membros[64];

static struct group *procura_gr(const char *nome, gid_t gid)
{
    FILE *f = fopen("/etc/group", "r");
    if (!f) return NULL;
    while (fgets(gr_linha, sizeof gr_linha, f)) {
        char *c[4] = { NULL, NULL, NULL, NULL };
        int nc = gr_linha[0] == '#' ? 0 : partir(gr_linha, c, 4);
        if (nc < 3) continue;
        if (nome ? strcmp(c[0], nome) : (gid_t)atoi(c[2]) != gid) continue;
        gr.gr_name = c[0];
        gr.gr_passwd = c[1];
        gr.gr_gid = (gid_t)atoi(c[2]);
        int n = 0;
        char *p = nc >= 4 ? c[3] : NULL;
        if (p) {
            while (*p && n < 63) {
                gr_membros[n++] = p;
                while (*p && *p != ',') p++;
                if (*p) *p++ = '\0';
            }
        }
        gr_membros[n] = NULL;
        gr.gr_mem = gr_membros;
        fclose(f);
        return &gr;
    }
    fclose(f);
    return NULL;
}

struct group *getgrnam(const char *nome) { return procura_gr(nome, 0); }
struct group *getgrgid(gid_t gid)        { return procura_gr(NULL, gid); }
