/* nlibc - getopt (POSIX) */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

char *optarg;
int optind = 1, opterr = 1, optopt;
static int dentro = 1;                  /* posição dentro de "-abc" */

int getopt(int argc, char *const argv[], const char *opcoes)
{
    optarg = NULL;
    if (optind >= argc || !argv[optind] || argv[optind][0] != '-' || !argv[optind][1])
        return -1;
    if (!strcmp(argv[optind], "--")) { optind++; return -1; }

    int c = argv[optind][dentro++];
    const char *o = strchr(opcoes, c);
    int silencioso = opcoes[0] == ':';
    if (!o || c == ':') {
        optopt = c;
        if (opterr && !silencioso) fprintf(stderr, "%s: opcao invalida: -%c\n", argv[0], c);
        if (!argv[optind][dentro]) { optind++; dentro = 1; }
        return '?';
    }
    if (o[1] == ':') {
        if (argv[optind][dentro]) {
            optarg = &argv[optind][dentro];
        } else if (optind + 1 < argc) {
            optarg = argv[++optind];
        } else {
            optopt = c;
            optind++;
            dentro = 1;
            if (opterr && !silencioso) fprintf(stderr, "%s: a opcao -%c precisa de um valor\n", argv[0], c);
            return silencioso ? ':' : '?';
        }
        optind++;
        dentro = 1;
        return c;
    }
    if (!argv[optind][dentro]) { optind++; dentro = 1; }
    return c;
}
