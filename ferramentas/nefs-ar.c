/*
 * ===========================================================================
 *  NEFS - ferramentas/nefs-ar.c
 *  Junta objetos (.o) numa biblioteca (.a), no formato "ar" do Unix, para
 *  o nefs-ld puxar só os objetos de que um programa precisa.
 *
 *    nefs-ar rcs biblioteca.a objetos.o...
 *
 *  As letras "rcs" existem para ser compatível com o ar do Unix; o nefs-ar
 *  sempre cria a biblioteca do zero, com o índice de símbolos ("/") que o
 *  ld do binutils e o ld.lld também exigem.
 *
 *  Formato: "!<arch>\n"; para cada membro, um cabeçalho de 60 bytes em
 *  texto (nome/, data, dono, grupo, modo, tamanho, "`\n") e os dados,
 *  completados até um tamanho par.
 *
 *  Escrito em C padrão: compila no Linux e dentro do próprio NovaEra.
 * ===========================================================================
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint32_t u32;
typedef uint64_t u64;

static void erro(const char *msg, const char *detalhe)
{
    fprintf(stderr, "nefs-ar: %s%s%s\n", msg, detalhe ? ": " : "", detalhe ? detalhe : "");
    exit(1);
}

static u64 le16(const u8 *p) { return (u64)p[0] | (u64)p[1] << 8; }
static u64 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }
static u64 le64(const u8 *p) { return le32(p) | le32(p + 4) << 32; }

static u8 *le_arquivo(const char *nome, size_t *tam)
{
    FILE *f = fopen(nome, "rb");
    if (!f) erro("não consegui abrir", nome);
    size_t cap = 65536, n = 0, k;
    u8 *d = malloc(cap);
    if (!d) erro("sem memória", 0);
    while ((k = fread(d + n, 1, cap - n, f)) > 0) {
        n += k;
        if (n == cap) {
            cap *= 2;
            d = realloc(d, cap);
            if (!d) erro("sem memória", 0);
        }
    }
    fclose(f);
    *tam = n;
    return d;
}

typedef struct {
    const char *nome;      /* sem a pasta */
    u8   *d;
    size_t tam;
    u64   pos;             /* onde o cabeçalho do membro fica no .a */
} membro_t;

/* Os símbolos globais definidos num objeto: chama f(nome) para cada um. */
static void simbolos(const membro_t *m, void (*f)(const char *, u32), u32 arg)
{
    const u8 *d = m->d;
    if (m->tam < 64 || memcmp(d, "\177ELF", 4) != 0 || d[4] != 2) erro("não é um objeto ELF de 64 bits", m->nome);
    u64 shoff = le64(d + 40), nsec = le16(d + 60);
    for (u64 i = 0; i < nsec; i++) {
        const u8 *h = d + shoff + i * 64;
        if (le32(h + 4) != 2) continue;               /* SHT_SYMTAB */
        const u8 *strs = d + le64(d + shoff + le32(h + 40) * 64 + 24);
        const u8 *tab = d + le64(h + 24);
        u64 n = le64(h + 32) / 24;
        for (u64 k = 1; k < n; k++) {
            const u8 *s = tab + k * 24;
            int ligacao = s[4] >> 4;
            u64 shndx = le16(s + 6);
            if (ligacao == 0 || shndx == 0) continue; /* local ou indefinido */
            f((const char *)strs + le32(s), arg);
        }
    }
}

static u32 nsimb, tam_nomes;
static void conta(const char *nome, u32 arg) { (void)arg; nsimb++; tam_nomes += (u32)strlen(nome) + 1; }

static u8 *indice;
static u32 pos_ofs, pos_nome;
static void anota(const char *nome, u32 pos)
{
    u8 *p = indice + 4 + 4 * pos_ofs++;
    p[0] = (u8)(pos >> 24); p[1] = (u8)(pos >> 16); p[2] = (u8)(pos >> 8); p[3] = (u8)pos;
    strcpy((char *)indice + 4 + 4 * nsimb + pos_nome, nome);
    pos_nome += (u32)strlen(nome) + 1;
}

static void cabecalho(FILE *f, const char *nome, u64 tam)
{
    char h[128];
    snprintf(h, sizeof h, "%-16s%-12s%-6s%-6s%-8s%-10llu`\n", nome, "0", "0", "0", "644",
             (unsigned long long)tam);
    fwrite(h, 1, 60, f);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: nefs-ar rcs biblioteca.a objetos.o...\n");
        return 1;
    }
    int a = 1;
    if (strspn(argv[1], "rcsu") == strlen(argv[1])) a = 2;  /* letras do ar */
    const char *saida = argv[a++];
    int n = argc - a;
    membro_t *m = calloc((size_t)(n ? n : 1), sizeof *m);
    if (!m) erro("sem memória", 0);

    /* nomes de até 15 letras ficam no cabeçalho; os maiores, no "//" */
    char *longos = calloc(1, 1);
    size_t tam_longos = 0;
    for (int i = 0; i < n; i++) {
        const char *b = strrchr(argv[a + i], '/');
        m[i].nome = b ? b + 1 : argv[a + i];
        m[i].d = le_arquivo(argv[a + i], &m[i].tam);
        if (strlen(m[i].nome) > 15) {
            size_t k = strlen(m[i].nome) + 2;
            longos = realloc(longos, tam_longos + k + 1);
            if (!longos) erro("sem memória", 0);
            sprintf(longos + tam_longos, "%s/\n", m[i].nome);
            tam_longos += k;
        }
    }

    /* índice: número de símbolos, a posição do membro de cada um, os nomes */
    for (int i = 0; i < n; i++) simbolos(&m[i], conta, 0);
    u64 tam_indice = 4 + 4 * (u64)nsimb + tam_nomes;
    u64 pos = 8 + 60 + tam_indice + (tam_indice & 1);
    if (tam_longos) pos += 60 + tam_longos + (tam_longos & 1);
    for (int i = 0; i < n; i++) {
        m[i].pos = pos;
        pos += 60 + m[i].tam + (m[i].tam & 1);
    }
    indice = calloc(1, (size_t)tam_indice + 1);
    if (!indice) erro("sem memória", 0);
    indice[0] = (u8)(nsimb >> 24); indice[1] = (u8)(nsimb >> 16);
    indice[2] = (u8)(nsimb >> 8);  indice[3] = (u8)nsimb;
    for (int i = 0; i < n; i++) simbolos(&m[i], anota, (u32)m[i].pos);

    FILE *f = fopen(saida, "wb");
    if (!f) erro("não consegui criar", saida);
    fwrite("!<arch>\n", 1, 8, f);
    cabecalho(f, "/", tam_indice);
    fwrite(indice, 1, (size_t)tam_indice, f);
    if (tam_indice & 1) fputc('\n', f);
    if (tam_longos) {
        cabecalho(f, "//", tam_longos);
        fwrite(longos, 1, tam_longos, f);
        if (tam_longos & 1) fputc('\n', f);
    }
    size_t ofs_longo = 0;
    for (int i = 0; i < n; i++) {
        char nome[32];
        if (strlen(m[i].nome) > 15) {
            snprintf(nome, sizeof nome, "/%lu", (unsigned long)ofs_longo);
            ofs_longo += strlen(m[i].nome) + 2;
        } else snprintf(nome, sizeof nome, "%s/", m[i].nome);
        cabecalho(f, nome, m[i].tam);
        fwrite(m[i].d, 1, m[i].tam, f);
        if (m[i].tam & 1) fputc('\n', f);
    }
    if (fclose(f) != 0) erro("erro ao gravar", saida);
    return 0;
}
