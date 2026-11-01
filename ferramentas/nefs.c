/*
 * ===========================================================================
 *  NEFS - ferramentas/nefs.c
 *  O nefs DENTRO do NovaEra: o NovaEra compilando o próprio NovaEra.
 *
 *    nefs compilar [-v] [-sem-portes] [pasta]
 *        compila o sistema da pasta (padrão: a pasta atual), como o
 *        build.sh faz no Linux, e gera pasta/build/kernel.bin e
 *        pasta/build/initrd.tar
 *    nefs novo <pasta> [nome]
 *        um sistema novo: copia o código de /usr/src/novaera para a pasta,
 *        com outro nome (sistema.conf)
 *    nefs info
 *        mostra o sistema e as ferramentas
 *
 *  O código do NovaEra fica em /usr/src/novaera, e o dos portes (o QBE e o
 *  cproc) em /usr/src/<pasta do porte>. O initrd gerado leva tudo isso de
 *  novo: o sistema compilado aqui também sabe se compilar.
 *
 *  Os passos são os do build.sh, na mesma ordem:
 *    1. build/sistema.h, build/programas.h e build/programas.s, a partir de
 *       sistema.conf e programas.lista
 *    2. os servidores do boot (usuario/<nome>.c) -> build/<nome>.elf
 *    3. a libc (libc.a), os comandos (usuario/bin) e as ferramentas
 *    4. os portes (portes/<nome>/receita)
 *    5. o núcleo -> build/kernel.bin;  e o initrd -> build/initrd.tar
 *  Quem compila é o cproc (com nefs-cpp, qbe e nefs-as); quem liga, o nefs-ld.
 *
 *  Escrito em C padrão (com o pedaço do POSIX que a libc do NovaEra tem):
 *  compila também no Linux.
 * ===========================================================================
 */
#include <dirent.h>
#include <errno.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static int verboso;

/* A imagem de boot do GRUB, no código do NovaEra (veja ferramentas/grub). */
#define GRUB_ELTORITO "ferramentas/grub/eltorito.img"

/* ------------------------------ Utilidades ------------------------------- */

static void erro(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "nefs: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static void *xmalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) erro("sem memória");
    return p;
}

static char *xstrdup(const char *s)
{
    char *d = xmalloc(strlen(s) + 1);
    strcpy(d, s);
    return d;
}

/* Texto que cresce. */
typedef struct {
    char  *d;
    size_t n, cap;
} texto_t;

static void tx_poe(texto_t *t, const char *s, size_t n)
{
    if (t->n + n + 1 > t->cap) {
        t->cap = (t->n + n + 1) * 2;
        char *d = realloc(t->d, t->cap);
        if (!d) erro("sem memória");
        t->d = d;
    }
    memcpy(t->d + t->n, s, n);
    t->n += n;
    t->d[t->n] = '\0';
}

static void tx_s(texto_t *t, const char *s) { tx_poe(t, s, strlen(s)); }

static void tx_f(texto_t *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);        /* primeiro, o tamanho */
    va_end(ap);
    if (n < 0) erro("formato inválido");
    char *b = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(b, (size_t)n + 1, fmt, ap);
    va_end(ap);
    tx_poe(t, b, (size_t)n);
    free(b);
}

static char *junta(const char *a, const char *b)
{
    texto_t t = {0};
    tx_s(&t, a);
    if (t.n && t.d[t.n - 1] != '/') tx_s(&t, "/");
    tx_s(&t, b);
    return t.d;
}

/* Lista de textos. */
typedef struct {
    char **v;
    int    n, cap;
} lista_t;

static void lista_poe(lista_t *l, const char *s)
{
    if (l->n + 2 > l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        char **v = realloc(l->v, sizeof(char *) * (size_t)l->cap);
        if (!v) erro("sem memória");
        l->v = v;
    }
    l->v[l->n++] = xstrdup(s);
    l->v[l->n] = NULL;
}

static int compara_txt(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void lista_ordena(lista_t *l) { if (l->n) qsort(l->v, (size_t)l->n, sizeof(char *), compara_txt); }

/* ------------------------------- Arquivos -------------------------------- */

static int existe(const char *c) { struct stat st; return stat(c, &st) == 0; }
static int eh_pasta(const char *c) { struct stat st; return stat(c, &st) == 0 && S_ISDIR(st.st_mode); }

static char *le_arquivo(const char *nome, size_t *tam)
{
    FILE *f = fopen(nome, "rb");
    if (!f) return NULL;
    texto_t t = {0};
    char bloco[4096];
    size_t n;
    while ((n = fread(bloco, 1, sizeof bloco, f)) > 0) tx_poe(&t, bloco, n);
    fclose(f);
    if (!t.d) tx_s(&t, "");
    if (tam) *tam = t.n;
    return t.d;
}

static void grava_arquivo(const char *nome, const char *dados, size_t tam)
{
    FILE *f = fopen(nome, "wb");
    if (!f) erro("não consegui criar %s: %s", nome, strerror(errno));
    if (fwrite(dados, 1, tam, f) != tam || fclose(f) != 0) erro("erro ao gravar %s", nome);
}

/* mkdir -p */
static void cria_pastas(const char *caminho)
{
    char *c = xstrdup(caminho);
    for (char *p = c + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            char fim = *p;
            *p = '\0';
            if (!eh_pasta(c) && mkdir(c, 0755) != 0) erro("não consegui criar a pasta %s", c);
            if (!fim) break;
            *p = '/';
        }
    }
    free(c);
}

/* Os arquivos de uma pasta que terminam em 'fim' (em ordem alfabética). */
static void lista_pasta(const char *pasta, const char *fim, lista_t *l)
{
    DIR *d = opendir(pasta);
    if (!d) return;
    struct dirent *e;
    size_t nf = strlen(fim);
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (e->d_name[0] == '.' || n < nf || strcmp(e->d_name + n - nf, fim) != 0) continue;
        char *c = junta(pasta, e->d_name);
        if (!eh_pasta(c)) lista_poe(l, c);
        free(c);
    }
    closedir(d);
    lista_ordena(l);
}

static char *base(const char *caminho, const char *tira)
{
    const char *b = strrchr(caminho, '/');
    b = b ? b + 1 : caminho;
    char *r = xstrdup(b);
    size_t n = strlen(r), k = strlen(tira);
    if (n >= k && strcmp(r + n - k, tira) == 0) r[n - k] = '\0';
    return r;
}

/* ------------------------------- Comandos -------------------------------- */

/* Executa um programa e espera; para tudo se ele falhar. */
static void executa(lista_t *args)
{
    if (verboso) {
        for (int i = 0; i < args->n; i++) printf("%s%s", i ? " " : "  $ ", args->v[i]);
        printf("\n");
        fflush(stdout);
    }
    pid_t pid;
    int r = posix_spawnp(&pid, args->v[0], NULL, NULL, args->v, environ);
    if (r) erro("não consegui executar %s: %s", args->v[0], strerror(r));
    int status;
    if (waitpid(pid, &status, 0) < 0) erro("waitpid falhou");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        texto_t t = {0};
        for (int i = 0; i < args->n; i++) { if (i) tx_s(&t, " "); tx_s(&t, args->v[i]); }
        erro("falhou (código %d): %s", WEXITSTATUS(status), t.d);
    }
}

static lista_t comando(const char *prog)
{
    lista_t l = {0};
    lista_poe(&l, prog);
    return l;
}

static void args(lista_t *l, ...)
{
    va_list ap;
    va_start(ap, l);
    const char *s;
    while ((s = va_arg(ap, const char *))) lista_poe(l, s);
    va_end(ap);
}

/* As mesmas opções do build.sh para o cproc. */
static const char *const CFLAGS[] = {
    "-std=c11", "-nostdinc", "-Ilibc/include", "-I.", "-Ibuild", "-Iusuario", NULL };
static const char *const CFLAGS_NUCLEO[] = { "-std=c11", "-I.", "-Ibuild", NULL };

static void compila(const char *saida, const char *fonte, const char *const *flags, const char *extra)
{
    lista_t c = comando("cproc");
    for (int i = 0; flags[i]; i++) lista_poe(&c, flags[i]);
    if (extra) lista_poe(&c, extra);
    args(&c, "-c", "-o", saida, fonte, (char *)NULL);
    executa(&c);
}

static void monta(const char *saida, const char *fonte)
{
    lista_t c = comando("nefs-as");
    args(&c, "-o", saida, fonte, (char *)NULL);
    executa(&c);
}

static long tamanho(const char *c) { struct stat st; return stat(c, &st) == 0 ? (long)st.st_size : -1; }

/* ---------------------- Variáveis (sistema.conf, receitas) --------------- */

/* NOME="valor" (as aspas podem atravessar linhas), como no sh. */
typedef struct {
    lista_t nomes, valores;
} vars_t;

static void le_vars(const char *caminho, vars_t *v)
{
    char *t = le_arquivo(caminho, NULL);
    if (!t) erro("não consegui ler %s", caminho);
    char *p = t;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        char *ini = p;
        while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_') p++;
        if (p == ini || *p != '=') { while (*p && *p != '\n') p++; continue; }
        char *nome = ini;
        *p++ = '\0';
        texto_t val = {0};
        tx_s(&val, "");
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p && *p != q) {
                if (q == '"' && *p == '\\' && p[1]) p++;
                tx_poe(&val, p++, 1);
            }
            if (*p) p++;
        } else {
            while (*p && *p != '\n' && *p != ' ' && *p != '\t' && *p != '#') tx_poe(&val, p++, 1);
        }
        lista_poe(&v->nomes, nome);
        lista_poe(&v->valores, val.d);
        free(val.d);
    }
    free(t);
}

static const char *var(const vars_t *v, const char *nome, const char *padrao)
{
    for (int i = v->nomes.n - 1; i >= 0; i--)
        if (strcmp(v->nomes.v[i], nome) == 0) return v->valores.v[i];
    return padrao;
}

/* Separa um texto em palavras (espaços, tabs e quebras de linha). */
static void palavras(const char *s, lista_t *l)
{
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\n') s++;
        const char *ini = s;
        while (*s && *s != ' ' && *s != '\t' && *s != '\n') s++;
        if (s > ini) {
            char *w = xmalloc((size_t)(s - ini) + 1);
            memcpy(w, ini, (size_t)(s - ini));
            lista_poe(l, w);
            free(w);
        }
    }
}

/* Texto UTF-8 -> literal C em CP437 (a tabela da VGA), como o build.sh. */
static char *cp437(const char *s)
{
    static const struct { const char *u, *c; } tab[] = {
        {"ç","\\207"},{"Ç","\\200"},{"á","\\240"},{"é","\\202"},{"í","\\241"},
        {"ó","\\242"},{"ú","\\243"},{"â","\\203"},{"ê","\\210"},{"ô","\\223"},
        {"à","\\205"},{"ü","\\201"},{"É","\\220"},{"ã","a"},{"õ","o"},{"Ã","A"},{"Õ","O"},
        {"Á","A"},{"Í","I"},{"Ó","O"},{"Ú","U"},{"Â","A"},{"Ê","E"},{"Ô","O"},
    };
    texto_t t = {0};
    tx_s(&t, "");
    while (*s) {
        if (*s == '"') { tx_s(&t, "\\\""); s++; continue; }
        size_t i;
        for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
            size_t n = strlen(tab[i].u);
            if (strncmp(s, tab[i].u, n) == 0) { tx_s(&t, tab[i].c); s += n; break; }
        }
        if (i == sizeof tab / sizeof tab[0]) tx_poe(&t, s++, 1);
    }
    return t.d;
}

/* --------------------------------- tar ----------------------------------- */

/* O initrd: um tar "ustar", o que o servidor de arquivos sabe ler. */
typedef struct {
    char  *nome;        /* no tar: "bin/ola" */
    char  *origem;      /* um arquivo, ou NULL */
    char  *dados;       /* ou os bytes em memória */
    size_t tam;
    int    pasta, exec;
} entrada_t;

static entrada_t *ents;
static int nents, capents;

static entrada_t *tar_entrada(const char *nome)
{
    for (int i = 0; i < nents; i++)
        if (strcmp(ents[i].nome, nome) == 0) return &ents[i];   /* o último vale */
    if (nents == capents) {
        capents = capents ? capents * 2 : 256;
        entrada_t *e = realloc(ents, sizeof(entrada_t) * (size_t)capents);
        if (!e) erro("sem memória");
        ents = e;
    }
    entrada_t *e = &ents[nents++];
    memset(e, 0, sizeof *e);
    e->nome = xstrdup(nome);
    return e;
}

static void tar_arquivo(const char *nome, const char *origem, int exec)
{
    entrada_t *e = tar_entrada(nome);
    e->origem = xstrdup(origem);
    e->exec = exec;
}

static void tar_dados(const char *nome, const char *dados, size_t tam)
{
    entrada_t *e = tar_entrada(nome);
    e->dados = xmalloc(tam + 1);
    memcpy(e->dados, dados, tam);
    e->tam = tam;
}

/* Uma pasta inteira, menos as subpastas da lista 'fora'. */
static void tar_arvore(const char *nome, const char *origem, const char *const *fora)
{
    DIR *d = opendir(origem);
    if (!d) return;
    lista_t itens = {0};
    struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.') lista_poe(&itens, e->d_name);
    closedir(d);
    lista_ordena(&itens);
    for (int i = 0; i < itens.n; i++) {
        char *o = junta(origem, itens.v[i]), *n = junta(nome, itens.v[i]);
        int pular = 0;
        for (int k = 0; fora && fora[k]; k++) if (strcmp(o, fora[k]) == 0) pular = 1;
        if (!pular) {
            if (eh_pasta(o)) tar_arvore(n, o, fora);
            else tar_arquivo(n, o, 0);
        }
        free(o);
        free(n);
    }
}

static int compara_ent(const void *a, const void *b)
{
    return strcmp(((const entrada_t *)a)->nome, ((const entrada_t *)b)->nome);
}

static void octal(char *campo, size_t tam, unsigned long v)
{
    /* tam-1 dígitos, completados com zeros, e o '\0' */
    campo[tam - 1] = '\0';
    for (size_t i = tam - 1; i-- > 0; v >>= 3) campo[i] = (char)('0' + (v & 7));
}

static void tar_grava(const char *saida)
{
    /* as pastas de cada caminho também entram, antes do conteúdo */
    int n = nents;
    for (int i = 0; i < n; i++) {
        char *c = xstrdup(ents[i].nome);
        for (char *p = c; (p = strchr(p, '/')); p++) {
            *p = '\0';
            entrada_t *e = tar_entrada(c);
            if (!e->origem && !e->dados) e->pasta = 1;
            *p = '/';
        }
        free(c);
    }
    qsort(ents, (size_t)nents, sizeof(entrada_t), compara_ent);

    FILE *f = fopen(saida, "wb");
    if (!f) erro("não consegui criar %s", saida);
    static char zeros[512];
    for (int i = 0; i < nents; i++) {
        entrada_t *e = &ents[i];
        char *dados = e->dados;
        size_t tam = e->tam;
        if (e->origem) {
            dados = le_arquivo(e->origem, &tam);
            if (!dados) erro("não consegui ler %s", e->origem);
        }
        char h[512];
        memset(h, 0, sizeof h);
        size_t nn = strlen(e->nome) + (e->pasta ? 1 : 0);
        if (nn > 99) erro("caminho longo demais para o tar: %s", e->nome);
        strcpy(h, e->nome);
        if (e->pasta) strcat(h, "/");
        octal(h + 100, 8, e->pasta || e->exec ? 0755 : 0644);
        octal(h + 108, 8, 0);
        octal(h + 116, 8, 0);
        octal(h + 124, 12, e->pasta ? 0 : (unsigned long)tam);
        octal(h + 136, 12, 0);
        h[156] = e->pasta ? '5' : '0';
        memcpy(h + 257, "ustar", 6);
        memcpy(h + 263, "00", 2);
        memset(h + 148, ' ', 8);
        unsigned long soma = 0;
        for (int k = 0; k < 512; k++) soma += (unsigned char)h[k];
        octal(h + 148, 7, soma);
        h[155] = ' ';
        fwrite(h, 1, 512, f);
        if (!e->pasta) {
            fwrite(dados, 1, tam, f);
            if (tam % 512) fwrite(zeros, 1, 512 - tam % 512, f);
        }
        if (e->origem) free(dados);
    }
    fwrite(zeros, 1, 512, f);
    fwrite(zeros, 1, 512, f);
    if (fclose(f) != 0) erro("erro ao gravar %s", saida);
}

/* ---------------------------- O código-fonte ----------------------------- */

/* O que vai para /usr/src/novaera: o mesmo que o build.sh põe lá. */
static const char *const fontes_raiz[] = {
    "kernel.c", "novaera.h", "boot.s", "cpu.s", "linker.ld", "build.sh",
    "sistema.conf", "programas.lista", "logo.txt", "LICENSE", NULL };
static const char *const fontes_pastas[] = {
    "usuario", "libc", "ferramentas", "exemplos", "portes", "nefs/livro", NULL };
static const char *const fontes_fora[] = { "portes/fontes", "ferramentas/testes", NULL };

/* ------------------------------- Portes ---------------------------------- */

typedef struct {
    vars_t v;
    char  *pasta_porte;      /* portes/qbe */
    char  *fonte;            /* onde está o código (/usr/src/qbe-1.3) */
} porte_t;

/* As fontes: extraídas em portes/fontes/<PASTA>, ou em /usr/src/<PASTA>. */
static char *acha_fontes(const char *pasta)
{
    char *c = junta("portes/fontes", pasta);
    if (eh_pasta(c)) return c;
    free(c);
    c = junta("/usr/src", pasta);
    if (eh_pasta(c)) return c;
    free(c);
    return NULL;
}

static lista_t compilados;      /* os objetos de portes já feitos neste build */

static void compila_porte(porte_t *p, lista_t *bins, int pular)
{
    const char *nome = var(&p->v, "NOME", "?"), *pasta = var(&p->v, "PASTA", "");
    p->fonte = acha_fontes(pasta);
    if (!p->fonte) {
        printf("  portes: %s sem fontes (nem portes/fontes/%s nem /usr/src/%s), fica de fora\n",
               nome, pasta, pasta);
        return;
    }
    if (pular) {
        printf("  portes: %s pulado (-sem-portes): o sistema novo não terá o %s\n", nome, nome);
        return;
    }
    lista_t progs = {0};
    palavras(var(&p->v, "PROGRAMAS", nome), &progs);
    /* Compila DENTRO da pasta do porte, com o caminho relativo ("main.c"):
     * o assert() guarda esse caminho, e assim o porte sai igual ao do
     * build.sh no Linux. As outras pastas vão absolutas. */
    char raiz[256];
    if (!getcwd(raiz, sizeof raiz)) erro("getcwd falhou");
    const char *flags[16];
    char fl[6][300];
    snprintf(fl[0], sizeof fl[0], "-I%s/libc/include", raiz);
    snprintf(fl[1], sizeof fl[1], "-I%s", raiz);
    snprintf(fl[2], sizeof fl[2], "-I%s/build", raiz);
    snprintf(fl[3], sizeof fl[3], "-I%s/usuario", raiz);
    snprintf(fl[4], sizeof fl[4], "-I%s/%s", raiz, p->pasta_porte);    /* o config.h do NovaEra */
    flags[0] = "-std=c11"; flags[1] = "-nostdinc";
    for (int i = 0; i < 5; i++) flags[2 + i] = fl[i];
    flags[7] = NULL;
    for (int i = 0; i < progs.n; i++) {
        char vn[64];
        snprintf(vn, sizeof vn, "FONTES_%s", progs.v[i]);
        for (char *c = vn; *c; c++) if (*c == '-') *c = '_';
        lista_t fs = {0};
        palavras(var(&p->v, vn, ""), &fs);
        lista_t ld = comando("nefs-ld");
        char saida[128];
        snprintf(saida, sizeof saida, "build/bin/%s", progs.v[i]);
        args(&ld, "-o", saida, "build/libc/crt0.o", (char *)NULL);
        for (int k = 0; k < fs.n; k++) {
            char obj[128], abs_obj[400];
            snprintf(obj, sizeof obj, "build/p/%s-%s.o", nome, fs.v[k]);
            for (char *c = obj + 8; *c; c++) if (*c == '/') *c = '_';
            snprintf(abs_obj, sizeof abs_obj, "%s/%s", raiz, obj);
            int feito = 0;                /* um .c de dois programas: uma vez só */
            for (int j = 0; j < compilados.n; j++) if (!strcmp(compilados.v[j], obj)) feito = 1;
            if (!feito) {
                if (chdir(p->fonte) != 0) erro("não achei %s", p->fonte);
                compila(abs_obj, fs.v[k], flags, NULL);
                if (chdir(raiz) != 0) erro("não consegui voltar para %s", raiz);
                lista_poe(&compilados, obj);
            }
            lista_poe(&ld, obj);
        }
        lista_poe(&ld, "build/libc/libc.a");
        executa(&ld);
        lista_poe(bins, saida);
        printf("  portes: %s -> bin/%s (%ld bytes)\n", nome, progs.v[i], tamanho(saida));
    }
}

/* O código do porte que vai para /usr/src/<PASTA>: os .c usados, todos os
 * .h, o LICENSE, e os arquivos do NovaEra para ele (como o config.h). */
static void fontes_do_porte(porte_t *p, const char *dir, const char *prefixo)
{
    DIR *d = opendir(dir);
    if (!d) return;
    lista_t itens = {0};
    struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.') lista_poe(&itens, e->d_name);
    closedir(d);
    lista_ordena(&itens);
    lista_t usados = {0};
    palavras(var(&p->v, "PROGRAMAS", ""), &usados);
    for (int i = 0; i < itens.n; i++) {
        char *o = junta(dir, itens.v[i]), *n = junta(prefixo, itens.v[i]);
        if (eh_pasta(o)) fontes_do_porte(p, o, n);
        else {
            size_t k = strlen(itens.v[i]);
            /* o caminho relativo à raiz das fontes */
            const char *rel = o + strlen(p->fonte) + 1;
            int quero = (k > 2 && strcmp(itens.v[i] + k - 2, ".h") == 0) ||
                        strcmp(itens.v[i], "LICENSE") == 0;
            for (int j = 0; j < usados.n && !quero; j++) {
                char vn[64];
                snprintf(vn, sizeof vn, "FONTES_%s", usados.v[j]);
                for (char *c = vn; *c; c++) if (*c == '-') *c = '_';
                lista_t fs = {0};
                palavras(var(&p->v, vn, ""), &fs);
                for (int m = 0; m < fs.n; m++) if (strcmp(fs.v[m], rel) == 0) quero = 1;
            }
            if (quero) tar_arquivo(n, o, 0);
        }
        free(o);
        free(n);
    }
}

/* ------------------------------ compilar --------------------------------- */

static void cmd_compilar(int argc, char **argv)
{
    const char *pasta = ".";
    int sem_portes = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) verboso = 1;
        else if (strcmp(argv[i], "-sem-portes") == 0) sem_portes = 1;
        else pasta = argv[i];
    }
    if (chdir(pasta) != 0) erro("não achei a pasta %s", pasta);
    if (!existe("kernel.c") || !existe("programas.lista")) erro("%s não parece o código do NovaEra (falta kernel.c)", pasta);
    clock_t inicio = clock();
    cria_pastas("build/bin");
    cria_pastas("build/libc");
    cria_pastas("build/o");
    cria_pastas("build/p");

    /* --- 1. arquivos gerados --- */
    vars_t conf = {0};
    le_vars("sistema.conf", &conf);
    const char *nome = var(&conf, "NOME", "NovaEra"), *versao = var(&conf, "VERSAO", "?");
    const char *lema = var(&conf, "LEMA", ""), *prompt = var(&conf, "PROMPT", "NovaEra");
    const char *puro = var(&conf, "PURO", "nao");
    texto_t h = {0};
    tx_s(&h, "/* Gerado por build.sh a partir de sistema.conf. Não edite. */\n");
    tx_f(&h, "#define SISTEMA_NOME   \"%s\"\n", cp437(nome));
    tx_f(&h, "#define SISTEMA_VERSAO \"%s\"\n", cp437(versao));
    tx_f(&h, "#define SISTEMA_LEMA   \"%s\"\n", cp437(lema));
    tx_f(&h, "#define SISTEMA_PROMPT \"%s\"\n", cp437(prompt));
    tx_f(&h, "#define SISTEMA_PURO   %d\n", strcmp(puro, "sim") == 0);
    grava_arquivo("build/sistema.h", h.d, h.n);

    char *lst = le_arquivo("programas.lista", NULL);
    if (!lst) erro("não consegui ler programas.lista");
    lista_t servs = {0};
    texto_t s = {0}, ph = {0}, tab = {0};
    tx_s(&s, "# Gerado por build.sh a partir de programas.lista. Não edite.\n.section .rodata\n");
    tx_s(&ph, "/* Gerado por build.sh a partir de programas.lista. Não edite. */\n");
    for (char *l = strtok(lst, "\n"); l; l = strtok(NULL, "\n")) {
        lista_t w = {0};
        palavras(l, &w);
        if (!w.n || w.v[0][0] == '#') continue;
        char *f = junta("usuario", w.v[0]);
        texto_t fc = {0};
        tx_f(&fc, "%s.c", f);
        if (!existe(fc.d)) erro("programas.lista cita '%s', mas %s não existe", w.v[0], fc.d);
        texto_t flags = {0};
        tx_s(&flags, "0");
        for (int i = 1; i < w.n; i++) {
            if (strcmp(w.v[i], "driver") == 0) tx_s(&flags, " | T_DRIVER");
            else if (strcmp(w.v[i], "vga") == 0) tx_s(&flags, " | T_VGA");
            else if (strcmp(w.v[i], "initrd") == 0) tx_s(&flags, " | T_INITRD");
            else if (strcmp(w.v[i], "-") != 0) erro("permissão desconhecida '%s' para '%s'", w.v[i], w.v[0]);
        }
        const char *n = w.v[0];
        lista_poe(&servs, n);
        tx_f(&s, ".globl prog_%s_ini\n.globl prog_%s_fim\n.balign 8\nprog_%s_ini:\n"
                 "    .incbin \"build/%s.elf\"\nprog_%s_fim:\n", n, n, n, n, n);
        tx_f(&ph, "extern const u8 prog_%s_ini[], prog_%s_fim[];\n", n, n);
        tx_f(&tab, "    { \"%s\", prog_%s_ini, prog_%s_fim, %s },\n", n, n, n, flags.d);
    }
    tx_f(&ph, "#define NUM_PROGRAMAS %d\nstatic const programa_info_t PROGRAMAS[NUM_PROGRAMAS] = {\n%s};\n",
         servs.n, tab.d ? tab.d : "");
    tx_s(&s, ".section .note.GNU-stack,\"\",@progbits\n");
    grava_arquivo("build/programas.h", ph.d, ph.n);
    grava_arquivo("build/programas.s", s.d, s.n);

    /* --- 2. servidores do boot --- */
    monta("build/crt0.o", "usuario/crt0.s");
    monta("build/sys.o", "usuario/sys.s");
    for (int i = 0; i < servs.n; i++) {
        char o[128], c[128], e[128];
        snprintf(o, sizeof o, "build/%s.o", servs.v[i]);
        snprintf(c, sizeof c, "usuario/%s.c", servs.v[i]);
        snprintf(e, sizeof e, "build/%s.elf", servs.v[i]);
        compila(o, c, CFLAGS, NULL);
        lista_t ld = comando("nefs-ld");
        args(&ld, "-o", e, "build/crt0.o", "build/sys.o", o, (char *)NULL);
        executa(&ld);
        printf("  %s -> %s (%ld bytes)\n", c, e, tamanho(e));
    }

    /* --- 3. libc, comandos e ferramentas --- */
    monta("build/libc/crt0.o", "libc/crt0.s");
    lista_t libc = {0}, ar = comando("nefs-ar");
    args(&ar, "rcs", "build/libc/libc.a", "build/libc/crt0.o", "build/sys.o", (char *)NULL);
    lista_pasta("libc/src", ".c", &libc);
    for (int i = 0; i < libc.n; i++) {
        char *b = base(libc.v[i], ".c"), o[128];
        snprintf(o, sizeof o, "build/libc/%s.o", b);
        compila(o, libc.v[i], CFLAGS, NULL);
        lista_poe(&ar, o);
        free(b);
    }
    executa(&ar);
    printf("  libc/src/*.c -> libc.a (%d partes)\n", libc.n + 2);

    lista_t progs = {0}, bins = {0};
    lista_pasta("usuario/bin", ".c", &progs);
    lista_pasta("ferramentas", ".c", &progs);
    for (int i = 0; i < progs.n; i++) {
        char *b = base(progs.v[i], ".c"), o[128], e[128];
        snprintf(o, sizeof o, "build/o/%s.o", b);
        snprintf(e, sizeof e, "build/bin/%s", b);
        compila(o, progs.v[i], CFLAGS, NULL);
        lista_t ld = comando("nefs-ld");
        args(&ld, "-o", e, "build/libc/crt0.o", o, "build/libc/libc.a", (char *)NULL);
        executa(&ld);
        lista_poe(&bins, e);
        free(b);
    }
    printf("  usuario/bin/*.c e ferramentas/*.c -> %d programas\n", progs.n);

    /* --- 4. portes --- */
    lista_t receitas = {0};
    DIR *d = opendir("portes");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char *pp = junta("portes", e->d_name), *r = junta(pp, "receita");
            if (existe(r)) lista_poe(&receitas, pp);
            free(pp);
            free(r);
        }
        closedir(d);
        lista_ordena(&receitas);
    }
    porte_t *portes = xmalloc(sizeof(porte_t) * (size_t)(receitas.n + 1));
    for (int i = 0; i < receitas.n; i++) {
        char *r = junta(receitas.v[i], "receita");
        le_vars(r, &portes[i].v);
        portes[i].pasta_porte = receitas.v[i];
        compila_porte(&portes[i], &bins, sem_portes);
        free(r);
    }

    /* --- 5. o núcleo --- */
    monta("build/boot.o", "boot.s");
    monta("build/cpu.o", "cpu.s");
    monta("build/programas.o", "build/programas.s");
    compila("build/kernel.o", "kernel.c", CFLAGS_NUCLEO, NULL);
    lista_t ld = comando("nefs-ld");
    args(&ld, "-binario", "-base", "0x100000", "-inicio", ".multiboot", "-o", "build/kernel.bin",
         "build/boot.o", "build/cpu.o", "build/kernel.o", "build/programas.o", (char *)NULL);
    executa(&ld);

    /* --- e o initrd --- */
    for (int i = 0; i < bins.n; i++) {
        char *b = base(bins.v[i], ""), *n = junta("bin", b);
        tar_arquivo(n, bins.v[i], 1);
        free(b);
        free(n);
    }
    texto_t es = {0};
    tx_f(&es, "NOME=%s\nVERSAO=%s\nLEMA=%s\nPURO=%s\n", nome, versao, lema, puro);
    tar_dados("etc/sistema", es.d, es.n);
    if (existe("logo.txt")) tar_arquivo("etc/logo.txt", "logo.txt", 0);
    tar_arquivo("usr/lib/nefs/libc.a", "build/libc/libc.a", 0);
    tar_arvore("usr/include", "libc/include", NULL);
    tar_arvore("usr/share/nefs/exemplos", "exemplos", NULL);
    tar_arvore("usr/share/nefs/livro", "nefs/livro", NULL);
    if (existe(GRUB_ELTORITO)) tar_arquivo("usr/lib/nefs/grub/eltorito.img", GRUB_ELTORITO, 0);
    for (int i = 0; i < receitas.n; i++) {
        const char *ex = var(&portes[i].v, "EXEMPLOS", NULL);
        if (ex && portes[i].fonte) {
            char *o = junta(receitas.v[i], "exemplos");
            tar_arvore(ex[0] == '/' ? ex + 1 : ex, o, NULL);
            free(o);
        }
    }
    /* o código, para o sistema novo também saber se compilar */
    for (int i = 0; fontes_raiz[i]; i++) {
        if (!existe(fontes_raiz[i])) continue;
        char *n = junta("usr/src/novaera", fontes_raiz[i]);
        tar_arquivo(n, fontes_raiz[i], 0);
        free(n);
    }
    for (int i = 0; fontes_pastas[i]; i++) {
        char *n = junta("usr/src/novaera", fontes_pastas[i]);
        tar_arvore(n, fontes_pastas[i], fontes_fora);
        free(n);
    }
    for (int i = 0; i < receitas.n; i++) {
        if (!portes[i].fonte) continue;
        char *n = junta("usr/src", var(&portes[i].v, "PASTA", "?"));
        fontes_do_porte(&portes[i], portes[i].fonte, n);
        /* os arquivos do NovaEra para o porte (o config.h) */
        DIR *dp = opendir(receitas.v[i]);
        struct dirent *e;
        while (dp && (e = readdir(dp))) {
            if (e->d_name[0] == '.' || !strcmp(e->d_name, "receita") || !strcmp(e->d_name, "exemplos")) continue;
            char *o = junta(receitas.v[i], e->d_name), *t = junta(n, e->d_name);
            if (!eh_pasta(o)) tar_arquivo(t, o, 0);
            free(o);
            free(t);
        }
        if (dp) closedir(dp);
        free(n);
    }
    tar_grava("build/initrd.tar");

    long seg = (long)((clock() - inicio) / CLOCKS_PER_SEC);
    printf("OK: build/kernel.bin (%ld bytes) e build/initrd.tar (%ld bytes), em %ld min %ld s\n",
           tamanho("build/kernel.bin"), tamanho("build/initrd.tar"), seg / 60, seg % 60);
}

/* ---------------------------------- iso ---------------------------------- */

/*
 * Uma ISO inicializável (CD/DVD, ou um pendrive no QEMU): ISO 9660 com o
 * boot "El Torito". Quem dá o boot é o GRUB (a imagem pronta em
 * ferramentas/grub/eltorito.img: o GRUB não compila dentro do NovaEra),
 * que lê /boot/grub/grub.cfg e carrega o kernel.bin e o initrd.tar.
 *
 *   setor 0-15   vazios (área do sistema)
 *   16           descritor primário (o nome do volume, onde está a raiz)
 *   17           descritor de boot El Torito (onde está o catálogo)
 *   18           fim dos descritores
 *   19, 20       tabelas de caminhos (little e big endian)
 *   21           o catálogo de boot (qual arquivo é a imagem de boot)
 *   22...        as pastas e, depois, os arquivos
 * Os números de 16 e 32 bits aparecem duas vezes: little e big endian.
 */

#define ISO_SETOR 2048u

typedef struct {
    const char *nome;      /* 8.3 em maiúsculas, como pede o ISO 9660 nível 1 */
    int   pai;             /* índice da pasta (a raiz é 0) */
    int   pasta;
    char *dados;
    size_t tam;
    unsigned lba, setores;
} iso_item_t;

static void iso_le32(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static void iso_be32(unsigned char *p, unsigned v) { p[3] = (unsigned char)v; p[2] = (unsigned char)(v >> 8); p[1] = (unsigned char)(v >> 16); p[0] = (unsigned char)(v >> 24); }
static void iso_ambos32(unsigned char *p, unsigned v) { iso_le32(p, v); iso_be32(p + 4, v); }
static void iso_ambos16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

/* Um registro de pasta; devolve o tamanho. */
static unsigned iso_registro(unsigned char *p, const char *nome, unsigned nlen, unsigned lba,
                             unsigned tam, int pasta)
{
    unsigned n = 33 + nlen;
    if (n & 1) n++;
    memset(p, 0, n);
    p[0] = (unsigned char)n;
    iso_ambos32(p + 2, lba);
    iso_ambos32(p + 10, tam);
    p[25] = pasta ? 2 : 0;
    iso_ambos16(p + 28, 1);
    p[32] = (unsigned char)nlen;
    memcpy(p + 33, nome, nlen);
    return n;
}

static void iso_espaco(unsigned char *p, size_t n) { memset(p, ' ', n); }

static void cmd_iso(int argc, char **argv)
{
    const char *pasta = ".", *saida = "build/novaera.iso";
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) saida = argv[++i];
        else pasta = argv[i];
    }
    if (chdir(pasta) != 0) erro("não achei a pasta %s", pasta);
    if (!existe("build/kernel.bin") || !existe("build/initrd.tar"))
        erro("falta build/kernel.bin ou build/initrd.tar: rode antes  nefs compilar");
    const char *grub = existe(GRUB_ELTORITO) ? GRUB_ELTORITO : "/usr/lib/nefs/grub/eltorito.img";
    if (!existe(grub)) erro("não achei a imagem de boot do GRUB (%s)", GRUB_ELTORITO);

    static const char cfg[] =
        "# GRUB da ISO do NovaEra (gerada por \"nefs iso\")\n"
        "set timeout=0\n"
        "menuentry \"NovaEra\" {\n"
        "    multiboot /boot/kernel.bin\n"
        "    module /boot/initrd.tar initrd\n"
        "    boot\n"
        "}\n";
    iso_item_t it[8];
    int n = 0;
    memset(it, 0, sizeof it);
    it[n].nome = "";             it[n].pasta = 1; it[n].pai = 0; n++;     /* 0: raiz */
    it[n].nome = "BOOT";         it[n].pasta = 1; it[n].pai = 0; n++;     /* 1 */
    it[n].nome = "GRUB";         it[n].pasta = 1; it[n].pai = 1; n++;     /* 2 */
    /* em cada pasta, os nomes em ordem alfabética (o ISO 9660 pede) */
    it[n].nome = "INITRD.TAR";   it[n].pai = 1; it[n].dados = le_arquivo("build/initrd.tar", &it[n].tam); n++;
    it[n].nome = "KERNEL.BIN";   it[n].pai = 1; it[n].dados = le_arquivo("build/kernel.bin", &it[n].tam); n++;
    it[n].nome = "ELTORITO.IMG"; it[n].pai = 2; it[n].dados = le_arquivo(grub, &it[n].tam); n++;
    int boot = n - 1;
    it[n].nome = "GRUB.CFG";     it[n].pai = 2; it[n].dados = xstrdup(cfg); it[n].tam = strlen(cfg); n++;
    for (int i = 0; i < n; i++) if (!it[i].pasta && !it[i].dados) erro("não consegui ler %s", it[i].nome);

    /* onde cada coisa fica */
    unsigned lba = 22;
    for (int i = 0; i < n; i++) if (it[i].pasta) { it[i].lba = lba++; it[i].setores = 1; it[i].tam = ISO_SETOR; }
    for (int i = 0; i < n; i++) if (!it[i].pasta) {
        it[i].lba = lba;
        it[i].setores = (unsigned)((it[i].tam + ISO_SETOR - 1) / ISO_SETOR);
        lba += it[i].setores ? it[i].setores : 0;
    }
    /* 150 setores (300 KiB) de folga no fim, como o mkisofs: o GRUB e
     * algumas BIOS leem o CD em blocos grandes, e ler além do fim falha */
    unsigned total = lba + 150;

    /* a "boot info table" que a imagem do GRUB espera (como o
     * -boot-info-table do xorriso): onde está a ISO e a própria imagem */
    unsigned char *bi = (unsigned char *)it[boot].dados;
    if (it[boot].tam < 64) erro("imagem de boot pequena demais");
    unsigned soma = 0;
    for (size_t k = 64; k < it[boot].tam; k += 4)      /* a última palavra, completada com zeros */
        for (size_t j = 0; j < 4 && k + j < it[boot].tam; j++) soma += (unsigned)bi[k + j] << (8 * j);
    memset(bi + 8, 0, 56);
    iso_le32(bi + 8, 16);
    iso_le32(bi + 12, it[boot].lba);
    iso_le32(bi + 16, (unsigned)it[boot].tam);
    iso_le32(bi + 20, soma);

    unsigned char *img = xmalloc((size_t)total * ISO_SETOR);
    /* as pastas */
    for (int d = 0; d < n; d++) {
        if (!it[d].pasta) continue;
        unsigned char *p = img + (size_t)it[d].lba * ISO_SETOR;
        p += iso_registro(p, "\0", 1, it[d].lba, ISO_SETOR, 1);                       /* . */
        p += iso_registro(p, "\1", 1, it[it[d].pai].lba, ISO_SETOR, 1);              /* .. */
        for (int i = 1; i < n; i++) {
            if (it[i].pai != d || i == d) continue;
            char nome[32];
            snprintf(nome, sizeof nome, it[i].pasta ? "%s" : "%s;1", it[i].nome);
            p += iso_registro(p, nome, (unsigned)strlen(nome), it[i].lba, (unsigned)(it[i].pasta ? ISO_SETOR : it[i].tam), it[i].pasta);
        }
    }
    /* os arquivos */
    for (int i = 0; i < n; i++)
        if (!it[i].pasta && it[i].tam) memcpy(img + (size_t)it[i].lba * ISO_SETOR, it[i].dados, it[i].tam);

    /* as tabelas de caminhos: uma linha por pasta (pais antes dos filhos) */
    unsigned char *lt = img + 19 * ISO_SETOR, *mt = img + 20 * ISO_SETOR;
    unsigned tam_tab = 0, num[8] = {0}, k = 0;
    for (int d = 0; d < n; d++) {
        if (!it[d].pasta) continue;
        num[d] = ++k;
        unsigned nl = d == 0 ? 1 : (unsigned)strlen(it[d].nome);
        const char *nm = d == 0 ? "\0" : it[d].nome;
        unsigned char *a = lt + tam_tab, *b = mt + tam_tab;
        a[0] = b[0] = (unsigned char)nl;
        iso_le32(a + 2, it[d].lba);
        iso_be32(b + 2, it[d].lba);
        unsigned pai = d == 0 ? 1 : num[it[d].pai];
        a[6] = (unsigned char)pai; a[7] = (unsigned char)(pai >> 8);
        b[7] = (unsigned char)pai; b[6] = (unsigned char)(pai >> 8);
        memcpy(a + 8, nm, nl);
        memcpy(b + 8, nm, nl);
        tam_tab += 8 + nl + (nl & 1);
    }

    /* o descritor primário */
    unsigned char *pv = img + 16 * ISO_SETOR;
    pv[0] = 1;
    memcpy(pv + 1, "CD001", 5);
    pv[6] = 1;
    iso_espaco(pv + 8, 32);
    iso_espaco(pv + 40, 32);
    memcpy(pv + 40, "NOVAERA", 7);
    iso_ambos32(pv + 80, total);
    iso_ambos16(pv + 120, 1);
    iso_ambos16(pv + 124, 1);
    iso_ambos16(pv + 128, ISO_SETOR);
    iso_ambos32(pv + 132, tam_tab);
    iso_le32(pv + 140, 19);
    iso_be32(pv + 148, 20);
    iso_registro(pv + 156, "\0", 1, it[0].lba, ISO_SETOR, 1);
    iso_espaco(pv + 190, 128 * 4 + 37 * 3);
    memcpy(pv + 318, "NEFS", 4);                     /* quem preparou */
    for (int c = 0; c < 4; c++) {                    /* datas: "não informadas" */
        memset(pv + 813 + 17 * c, '0', 16);
        pv[813 + 17 * c + 16] = 0;
    }
    pv[881] = 1;

    /* El Torito: o descritor de boot e o catálogo */
    unsigned char *br = img + 17 * ISO_SETOR;
    memcpy(br + 1, "CD001", 5);
    br[6] = 1;
    memcpy(br + 7, "EL TORITO SPECIFICATION", 23);
    iso_le32(br + 71, 21);
    unsigned char *cat = img + 21 * ISO_SETOR;
    cat[0] = 1;                                       /* validação, plataforma x86 */
    cat[30] = 0x55; cat[31] = 0xAA;
    unsigned s16 = 0;
    for (int c = 0; c < 32; c += 2) s16 += (unsigned)cat[c] | (unsigned)cat[c + 1] << 8;
    s16 = (0x10000u - (s16 & 0xFFFF)) & 0xFFFF;
    cat[28] = (unsigned char)s16; cat[29] = (unsigned char)(s16 >> 8);
    cat[32] = 0x88;                                   /* a entrada de boot: inicializável */
    cat[33] = 0;                                      /* sem emulação de disquete */
    cat[38] = 4;                                      /* a BIOS carrega 4 setores de 512 */
    iso_le32(cat + 40, it[boot].lba);
    unsigned char *fim = img + 18 * ISO_SETOR;
    fim[0] = 255;
    memcpy(fim + 1, "CD001", 5);
    fim[6] = 1;

    grava_arquivo(saida, (const char *)img, (size_t)total * ISO_SETOR);
    printf("OK: %s (%ld bytes): kernel.bin + initrd.tar + GRUB\n", saida, tamanho(saida));
}

/* -------------------------------- livro ---------------------------------- */

static void cmd_livro(int argc, char **argv)
{
    const char *pasta = existe("/usr/share/nefs/livro") ? "/usr/share/nefs/livro" : "nefs/livro";
    lista_t caps = {0};
    lista_pasta(pasta, ".md", &caps);
    if (!caps.n) erro("o livro não está em %s", pasta);
    /* os capítulos são "01-o-que-e.md", "02-...": o número vem do nome */
    if (argc == 0) {
        printf("O Livro do NEFS (leia um capítulo com: nefs livro <número>):\n");
        for (int i = 0; i < caps.n; i++) {
            char *t = le_arquivo(caps.v[i], NULL), *nl = t ? strchr(t, '\n') : NULL;
            if (nl) *nl = '\0';
            printf("  %s\n", t ? (t[0] == '#' ? t + 2 : t) : caps.v[i]);
            free(t);
        }
        return;
    }
    int n = atoi(argv[0]);
    for (int i = 0; i < caps.n; i++) {
        char *b = base(caps.v[i], "");
        int num = atoi(b);
        free(b);
        if (num != n) continue;
        char *t = le_arquivo(caps.v[i], NULL);
        if (t) fputs(t, stdout);
        return;
    }
    erro("o capítulo %s não existe", argv[0]);
}

/* --------------------------------- novo ---------------------------------- */

static void copia_arquivo(const char *de, const char *para)
{
    size_t tam;
    char *d = le_arquivo(de, &tam);
    if (!d) erro("não consegui ler %s", de);
    grava_arquivo(para, d, tam);
    free(d);
}

static void copia_arvore(const char *de, const char *para, const char *const *fora)
{
    cria_pastas(para);
    DIR *d = opendir(de);
    if (!d) return;
    lista_t itens = {0};
    struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.') lista_poe(&itens, e->d_name);
    closedir(d);
    for (int i = 0; i < itens.n; i++) {
        char *o = junta(de, itens.v[i]), *n = junta(para, itens.v[i]);
        int pular = 0;
        for (int k = 0; fora && fora[k]; k++) {
            size_t a = strlen(o), b = strlen(fora[k]);
            if (a >= b && strcmp(o + a - b, fora[k]) == 0) pular = 1;
        }
        if (!pular) {
            if (eh_pasta(o)) copia_arvore(o, n, fora);
            else copia_arquivo(o, n);
        }
        free(o);
        free(n);
    }
}

static void cmd_novo(int argc, char **argv)
{
    if (argc < 1) erro("uso: nefs novo <pasta> [nome do sistema]");
    const char *pasta = argv[0];
    /* o shell do NovaEra não tem aspas: "Meu Sistema" chega em pedaços */
    texto_t tn = {0};
    for (int i = 1; i < argc; i++) { if (i > 1) tx_s(&tn, " "); tx_s(&tn, argv[i]); }
    const char *nome = argc > 1 ? tn.d : (strrchr(pasta, '/') ? strrchr(pasta, '/') + 1 : pasta);
    const char *modelo = "/usr/src/novaera";
    if (existe(pasta)) erro("'%s' já existe", pasta);
    if (!existe("/usr/src/novaera/kernel.c")) erro("o código do NovaEra não está em %s", modelo);
    static const char *const fora[] = { "/build", NULL };
    copia_arvore(modelo, pasta, fora);

    char prompt[64];
    size_t k = 0;
    for (const char *c = nome; *c && k < sizeof prompt - 1; c++) if (*c != ' ') prompt[k++] = *c;
    prompt[k] = '\0';
    texto_t t = {0};
    tx_s(&t, "# sistema.conf - a identidade do seu sistema (a \"distribuição\").\n"
             "# O nefs gera build/sistema.h e o /etc/sistema, que o novafetch lê.\n");
    tx_f(&t, "NOME=\"%s\"\nVERSAO=\"0.1\"\nLEMA=\"feito com o NEFS dentro do NovaEra\"\n"
             "PROMPT=\"%s\"\nPURO=\"nao\"\n", nome, prompt);
    char *conf = junta(pasta, "sistema.conf");
    grava_arquivo(conf, t.d, t.n);
    printf("sistema '%s' criado em %s\n", nome, pasta);
    printf("próximo passo:  nefs compilar %s\n", pasta);
}

/* --------------------------------- info ---------------------------------- */

static void cmd_info(void)
{
    char *s = le_arquivo("/etc/sistema", NULL);
    printf("NEFS dentro do NovaEra\n");
    if (s) printf("%s", s);
    static const char *const ferramentas[] = {
        "cproc", "cproc-qbe", "qbe", "nefs-cpp", "nefs-as", "nefs-ld", "nefs-ar", NULL };
    for (int i = 0; ferramentas[i]; i++) {
        char *c = junta("/bin", ferramentas[i]);
        printf("  %-10s %s\n", ferramentas[i], existe(c) ? c : "(não encontrado)");
        free(c);
    }
    printf("  código     %s\n", existe("/usr/src/novaera/kernel.c") ? "/usr/src/novaera" : "(não encontrado)");
}

static void uso(void)
{
    printf("nefs - o NEFS dentro do NovaEra\n"
           "  nefs compilar [-v] [-sem-portes] [pasta]   compila o sistema da pasta\n"
           "  nefs novo <pasta> [nome do sistema]        um sistema novo, a partir de /usr/src/novaera\n"
           "  nefs iso [-o saida.iso] [pasta]            uma ISO inicializável (depois do compilar)\n"
           "  nefs livro [capítulo]                      o Livro do NEFS\n"
           "  nefs info                                  o sistema e as ferramentas\n"
           "Exemplo:  cd /usr/src/novaera  e  nefs compilar\n");
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "ajuda") || !strcmp(argv[1], "-h")) { uso(); return 0; }
    if (!strcmp(argv[1], "compilar")) cmd_compilar(argc - 2, argv + 2);
    else if (!strcmp(argv[1], "novo")) cmd_novo(argc - 2, argv + 2);
    else if (!strcmp(argv[1], "info")) cmd_info();
    else if (!strcmp(argv[1], "iso")) cmd_iso(argc - 2, argv + 2);
    else if (!strcmp(argv[1], "livro")) cmd_livro(argc - 2, argv + 2);
    else { uso(); return 2; }
    return 0;
}
