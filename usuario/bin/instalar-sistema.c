/*
 * usuario/bin/instalar-sistema.c - copia o NovaEra que está rodando para
 * um disco montado (como o pacstrap do Arch). Só o root.
 *
 *   instalar-sistema /mnt
 *
 * Copia /bin, /etc, /usr e /boot (com o kernel.bin) e cria as pastas que
 * todo sistema tem: /home, /root, /tmp, /mnt, /disco. Depois, para o disco
 * dar boot sozinho: instalar-boot sda /mnt
 *
 * Num disco que já tem o NovaEra, serve para ATUALIZAR: os programas são
 * trocados, mas um arquivo do /etc que já existe (passwd, sombra, group,
 * hostname, montagens) fica como está.
 */
#include <stdio.h>
#include "ulib.h"

static u8  buf[ESCRITA_MAX];
static u32 arquivos, pastas, mantidos;
static int manter_existentes;            /* no /etc: a configuração de quem já instalou fica */
static u64 bytes;

static void junta(char *d, const char *a, const char *b)
{
    str_copia(d, a, CAMINHO_MAX);
    u32 n = str_tam(d);
    if (n && d[n - 1] != '/' && n + 1 < CAMINHO_MAX) d[n++] = '/';
    str_copia(d + n, b, CAMINHO_MAX - n);
}

static int copiar_arquivo(const char *de, const char *para, u32 modo)
{
    long a = arq_abrir(de, ARQ_MODO_LER);
    if (a < 0) return -1;
    long b = arq_abrir_com(para, ARQ_MODO_ESCREVER, modo & 07777);
    if (b < 0) { arq_fechar((u32)a); return -1; }
    long n;
    int r = 0;
    while ((n = ler_de(ARQ_SERVIDOR(a), (u32)a, (char *)buf, sizeof buf)) > 0) {
        if (escreve_em(ARQ_SERVIDOR(b), (u32)b, (const char *)buf, (u64)n, 0) != n) { r = -1; break; }
        bytes += (u64)n;
    }
    if (n < 0) r = -1;
    arq_fechar((u32)a);
    arq_fechar((u32)b);
    arq_permissoes(para, modo & 07777);
    arquivos++;
    return r;
}

static int copiar_pasta(const char *de, const char *para, u32 modo)
{
    if (arq_info(para, 0) != ARQ_TIPO_PASTA && arq_pasta(para) < 0) {
        fprintf(stderr, "instalar-sistema: não consegui criar %s\n", para);
        return -1;
    }
    arq_permissoes(para, modo & 07777);
    pastas++;
    int r = 0;
    char nome[MSG_TEXTO_MAX], a[CAMINHO_MAX], b[CAMINHO_MAX];
    arq_estado_t e;
    for (u32 i = 0; arq_listar_estado(de, i, nome, &e) == 0; i++) {
        junta(a, de, nome);
        junta(b, para, nome);
        if (e.tipo == ARQ_TIPO_PASTA) r |= copiar_pasta(a, b, e.modo ? e.modo : 0755);
        else if (manter_existentes && arq_info(b, 0) == ARQ_TIPO_ARQUIVO) mantidos++;
        else if (copiar_arquivo(a, b, e.modo ? e.modo : 0644) < 0) {
            fprintf(stderr, "instalar-sistema: erro ao copiar %s\n", a);
            r = -1;
        }
    }
    return r;
}

static void criar(const char *raiz, const char *nome, u32 modo)
{
    char c[CAMINHO_MAX];
    junta(c, raiz, nome);
    if (arq_info(c, 0) != ARQ_TIPO_PASTA) arq_pasta(c);
    arq_permissoes(c, modo);
}

int main(int argc, char **argv)
{
    identidade_t eu;
    sys_identidade(0, &eu);
    if (argc != 2) { fprintf(stderr, "uso: instalar-sistema PASTA   (ex.: instalar-sistema /mnt)\n"); return 1; }
    if (eu.uid != UID_ROOT) { fprintf(stderr, "instalar-sistema: precisa ser o root (use: sudo instalar-sistema ...)\n"); return 1; }
    char raiz[CAMINHO_MAX];
    if (caminho_absoluto(argv[1], raiz) < 0 || str_igual(raiz, "/")) {
        fprintf(stderr, "instalar-sistema: diga onde o disco novo está montado (ex.: /mnt)\n");
        return 1;
    }
    /* tem de ser um disco montado (não a memória) */
    char pasta[CAMINHO_MAX];
    u32 s, d, srv = 0;
    int montado = 0;
    for (u32 i = 0; montagem(i, pasta, &s, &d) == 0; i++)
        if (str_igual(pasta, raiz)) { montado = 1; srv = s; }
    if (!montado) {
        fprintf(stderr, "instalar-sistema: nada está montado em %s (formatar -t ext2 sdXN; montar sdXN %s)\n",
                raiz, raiz);
        return 1;
    }
    if (srv != PID_EXT2) printf("Aviso: o disco não é ext2; as permissões não ficam guardadas.\n");

    static const char *const copiar[] = { "/bin", "/etc", "/usr", "/boot", 0 };
    int r = 0;
    for (u32 i = 0; copiar[i]; i++) {
        if (arq_info(copiar[i], 0) != ARQ_TIPO_PASTA) continue;
        char destino[CAMINHO_MAX];
        junta(destino, raiz, copiar[i] + 1);
        printf("Copiando %s...\n", copiar[i]);
        fflush(stdout);
        manter_existentes = str_igual(copiar[i], "/etc");
        r |= copiar_pasta(copiar[i], destino, 0755);
        manter_existentes = 0;
    }
    criar(raiz, "home", 0755);
    criar(raiz, "root", 0700);
    criar(raiz, "tmp", 01777);
    criar(raiz, "mnt", 0755);
    criar(raiz, "disco", 0755);

    /* /etc/sombra: só o root lê */
    char c[CAMINHO_MAX];
    junta(c, raiz, "etc/sombra");
    arq_permissoes(c, 0600);
    /* /etc/montagens: os outros discos para montar no boot */
    junta(c, raiz, "etc/montagens");
    if (arq_info(c, 0) < 0) {
        long f = arq_abrir(c, ARQ_MODO_ESCREVER);
        const char *t = "# /etc/montagens: os discos que o init monta no boot (o /etc/fstab do Linux)\n"
                        "# dispositivo   pasta\n"
                        "# sda2          /dados\n";
        if (f >= 0) { escreve_em(ARQ_SERVIDOR(f), (u32)f, t, str_tam(t), 0); arq_fechar((u32)f); }
    }
    junta(c, raiz, "boot/kernel.bin");
    int tem_kernel = arq_info(c, 0) == ARQ_TIPO_ARQUIVO;

    printf("%s: %u pastas, %u arquivos, %lu KiB em %s.\n", r ? "Terminou com erros" : "Pronto",
           pastas, arquivos, (unsigned long)(bytes / 1024), raiz);
    if (mantidos) printf("No /etc, %u arquivos já existiam e ficaram como estavam (usuários, senhas...).\n", mantidos);
    if (!tem_kernel) printf("Aviso: não achei o /boot/kernel.bin para copiar (rode o nefs compilar).\n");
    else printf("Agora, para dar boot por este disco: instalar-boot sda %s\n", raiz);
    return r ? 1 : 0;
}
