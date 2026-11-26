/*
 * usuario/bin/testelibc.c - testa a libc do NovaEra (nlibc).
 *
 * É C padrão: o mesmo arquivo compila no Linux com a glibc, e as respostas
 * esperadas aqui foram conferidas lá. No NovaEra:   testelibc
 * Sai com o número de falhas (0 = tudo certo).
 */
#include <errno.h>
#include <limits.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int total, falhas;

static void confere(int ok, const char *o_que)
{
    total++;
    if (!ok) { falhas++; printf("  FALHOU: %s\n", o_que); }
}

static void texto(const char *obtido, const char *esperado, const char *o_que)
{
    total++;
    if (strcmp(obtido, esperado)) {
        falhas++;
        printf("  FALHOU: %s\n    obtido:   [%s]\n    esperado: [%s]\n", o_que, obtido, esperado);
    }
}

static char buf[512];

/* ------------------------------- printf ---------------------------------- */

static void testa_printf(void)
{
    snprintf(buf, sizeof buf, "%d|%5d|%-5d|%05d|%+d|% d|%x|%X|%#x|%o|%#o|%u",
             42, 42, 42, 42, 42, 42, 255, 255, 255, 8, 8, 4000000000u);
    texto(buf, "42|   42|42   |00042|+42| 42|ff|FF|0xff|10|010|4000000000", "printf inteiros");
    snprintf(buf, sizeof buf, "%ld|%lld|%llu|%zu|%hhd|%hd|%.3d|%.0d|%c|%s|%.2s|%6s|%-6s|%%",
             -9223372036854775807L - 1, -5LL, 18446744073709551615ULL, (size_t)7,
             (char)300, (short)70000, 7, 0, 'x', "ola", "ola", "ab", "ab");
    texto(buf, "-9223372036854775808|-5|18446744073709551615|7|44|4464|007||x|ola|ol|    ab|ab    |%",
          "printf tamanhos e textos");
    snprintf(buf, sizeof buf, "%f|%.2f|%.0f|%.0f|%.0f|%.0f|%e|%.3e|%E", 3.14159, 2.675, 0.5, 1.5,
             2.5, -0.4, 123456.789, 0.000123456, 1e-10);
    texto(buf, "3.141590|2.67|0|2|2|-0|1.234568e+05|1.235e-04|1.000000E-10", "printf %f e %e");
    snprintf(buf, sizeof buf, "%g|%g|%g|%g|%g|%g|%G|%#g|%g|%g", 100000.0, 1000000.0, 0.0001,
             0.00001, 123.456, 0.0, 1e-20, 1.0, -0.0, 1e100);
    texto(buf, "100000|1e+06|0.0001|1e-05|123.456|0|1E-20|1.00000|-0|1e+100", "printf %g");
    snprintf(buf, sizeof buf, "%.17g|%.17g|%.17g|%.17g|%.17g", 0.1, 1.0 / 3, 1e-300,
             5e-324, 1.7976931348623157e308);
    texto(buf, "0.10000000000000001|0.33333333333333331|1e-300|"
               "4.9406564584124654e-324|1.7976931348623157e+308", "printf %.17g (exato)");
    snprintf(buf, sizeof buf, "%10.4f|%-10.2e|%+.1f|%08.2f|%.30f", 3.14159265, 12345.678, 2.25,
             -3.5, 0.1);
    texto(buf, "    3.1416|1.23e+04  |+2.2|-0003.50|0.100000000000000005551115123126",
          "printf largura e precisão");
    snprintf(buf, sizeof buf, "%f|%e|%g|%f", 1.0 / 0.0, -1.0 / 0.0, 1e308 * 10, 1e22);
    texto(buf, "inf|-inf|inf|10000000000000000000000.000000", "printf infinito e grandes");
    int n = snprintf(buf, 5, "%s", "abcdefgh");
    confere(n == 8 && !strcmp(buf, "abcd"), "snprintf corta e conta");
}

/* ------------------------------- strtod ---------------------------------- */

static int bits_iguais(double a, double b) { return !memcmp(&a, &b, sizeof a); }

static void testa_strtod(void)
{
    const char *txt[] = { "0.1", "1e23", "9007199254740993", "2.2250738585072011e-308",
                          "4.9406564584124654e-324", "2.4703282292062328e-324",
                          "1.7976931348623157e308", "0x1.8p1", "123.456e-2", "-0.0",
                          "3.141592653589793238462643383279", "1e-400", "1e400" };
    double val[] = { 0.1, 1e23, 9007199254740992.0, 2.2250738585072011e-308,
                     4.9406564584124654e-324, 4.9406564584124654e-324,
                     1.7976931348623157e308, 3.0, 1.23456, -0.0,
                     3.141592653589793, 0.0, 1.0 / 0.0 };
    for (unsigned i = 0; i < sizeof txt / sizeof txt[0]; i++) {
        snprintf(buf, sizeof buf, "strtod(\"%s\")", txt[i]);
        confere(bits_iguais(strtod(txt[i], NULL), val[i]), buf);
    }
    /* ida e volta: %.17g -> strtod devolve exatamente o mesmo double */
    double x = 1.0;
    int ok = 1;
    for (int i = 0; i < 300; i++) {
        x = x * 1.37 + 1e-7 / (i + 1);
        if (i % 50 == 0) x = 1.0 / x;
        snprintf(buf, sizeof buf, "%.17g", x);
        if (!bits_iguais(strtod(buf, NULL), x)) ok = 0;
    }
    confere(ok, "ida e volta %.17g <-> strtod (300 números)");
    char *fim;
    double v = strtod("  -12.5e1xyz", &fim);
    confere(v == -125.0 && !strcmp(fim, "xyz"), "strtod para no lugar certo");
}

/* --------------------------- strtol, sscanf ------------------------------- */

static void testa_conversoes(void)
{
    char *fim;
    confere(strtol("  -123abc", &fim, 10) == -123 && *fim == 'a', "strtol base 10");
    confere(strtol("0x1F", NULL, 0) == 31 && strtol("017", NULL, 0) == 15 &&
            strtol("z", NULL, 36) == 35, "strtol bases 0 e 36");
    errno = 0;
    confere(strtol("99999999999999999999", NULL, 10) == LONG_MAX && errno == ERANGE,
            "strtol estoura com ERANGE");
    confere(strtoull("18446744073709551615", NULL, 10) == ULLONG_MAX, "strtoull máximo");
    confere(atoi("42") == 42 && abs(-5) == 5, "atoi e abs");

    int a; char s[16]; double d; float f; unsigned u;
    int r = sscanf("  17 palavra 2.5 _1.25 ff", "%d %15s %lf _%f %x", &a, s, &d, &f, &u);
    confere(r == 5 && a == 17 && !strcmp(s, "palavra") && d == 2.5 && f == 1.25f && u == 255,
            "sscanf %d %s %lf _%f %x");
    confere(sscanf("abc", "%d", &a) == 0, "sscanf sem número");
}

/* ------------------------------- string.h --------------------------------- */

static void testa_strings(void)
{
    char t[32] = "um,dois;;tres";
    confere(strlen(t) == 13 && !strcmp(strchr(t, 'd'), "dois;;tres") &&
            !strcmp(strrchr(t, ';'), ";tres") && !strcmp(strstr(t, "tr"), "tres"),
            "strlen strchr strrchr strstr");
    char *p1 = strtok(t, ",;"), *p2 = strtok(NULL, ",;"), *p3 = strtok(NULL, ",;");
    confere(!strcmp(p1, "um") && !strcmp(p2, "dois") && !strcmp(p3, "tres") &&
            strtok(NULL, ",;") == NULL, "strtok");
    char m[16] = "abcdefgh";
    memmove(m + 2, m, 5);
    confere(!memcmp(m, "ababcdeh", 8), "memmove com sobreposição");
    confere(strspn("aab12", "ab") == 3 && strcspn("aab12", "12") == 3 &&
            strcmp("abc", "abd") < 0 && strncmp("abcx", "abcy", 3) == 0, "strspn strcspn strcmp");
    char *dup = strdup("copia");
    confere(dup && !strcmp(dup, "copia"), "strdup");
    free(dup);
}

/* --------------------------- malloc e qsort ------------------------------- */

typedef struct { int chave, ordem; } par_t;
static int por_chave(const void *a, const void *b)
{
    return ((const par_t *)a)->chave - ((const par_t *)b)->chave;
}

static void testa_memoria(void)
{
    enum { N = 300 };
    unsigned char *blocos[N];
    int ok = 1;
    for (int i = 0; i < N; i++) {
        blocos[i] = malloc((size_t)(i * 37 % 4000 + 1));
        if (!blocos[i]) ok = 0;
        else memset(blocos[i], i & 0xFF, (size_t)(i * 37 % 4000 + 1));
    }
    for (int i = 0; i < N; i += 2) {
        blocos[i] = realloc(blocos[i], (size_t)(i * 37 % 4000 + 5000));
        if (!blocos[i] || blocos[i][0] != (i & 0xFF)) ok = 0;
    }
    for (int i = 0; i < N; i++) {
        if (blocos[i][i * 37 % 4000] != (i & 0xFF)) ok = 0;
        free(blocos[i]);
    }
    int *zeros = calloc(1000, sizeof(int));
    for (int i = 0; zeros && i < 1000; i++) if (zeros[i]) ok = 0;
    free(zeros);
    confere(ok, "malloc, realloc, calloc e free (300 blocos)");

    par_t v[200];
    for (int i = 0; i < 200; i++) { v[i].chave = (i * 7) % 10; v[i].ordem = i; }
    qsort(v, 200, sizeof v[0], por_chave);
    ok = 1;
    for (int i = 1; i < 200; i++)
        if (v[i - 1].chave > v[i].chave ||
            (v[i - 1].chave == v[i].chave && v[i - 1].ordem > v[i].ordem)) ok = 0;
    confere(ok, "qsort ordena e mantém a ordem dos iguais");
    int chaves[] = { 1, 3, 5, 7, 9 }, procura = 7;
    int *achou = bsearch(&procura, chaves, 5, sizeof(int), por_chave);
    confere(achou && *achou == 7, "bsearch");
}

/* ------------------------------- arquivos --------------------------------- */

static void testa_arquivos(void)
{
    FILE *f = fopen("/tmp/teste.txt", "w");
    confere(f != NULL, "fopen escrita");
    if (!f) return;
    fprintf(f, "linha %d\n", 1);
    fputs("segunda 2.5\n", f);
    fclose(f);
    f = fopen("/tmp/teste.txt", "a");
    fputc('Z', f);
    fclose(f);

    f = fopen("/tmp/teste.txt", "r");
    char l[64];
    double d = 0;
    int ok = f && fgets(l, sizeof l, f) && !strcmp(l, "linha 1\n") &&
             fscanf(f, "segunda %lf\n", &d) == 1 && d == 2.5 && fgetc(f) == 'Z' &&
             fgetc(f) == EOF && feof(f);
    if (f) fclose(f);
    confere(ok, "fprintf, fgets, fscanf, modo anexar, EOF");
    confere(remove("/tmp/teste.txt") == 0 && fopen("/tmp/teste.txt", "r") == NULL &&
            errno == ENOENT, "remove e ENOENT");

    char modelo[] = "/tmp/tmpXXXXXX";
    int fd = mkstemp(modelo);
    confere(fd >= 0 && strcmp(modelo, "/tmp/tmpXXXXXX") && write(fd, "abc", 3) == 3 &&
            close(fd) == 0 && unlink(modelo) == 0, "mkstemp, write, close, unlink");
}

/* -------------------------------- getopt ---------------------------------- */

static void testa_getopt(void)
{
    char *args[] = { "prog", "-a", "-b", "valor", "-cz", "resto", NULL };
    int a = 0, c = 0, z = 0, op;
    char *b = NULL;
    optind = 1;
    while ((op = getopt(6, args, "ab:cz")) != -1) {
        if (op == 'a') a = 1;
        if (op == 'b') b = optarg;
        if (op == 'c') c = 1;
        if (op == 'z') z = 1;
    }
    confere(a && c && z && b && !strcmp(b, "valor") && optind == 5 &&
            !strcmp(args[optind], "resto"), "getopt");
}

/* ------------------------------- processos -------------------------------- */

static double contas(long n)
{
    double x = 0.5;
    for (long i = 0; i < n; i++) x = x * 1.0000001 + 0.25 / (double)(i + 1);
    return x;
}

static void testa_processos(char *eu)
{
    int fds[2];
    confere(pipe(fds) == 0, "pipe");
    posix_spawn_file_actions_t acoes;
    posix_spawn_file_actions_init(&acoes);
    posix_spawn_file_actions_adddup2(&acoes, fds[1], 1);
    posix_spawn_file_actions_addclose(&acoes, fds[0]);
    char *args[] = { "echo", "ola", "pelo", "pipe", NULL };
    pid_t pid;
    int r = posix_spawnp(&pid, "echo", &acoes, NULL, args, NULL);
    posix_spawn_file_actions_destroy(&acoes);
    close(fds[1]);
    char lido[64];
    ssize_t n, tot = 0;
    while ((n = read(fds[0], lido + tot, sizeof lido - 1 - (size_t)tot)) > 0) tot += n;
    lido[tot] = '\0';
    close(fds[0]);
    int status = -1;
    confere(r == 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0, "posix_spawnp + waitpid");
    texto(lido, "ola pelo pipe\n", "a saída do filho chega pelo pipe");

    /* Vários processos fazendo contas com vírgula ao mesmo tempo: só dá
     * certo se o núcleo salvar a FPU/SSE de cada um na troca. */
    double esperado = contas(3000000);
    char esperado_txt[40];
    snprintf(esperado_txt, sizeof esperado_txt, "%.17g", esperado);
    pid_t filhos[3];
    char *conta_args[] = { eu, "--contas", esperado_txt, NULL };
    int criados = 0;
    for (int i = 0; i < 3; i++)
        if (posix_spawnp(&filhos[i], eu, NULL, NULL, conta_args, NULL) == 0) criados++;
    double meu = contas(3000000);
    int certos = 0;
    for (int i = 0; i < criados; i++) {
        pid_t quem = wait(&status);
        if (quem > 0 && WEXITSTATUS(status) == 0) certos++;
    }
    confere(criados == 3 && certos == 3 && bits_iguais(meu, esperado),
            "4 processos com ponto flutuante ao mesmo tempo (FPU salva na troca)");
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "--contas")) {
        /* filho do teste de FPU: a conta tem de dar o que o pai calculou */
        return bits_iguais(contas(3000000), strtod(argv[2], NULL)) ? 0 : 1;
    }
    printf("Testando a libc do NovaEra...\n");
    testa_printf();
    testa_strtod();
    testa_conversoes();
    testa_strings();
    testa_memoria();
    testa_arquivos();
    testa_getopt();
    testa_processos(argv[0]);
    printf("%s: %d de %d testes passaram\n", falhas ? "FALHAS" : "OK", total - falhas, total);
    return falhas;
}
