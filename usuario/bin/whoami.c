/* usuario/bin/whoami.c - o nome do usuário de quem pergunta. */
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>

int main(void)
{
    struct passwd *p = getpwuid(getuid());
    if (p) printf("%s\n", p->pw_name);
    else printf("%u\n", (unsigned)getuid());
    return 0;
}
