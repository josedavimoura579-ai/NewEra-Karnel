/*
 * usuario/bin/particionar.c - a tabela de partições (MBR) de um disco:
 * o fdisk do NovaEra.
 *
 *   particionar sdb                     mostra a tabela
 *   particionar sdb criar 100M          nova partição FAT32 de 100 MiB
 *   particionar sdb criar resto linux   nova partição com o resto do disco
 *   particionar sdb apagar 2            apaga a partição sdb2
 *   particionar sdb boot 1              marca sdb1 como a de dar boot
 *   particionar sdb limpar              tabela nova, vazia (APAGA TUDO)
 *
 * Tamanhos: 512K, 100M, 2G ou "resto". Tipos: fat32 (o padrão), linux
 * (para o ext2), swap, ou o número do tipo em hexadecimal (0c, 83...).
 *
 * A MBR é o setor 0 do disco: 446 bytes de código de boot, 4 entradas de
 * 16 bytes (as partições primárias) e a assinatura 55 AA. Cada entrada diz
 * onde a partição começa (LBA) e quantos setores tem. As partições começam
 * alinhadas em 1 MiB (setor 2048), como o fdisk do Linux faz.
 *
 * Não mexe num disco que tem algo montado: desmonte antes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ulib.h"

#define SETOR     512u
#define ALINHA    2048u              /* 1 MiB */

static u8  mbr[SETOR];
static u32 dev, setores;
static char nome[8];

static u32  le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void poe32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static u8  *entrada(int k) { return mbr + 446 + 16 * k; }

static int disco_es(u32 tipo)
{
    msg_t m;
    msg_zera(&m);
    m.tipo   = tipo;
    m.arg[0] = 0;
    m.arg[1] = 1;
    m.arg[2] = (u32)(u64)mbr;
    m.arg[3] = dev;
    return sys_chamar(PID_DISCO, &m) < 0 || m.tipo != MSG_OK ? -1 : 0;
}

static int tem_tabela(void) { return mbr[510] == 0x55 && mbr[511] == 0xAA; }

static int fat_inteiro(void)
{
    return tem_tabela() && (mbr[0] == 0xEB || mbr[0] == 0xE9) && mbr[11] == 0 && mbr[12] == 2 &&
           (!memcmp(mbr + 82, "FAT32", 5) || !memcmp(mbr + 54, "FAT", 3));
}

static const char *tipo_nome(u32 t)
{
    switch (t) {
    case 0x0B: case PART_FAT32: return "FAT32";
    case 0x06: case 0x0E:       return "FAT16";
    case PART_LINUX:            return "Linux (ext2)";
    case PART_SWAP:             return "swap";
    case 0x05: case 0x0F:       return "estendida (não suportada)";
    case 0xEE:                  return "GPT (não suportada)";
    }
    return "?";
}

static void mostrar(void)
{
    msg_t m;
    disco_info(dev, &m);
    printf("Disco %s: %lu MiB (%u setores), %s\n", nome, (unsigned long)(setores / 2048), setores, m.texto);
    if (fat_inteiro()) {
        printf("Sem tabela de partições: o disco inteiro é um FAT32 (como um pendrive).\n");
        printf("Para particionar: particionar %s limpar\n", nome);
        return;
    }
    if (!tem_tabela()) {
        printf("Sem tabela de partições (disco novo). Crie uma: particionar %s criar 100M\n", nome);
        return;
    }
    int n = 0;
    for (int k = 0; k < 4; k++) {
        u8 *e = entrada(k);
        if (!e[4]) continue;
        if (!n++) printf("Dispositivo  Boot  Inicio     Fim        Tamanho    Tipo\n");
        u32 ini = le32(e + 8), tam = le32(e + 12);
        char t[16];
        snprintf(t, sizeof t, "%lu MiB", (unsigned long)(tam / 2048));
        printf("%s%-8d  %-4s  %-9u  %-9u  %-9s  %02x %s\n", nome, k + 1, e[0] == 0x80 ? "*" : "",
               ini, ini + tam - 1, t, e[4], tipo_nome(e[4]));
    }
    if (!n) printf("A tabela está vazia. Crie uma partição: particionar %s criar 100M\n", nome);
}

/* CHS (cilindro, cabeça, setor) para BIOS antigas: 255 cabeças, 63 setores
 * por trilha; depois de 1023 cilindros, o valor "não cabe" (FE FF FF). */
static void chs(u8 *p, u32 lba)
{
    u32 c = lba / (255 * 63), h = (lba / 63) % 255, s = lba % 63 + 1;
    if (c > 1023) { p[0] = 0xFE; p[1] = 0xFF; p[2] = 0xFF; return; }
    p[0] = (u8)h;
    p[1] = (u8)(s | ((c >> 2) & 0xC0));
    p[2] = (u8)c;
}

static void tabela_nova(void)
{
    memset(mbr, 0, sizeof mbr);
    poe32(mbr + 440, 0x4E450000u ^ (sys_tempo() * 2654435761u));   /* a "assinatura do disco" */
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
}

static long tamanho_de(const char *t, u32 livre)
{
    if (!strcmp(t, "resto")) return (long)livre;
    char *fim;
    unsigned long v = strtoul(t, &fim, 10);
    if (fim == t || !v) return -1;
    unsigned long mult = 2048;                       /* sem letra: MiB */
    if (*fim == 'K' || *fim == 'k') mult = 2;
    else if (*fim == 'M' || *fim == 'm') mult = 2048;
    else if (*fim == 'G' || *fim == 'g') mult = 2048ul * 1024;
    else if (*fim) return -1;
    unsigned long s = v * mult;
    if (s > livre) return -2;
    return (long)s;
}

static int tipo_de(const char *t)
{
    if (!strcmp(t, "fat32") || !strcmp(t, "fat")) return PART_FAT32;
    if (!strcmp(t, "linux") || !strcmp(t, "ext2")) return PART_LINUX;
    if (!strcmp(t, "swap")) return PART_SWAP;
    char *fim;
    long v = strtol(t, &fim, 16);
    return *fim || v <= 0 || v > 255 ? -1 : (int)v;
}

static int gravar(void)
{
    if (disco_es(DISCO_ESCREVER) < 0) { fprintf(stderr, "particionar: erro ao gravar no disco\n"); return 1; }
    msg_t m;
    msg_zera(&m);
    m.tipo   = DISCO_RELER;                          /* o driver lê a tabela nova */
    m.arg[3] = dev;
    sys_chamar(PID_DISCO, &m);
    mostrar();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: particionar DISCO [criar TAMANHO [TIPO] | apagar N | boot N | limpar]\n"
                        "ex.: particionar sdb criar 100M      (veja os discos com: discos)\n");
        return 1;
    }
    long d = dispositivo_de(argv[1]);
    msg_t m;
    if (d < 0 || DEV_PARTICAO((u32)d) || !(setores = disco_info((u32)d, &m))) {
        fprintf(stderr, "particionar: %s não é um disco (sda, sdb, sdc ou sdd; veja: discos)\n", argv[1]);
        return 1;
    }
    dev = (u32)d;
    dispositivo_nome(dev, nome);
    if (disco_es(DISCO_LER) < 0) { fprintf(stderr, "particionar: erro ao ler o disco\n"); return 1; }
    if (argc == 2) { mostrar(); return 0; }

    /* nada montado neste disco */
    char pasta[CAMINHO_MAX];
    for (u32 p = 0; p <= 4; p++) {
        if (montado_em(DISCO_DEV(DEV_DISCO(dev), p), pasta)) {
            fprintf(stderr, "particionar: %s está montado em %s (desmonte antes: desmontar %s)\n",
                    dispositivo_nome(DISCO_DEV(DEV_DISCO(dev), p), nome), pasta, pasta);
            return 1;
        }
    }

    const char *cmd = argv[2];
    if (!strcmp(cmd, "limpar")) {
        if (argc < 4 || strcmp(argv[3], "-sim")) {
            printf("Isto APAGA a tabela de partições de %s (e tudo o que havia nele). Digite sim: ", nome);
            fflush(stdout);
            char r[16];
            if (!fgets(r, sizeof r, stdin) || strncmp(r, "sim", 3)) { printf("Nada foi feito.\n"); return 1; }
        }
        tabela_nova();
        return gravar();
    }

    if (fat_inteiro()) {
        fprintf(stderr, "particionar: %s está formatado inteiro, sem tabela. Antes: particionar %s limpar\n",
                nome, nome);
        return 1;
    }
    if (!tem_tabela()) tabela_nova();

    if (!strcmp(cmd, "criar")) {
        if (argc < 4) { fprintf(stderr, "uso: particionar %s criar TAMANHO [TIPO]\n", nome); return 1; }
        int livre_k = -1;
        u32 ini = ALINHA;
        for (int k = 0; k < 4; k++) {
            u8 *e = entrada(k);
            if (!e[4]) { if (livre_k < 0) livre_k = k; continue; }
            u32 fim = le32(e + 8) + le32(e + 12);
            if (fim > ini) ini = fim;
        }
        if (livre_k < 0) { fprintf(stderr, "particionar: já há 4 partições (o máximo da MBR)\n"); return 1; }
        ini = (ini + ALINHA - 1) / ALINHA * ALINHA;
        if (ini >= setores) { fprintf(stderr, "particionar: não há espaço livre no fim do disco\n"); return 1; }
        long tam = tamanho_de(argv[3], setores - ini);
        if (tam == -2) {
            fprintf(stderr, "particionar: não cabe: há %lu MiB livres\n", (unsigned long)((setores - ini) / 2048));
            return 1;
        }
        if (tam < 0) { fprintf(stderr, "particionar: tamanho inválido: %s (ex.: 100M, 2G, resto)\n", argv[3]); return 1; }
        int tipo = argc > 4 ? tipo_de(argv[4]) : PART_FAT32;
        if (tipo < 0) { fprintf(stderr, "particionar: tipo desconhecido: %s (fat32, linux, swap)\n", argv[4]); return 1; }
        u8 *e = entrada(livre_k);
        memset(e, 0, 16);
        chs(e + 1, ini);
        e[4] = (u8)tipo;
        chs(e + 5, ini + (u32)tam - 1);
        poe32(e + 8, ini);
        poe32(e + 12, (u32)tam);
        printf("Criada %s%d: %lu MiB, %s.\n", nome, livre_k + 1, (unsigned long)(tam / 2048), tipo_nome((u32)tipo));
        return gravar();
    }

    if (!strcmp(cmd, "apagar") || !strcmp(cmd, "boot")) {
        int k = argc > 3 ? atoi(argv[3]) - 1 : -1;
        if (k < 0 || k > 3 || !entrada(k)[4]) {
            fprintf(stderr, "particionar: não existe a partição %s%s\n", nome, argc > 3 ? argv[3] : "?");
            return 1;
        }
        if (!strcmp(cmd, "apagar")) memset(entrada(k), 0, 16);
        else for (int i = 0; i < 4; i++) entrada(i)[0] = i == k ? 0x80 : 0;
        return gravar();
    }

    fprintf(stderr, "particionar: não conheço '%s' (criar, apagar, boot, limpar)\n", cmd);
    return 1;
}
