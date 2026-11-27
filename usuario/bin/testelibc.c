/*
 * usuario/bin/testelibc.c - testa a libc do NovaEra (nlibc).
 *
 * É C padrão: o mesmo arquivo compila no Linux com a glibc, e as respostas
 * esperadas aqui foram conferidas lá. No NovaEra:   testelibc
 * Sai com o número de falhas (0 = tudo certo).
 */
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <signal.h>
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

/* -------------------------------- Sinais ---------------------------------- */

#include <sched.h>
static void sched_yield_ou_nada(void) { sched_yield(); }

static int    recebidos, ultimo;
static double no_tratador;
int *nulo;                              /* global (não static): o compilador não sabe que é 0 */

static void tratador(int s)
{
    recebidos++;
    ultimo = s;
    no_tratador = contas(1000);         /* mexe na FPU: o núcleo tem de devolvê-la */
}

static void tratador_segv(int s)
{
    (void)s;
    _exit(42);                          /* voltar refaria a mesma leitura */
}

static int filho(char *eu, const char *modo)
{
    char *args[] = { eu, (char *)modo, NULL };
    pid_t pid;
    if (posix_spawnp(&pid, eu, NULL, NULL, args, NULL) != 0) return -1;
    return pid;
}

static int espera(pid_t pid)
{
    int status = -1;
    if (pid <= 0 || waitpid(pid, &status, 0) != pid) return -1;
    return status;
}

static void testa_sinais(char *eu)
{
    double antes = contas(2000);
    confere(signal(SIGUSR1, tratador) == SIG_DFL, "signal devolve o tratador antigo (SIG_DFL)");
    confere(raise(SIGUSR1) == 0 && recebidos == 1 && ultimo == SIGUSR1, "raise chama o tratador");
    confere(bits_iguais(antes, contas(2000)) && no_tratador > 0, "a FPU volta intacta depois do tratador");
    confere(signal(SIGUSR1, tratador) == tratador, "signal devolve o tratador instalado");

    sigset_t m;
    sigemptyset(&m);
    sigaddset(&m, SIGUSR1);
    sigprocmask(SIG_BLOCK, &m, NULL);
    raise(SIGUSR1);
    confere(recebidos == 1, "sinal bloqueado fica pendente");
    sigprocmask(SIG_UNBLOCK, &m, NULL);
    confere(recebidos == 2, "desbloqueado, ele chega");

    signal(SIGUSR2, SIG_IGN);
    confere(raise(SIGUSR2) == 0, "sinal ignorado não encerra");
    signal(SIGCHLD, SIG_DFL);

    int fds[2];
    signal(SIGPIPE, SIG_IGN);
    pipe(fds);
    close(fds[0]);
    errno = 0;
    confere(write(fds[1], "x", 1) == -1 && errno == EPIPE, "pipe sem leitor: EPIPE (com SIGPIPE ignorado)");
    close(fds[1]);
    signal(SIGPIPE, SIG_DFL);

    int st = espera(filho(eu, "--pipe"));
    confere(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGPIPE, "pipe sem leitor: SIGPIPE encerra");
    st = espera(filho(eu, "--falha"));
    confere(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV, "ler o endereço 0: SIGSEGV");
    st = espera(filho(eu, "--trata-falha"));
    confere(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 42, "tratador de SIGSEGV roda");
    st = espera(filho(eu, "--abort"));
    confere(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT, "abort: SIGABRT");

    pid_t p = filho(eu, "--laco");
    confere(p > 0 && kill(p, 0) == 0, "kill(pid, 0): o filho existe");
    confere(p > 0 && getpgid(p) == getpgrp(), "o filho herda o grupo");
    confere(kill(p, SIGTERM) == 0, "kill SIGTERM");
    st = espera(p);
    confere(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM, "WIFSIGNALED e WTERMSIG");
    confere(kill(p, SIGTERM) == -1 && errno == ESRCH, "kill num processo que acabou: ESRCH");

    /* Sinais no meio de uma conta: o filho é interrompido em qualquer ponto
     * e a conta tem de dar o mesmo resultado (a FPU é salva no sinal). */
    p = filho(eu, "--fpu-sinal");
    for (int i = 0; i < 5; i++) { usleep(30000); kill(p, SIGUSR1); }
    st = espera(p);
    confere(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0, "sinais no meio de contas com vírgula");
    signal(SIGUSR1, SIG_DFL);
}

/* -------------------------------- Threads --------------------------------- */

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static long contador;

static void *somar(void *arg)
{
    for (int i = 0; i < 20000; i++) {
        pthread_mutex_lock(&trava);
        contador++;
        pthread_mutex_unlock(&trava);
        if (i % 1000 == 0) sched_yield_ou_nada();
    }
    return arg;
}

static void *dobro(void *arg) { return (void *)((long)arg * 2); }

/* produtor e consumidor: uma fila de 4 lugares */
static pthread_mutex_t fila_trava = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  tem_item = PTHREAD_COND_INITIALIZER, tem_lugar = PTHREAD_COND_INITIALIZER;
static int fila[4], fila_n, fila_ini, consumidos_soma;

static void *produtor(void *arg)
{
    (void)arg;
    for (int i = 1; i <= 100; i++) {
        pthread_mutex_lock(&fila_trava);
        while (fila_n == 4) pthread_cond_wait(&tem_lugar, &fila_trava);
        fila[(fila_ini + fila_n++) % 4] = i;
        pthread_cond_signal(&tem_item);
        pthread_mutex_unlock(&fila_trava);
    }
    return NULL;
}

static void *consumidor(void *arg)
{
    (void)arg;
    for (int i = 0; i < 100; i++) {
        pthread_mutex_lock(&fila_trava);
        while (fila_n == 0) pthread_cond_wait(&tem_item, &fila_trava);
        consumidos_soma += fila[fila_ini];
        fila_ini = (fila_ini + 1) % 4;
        fila_n--;
        pthread_cond_signal(&tem_lugar);
        pthread_mutex_unlock(&fila_trava);
    }
    return NULL;
}

static void *sair_7(void *arg) { (void)arg; exit(7); }
static void *falhar(void *arg) { (void)arg; return (void *)(long)*nulo; }
static void *contas_thread(void *arg) { (void)arg; return contas(200000) == contas(200000) ? (void *)1 : NULL; }

static void testa_threads(char *eu)
{
    pthread_t t[4];
    int criadas = 0;
    for (int i = 0; i < 4; i++) if (pthread_create(&t[i], NULL, somar, (void *)(long)i) == 0) criadas++;
    long soma_ret = 0;
    for (int i = 0; i < criadas; i++) { void *r; pthread_join(t[i], &r); soma_ret += (long)r; }
    confere(criadas == 4 && contador == 80000, "4 threads somando com mutex: 80000");
    confere(soma_ret == 0 + 1 + 2 + 3, "pthread_join devolve o valor de cada thread");

    void *r = NULL;
    pthread_t d;
    confere(pthread_create(&d, NULL, dobro, (void *)21) == 0 && pthread_join(d, &r) == 0 && (long)r == 42,
            "pthread_create + join: 21 * 2 = 42");

    pthread_t p, c;
    pthread_create(&p, NULL, produtor, NULL);
    pthread_create(&c, NULL, consumidor, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    confere(consumidos_soma == 5050, "produtor e consumidor com pthread_cond: 1 + ... + 100");

    int ok = 1;                                 /* muitas threads, uma depois da outra */
    for (int i = 0; i < 100 && ok; i++) {
        if (pthread_create(&d, NULL, dobro, (void *)(long)i) != 0 || pthread_join(d, &r) != 0 || (long)r != 2 * i) ok = 0;
    }
    confere(ok, "100 threads criadas e esperadas em sequência");

    pthread_t f[3];
    int certas = 0;
    for (int i = 0; i < 3; i++) pthread_create(&f[i], NULL, contas_thread, NULL);
    for (int i = 0; i < 3; i++) { pthread_join(f[i], &r); certas += r != NULL; }
    confere(certas == 3, "threads fazendo contas com vírgula ao mesmo tempo");

    int st = espera(filho(eu, "--thread-exit"));
    confere(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 7, "exit() numa thread encerra o programa todo");
    st = espera(filho(eu, "--thread-falha"));
    confere(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV, "falha numa thread: SIGSEGV no programa todo");
    st = espera(filho(eu, "--principal-sai"));
    confere(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0, "a principal sai com pthread_exit; a outra termina");
}

/* --------------------------- mmap e memória compartilhada ------------------ */

typedef struct {
    pthread_mutex_t trava;            /* um mutex DENTRO da memória compartilhada */
    long contador;
    char recado[64];
} comum_t;

static comum_t *abrir_comum(int criar)
{
    int fd = shm_open("/testelibc", criar ? (O_CREAT | O_RDWR) : O_RDWR, 0600);
    if (fd < 0) return NULL;
    if (criar && ftruncate(fd, sizeof(comum_t)) < 0) { close(fd); return NULL; }
    void *p = mmap(NULL, sizeof(comum_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return p == MAP_FAILED ? NULL : p;
}

static void somar_comum(comum_t *c)
{
    for (int i = 0; i < 20000; i++) {
        pthread_mutex_lock(&c->trava);
        c->contador++;
        pthread_mutex_unlock(&c->trava);
        if (i % 1000 == 0) sched_yield_ou_nada();
    }
}

static void testa_mmap(char *eu)
{
    size_t tam = 1024 * 1024;
    unsigned char *m = mmap(NULL, tam, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int ok = m != MAP_FAILED;
    for (size_t i = 0; ok && i < tam; i += 4096) if (m[i] != 0) ok = 0;   /* vem zerada */
    for (size_t i = 0; ok && i < tam; i++) m[i] = (unsigned char)(i * 7);
    for (size_t i = 0; ok && i < tam; i++) if (m[i] != (unsigned char)(i * 7)) ok = 0;
    confere(ok, "mmap de 1 MiB: zerado, escreve e lê");
    confere(ok && munmap(m, tam) == 0, "munmap");

    int st = espera(filho(eu, "--munmap"));
    confere(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV, "usar depois do munmap: SIGSEGV");
    st = espera(filho(eu, "--so-leitura"));
    confere(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV, "escrever em PROT_READ: SIGSEGV");

    shm_unlink("/testelibc");
    comum_t *c = abrir_comum(1);
    confere(c != NULL, "shm_open + ftruncate + mmap");
    if (!c) return;
    pthread_mutex_init(&c->trava, NULL);
    strcpy(c->recado, "ola, filho");
    pid_t p = filho(eu, "--shm");
    somar_comum(c);                         /* o filho soma junto, ao mesmo tempo */
    st = espera(p);
    confere(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0, "o filho leu o recado do pai");
    texto(c->recado, "oi, pai", "o pai lê o que o filho escreveu");
    confere(c->contador == 40000, "pai e filho somando com um mutex compartilhado: 40000");
    confere(shm_unlink("/testelibc") == 0 && shm_open("/testelibc", O_RDWR, 0) == -1 && errno == ENOENT,
            "shm_unlink: o nome some");
    munmap(c, sizeof(comum_t));
}

/* -------------------------------- Sockets ---------------------------------- */

static struct sockaddr_un endereco(const char *nome)
{
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, nome);
    return a;
}

static int ler_tudo(int fd, char *buf, int n)
{
    int tot = 0, r;
    while (tot < n && (r = (int)read(fd, buf + tot, (size_t)(n - tot))) > 0) tot += r;
    return tot;
}

#define GRANDE 200000

static int cliente(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = endereco("/tmp/teste.sock");
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) < 0) return 1;
    if (write(fd, "ping", 4) != 4) return 2;
    char r[8] = {0};
    if (ler_tudo(fd, r, 4) != 4 || strcmp(r, "pong")) return 3;
    static char dados[GRANDE];
    for (int i = 0; i < GRANDE; i++) dados[i] = (char)(i * 13);
    if (write(fd, dados, GRANDE) != GRANDE) return 4;
    close(fd);
    return 0;
}

static void testa_sockets(char *eu)
{
    int par[2];
    char b[16] = {0};
    confere(socketpair(AF_UNIX, SOCK_STREAM, 0, par) == 0, "socketpair");
    struct pollfd pf = { par[1], POLLIN, 0 };
    confere(poll(&pf, 1, 50) == 0, "poll sem dados: espera e volta 0");
    write(par[0], "abc", 3);
    confere(poll(&pf, 1, 50) == 1 && (pf.revents & POLLIN), "poll com dados: POLLIN");
    confere(read(par[1], b, sizeof b) == 3 && !memcmp(b, "abc", 3), "socketpair: de um lado para o outro");
    write(par[1], "xyz", 3);
    confere(read(par[0], b, sizeof b) == 3 && !memcmp(b, "xyz", 3), "socketpair: e de volta");
    close(par[1]);
    confere(read(par[0], b, sizeof b) == 0, "o outro lado fechou: read devolve 0");
    signal(SIGPIPE, SIG_IGN);
    errno = 0;
    confere(write(par[0], "q", 1) == -1 && errno == EPIPE, "escrever sem ninguém do outro lado: EPIPE");
    signal(SIGPIPE, SIG_DFL);
    close(par[0]);

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = endereco("/tmp/teste.sock");
    confere(s >= 0 && bind(s, (struct sockaddr *)&a, sizeof a) == 0 && listen(s, 4) == 0,
            "socket + bind + listen");
    int s2 = socket(AF_UNIX, SOCK_STREAM, 0);
    confere(bind(s2, (struct sockaddr *)&a, sizeof a) == -1 && errno == EADDRINUSE, "o mesmo nome duas vezes: EADDRINUSE");
    close(s2);
    struct sockaddr_un nada = endereco("/tmp/ninguem.sock");
    s2 = socket(AF_UNIX, SOCK_STREAM, 0);
    confere(connect(s2, (struct sockaddr *)&nada, sizeof nada) == -1 && errno == ECONNREFUSED,
            "conectar onde ninguém ouve: ECONNREFUSED");
    close(s2);

    pid_t p = filho(eu, "--cliente");
    int c = accept(s, NULL, NULL);
    confere(c >= 0, "accept");
    char r[8] = {0};
    confere(ler_tudo(c, r, 4) == 4 && !strcmp(r, "ping"), "o servidor recebe ping");
    write(c, "pong", 4);
    static char dados[GRANDE];
    int n = ler_tudo(c, dados, GRANDE), ok = n == GRANDE;
    for (int i = 0; ok && i < GRANDE; i++) if (dados[i] != (char)(i * 13)) ok = 0;
    confere(ok, "200 KB pelo socket, sem errar um byte");
    confere(read(c, r, 1) == 0, "o cliente fechou: fim");
    int st = espera(p);
    confere(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0, "o cliente recebeu pong");
    close(c);
    close(s);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--cliente")) return cliente();
    if (argc == 2 && !strcmp(argv[1], "--munmap")) {
        char *m = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        m[0] = 1;
        munmap(m, 8192);
        return m[0];
    }
    if (argc == 2 && !strcmp(argv[1], "--so-leitura")) {
        char *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        m[0] = 1;
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--shm")) {
        comum_t *c = abrir_comum(0);
        if (!c || strcmp(c->recado, "ola, filho")) return 1;
        somar_comum(c);
        strcpy(c->recado, "oi, pai");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--thread-exit")) {
        pthread_t t;
        pthread_create(&t, NULL, sair_7, NULL);
        for (;;) sched_yield_ou_nada();
    }
    if (argc == 2 && !strcmp(argv[1], "--thread-falha")) {
        pthread_t t;
        pthread_create(&t, NULL, falhar, NULL);
        pthread_join(t, NULL);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--principal-sai")) {
        pthread_t t;
        pthread_create(&t, NULL, contas_thread, NULL);
        pthread_exit(NULL);
    }
    if (argc == 2 && !strcmp(argv[1], "--laco")) for (;;) ;
    if (argc == 2 && !strcmp(argv[1], "--falha")) return *nulo;
    if (argc == 2 && !strcmp(argv[1], "--trata-falha")) { signal(SIGSEGV, tratador_segv); return *nulo; }
    if (argc == 2 && !strcmp(argv[1], "--abort")) abort();
    if (argc == 2 && !strcmp(argv[1], "--fpu-sinal")) {
        signal(SIGUSR1, tratador);
        double e = contas(200000);
        while (recebidos < 5) {                 /* getpid(): obriga a reler 'recebidos' */
            if (getpid() <= 0 || !bits_iguais(contas(200000), e)) return 1;
        }
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--pipe")) {
        int fds[2];
        pipe(fds);
        close(fds[0]);
        write(fds[1], "x", 1);
        return 0;
    }
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
    testa_sinais(argv[0]);
    testa_threads(argv[0]);
    testa_mmap(argv[0]);
    testa_sockets(argv[0]);
    printf("%s: %d de %d testes passaram\n", falhas ? "FALHAS" : "OK", total - falhas, total);
    return falhas;
}
