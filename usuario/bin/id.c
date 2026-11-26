/*
 * usuario/bin/id.c - quem sou eu: o usuário, o grupo e os outros grupos.
 *   id        uid=1000(joao) gid=1000(joao) grupos=1000(joao),27(sudo)
 */
#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

static void grupo(gid_t g)
{
    struct group *gr = getgrgid(g);
    printf("%u", (unsigned)g);
    if (gr) printf("(%s)", gr->gr_name);
}

int main(void)
{
    uid_t u = getuid();
    struct passwd *p = getpwuid(u);
    printf("uid=%u", (unsigned)u);
    if (p) printf("(%s)", p->pw_name);
    printf(" gid=");
    grupo(getgid());
    gid_t lista[16];
    int n = getgroups(16, lista);
    printf(" grupos=");
    for (int i = 0; i < n; i++) {
        if (i) printf(",");
        grupo(lista[i]);
    }
    printf("\n");
    return 0;
}
