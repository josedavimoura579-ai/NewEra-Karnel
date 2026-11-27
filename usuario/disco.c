/*
 * ===========================================================================
 *  NovaEra - usuario/disco.c   (PID 7, anel 3)
 *  Driver de disco ATA (IDE): até quatro discos, o mestre e o escravo de
 *  cada um dos dois canais, como os "-drive ...,index=0..3" do QEMU ou os
 *  HDs de um PC antigo. Os nomes são os do Linux: sda, sdb, sdc e sdd.
 *
 *  Fala com cada disco pelas portas do canal (0x1F0-0x1F7 o primeiro,
 *  0x170-0x177 o segundo) no modo PIO: o programa pede os setores e copia
 *  cada um, 256 palavras de 16 bits, pela porta de dados. É o jeito mais
 *  simples (e mais lento) de usar um disco, mas funciona em qualquer PC e
 *  não precisa de interrupção: o driver espera o disco ficar pronto lendo
 *  a porta de estado.
 *
 *  Partições: no boot (e em DISCO_RELER) o driver lê a tabela MBR do setor
 *  0 de cada disco: até 4 partições primárias, sda1 a sda4. Quem pede um
 *  setor de sda1 conta a partir do começo dela; o driver soma o início e
 *  confere o fim, e assim um sistema de arquivos não consegue escrever fora
 *  da sua partição.
 *
 *  Pedidos (novaera.h): DISCO_INFO, DISCO_LER, DISCO_ESCREVER e DISCO_RELER,
 *  até 128 setores de 512 bytes por vez. Os dados vão e vêm direto da
 *  memória de quem pediu (SYS_COPIAR_PARA / SYS_COPIAR_DE).
 * ===========================================================================
 */
#include "ulib.h"

void ins16(u16 porta, void *buf, u64 n);         /* sys.s */
void outs16(u16 porta, const void *buf, u64 n);

/* Os registradores, a partir da porta base do canal */
#define R_DADOS      0
#define R_CONTA      2          /* quantos setores */
#define R_LBA0       3
#define R_LBA1       4
#define R_LBA2       5
#define R_DRIVE      6          /* 0xE0 = mestre, 0xF0 = escravo, endereço LBA (+ bits 24-27) */
#define R_COMANDO    7          /* escrevendo */
#define R_ESTADO     7          /* lendo */

#define EST_ERRO      0x01
#define EST_DRQ       0x08      /* o disco quer (ou tem) dados */
#define EST_FALHA     0x20
#define EST_OCUPADO   0x80

#define CMD_LER       0x20
#define CMD_ESCREVER  0x30
#define CMD_ESVAZIAR  0xE7      /* grava o cache do disco */
#define CMD_IDENTIFICA 0xEC

#define MAX_SETORES   128u
#define PARTICOES     4u

static const u16 BASE[2]     = { 0x1F0, 0x170 };
static const u16 CONTROLE[2] = { 0x3F6, 0x376 };

typedef struct {
    u32  setores;               /* 0 = não há disco */
    char modelo[41];
    struct { u32 ini, tam; u8 tipo; } part[PARTICOES + 1];   /* [0] = o disco inteiro */
} disco_t;

static disco_t discos[DISCOS_MAX];
static u16 buf[MAX_SETORES * 256];               /* 64 KiB */

static u16 base_de(u32 d) { return BASE[d / 2]; }

/* ~400 ns: o tempo do disco trocar o registrador de estado. */
static void espera_um_pouco(u32 d)
{
    for (int i = 0; i < 4; i++) inb(CONTROLE[d / 2]);
}

/* Espera o disco largar o "ocupado". Com 'dados', espera também o DRQ. */
static int espera(u32 d, int dados)
{
    for (u32 n = 0; n < 50000000u; n++) {
        u8 e = inb((u16)(base_de(d) + R_ESTADO));
        if (e == 0xFF) return -1;                    /* ninguém no barramento */
        if (e & EST_OCUPADO) continue;
        if (e & (EST_ERRO | EST_FALHA)) return -1;
        if (!dados || (e & EST_DRQ)) return 0;
    }
    return -1;                                       /* demorou demais */
}

static void selecionar(u32 d, u32 lba_alto)
{
    outb((u16)(base_de(d) + R_DRIVE), (u8)(0xE0 | ((d & 1) << 4) | (lba_alto & 0x0F)));
    espera_um_pouco(d);
}

static void endereco(u32 d, u32 lba, u32 n)
{
    u16 b = base_de(d);
    selecionar(d, lba >> 24);
    outb((u16)(b + R_CONTA), (u8)n);                 /* 0 = 256 */
    outb((u16)(b + R_LBA0), (u8)lba);
    outb((u16)(b + R_LBA1), (u8)(lba >> 8));
    outb((u16)(b + R_LBA2), (u8)(lba >> 16));
}

static int ata_ler(u32 d, u32 lba, u32 n)
{
    selecionar(d, lba >> 24);
    if (espera(d, 0) < 0) return -1;
    endereco(d, lba, n);
    outb((u16)(base_de(d) + R_COMANDO), CMD_LER);
    for (u32 i = 0; i < n; i++) {
        if (espera(d, 1) < 0) return -1;
        ins16(base_de(d) + R_DADOS, buf + 256 * i, 256);
    }
    return 0;
}

static int ata_escrever(u32 d, u32 lba, u32 n)
{
    selecionar(d, lba >> 24);
    if (espera(d, 0) < 0) return -1;
    endereco(d, lba, n);
    outb((u16)(base_de(d) + R_COMANDO), CMD_ESCREVER);
    for (u32 i = 0; i < n; i++) {
        if (espera(d, 1) < 0) return -1;
        outs16(base_de(d) + R_DADOS, buf + 256 * i, 256);
    }
    if (espera(d, 0) < 0) return -1;
    outb((u16)(base_de(d) + R_COMANDO), CMD_ESVAZIAR);
    return espera(d, 0);
}

/* IDENTIFY: o disco existe? Quantos setores? Qual o modelo? */
static void identificar(u32 d)
{
    u16 b = base_de(d);
    disco_t *x = &discos[d];
    x->setores = 0;
    outb(CONTROLE[d / 2], 0x02);                     /* sem interrupções (nIEN) */
    outb((u16)(b + R_DRIVE), (u8)(0xA0 | ((d & 1) << 4)));
    espera_um_pouco(d);
    outb((u16)(b + R_CONTA), 0);
    outb((u16)(b + R_LBA0), 0);
    outb((u16)(b + R_LBA1), 0);
    outb((u16)(b + R_LBA2), 0);
    outb((u16)(b + R_COMANDO), CMD_IDENTIFICA);
    u8 e = inb((u16)(b + R_ESTADO));
    if (e == 0 || e == 0xFF) return;                 /* não há disco */
    for (u32 n = 0; inb((u16)(b + R_ESTADO)) & EST_OCUPADO; n++)
        if (n > 50000000u) return;
    if (inb((u16)(b + R_LBA1)) || inb((u16)(b + R_LBA2))) return;   /* um CD (ATAPI) */
    if (espera(d, 1) < 0) return;
    ins16(b + R_DADOS, buf, 256);
    x->setores = (u32)buf[60] | (u32)buf[61] << 16;  /* endereçáveis em LBA28 */
    /* o modelo: palavras 27 a 46, cada uma com os dois bytes trocados */
    for (u32 i = 0; i < 20; i++) {
        x->modelo[2 * i]     = (char)(buf[27 + i] >> 8);
        x->modelo[2 * i + 1] = (char)buf[27 + i];
    }
    u32 n = 40;
    while (n && x->modelo[n - 1] == ' ') n--;
    x->modelo[n] = '\0';
}

static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

/* A tabela MBR: 4 entradas de 16 bytes no byte 446, assinatura 55 AA no
 * fim. Um disco formatado inteiro, sem partições (como um pendrive), tem
 * um setor de boot de FAT no lugar: aí não há tabela. */
static void ler_particoes(u32 d)
{
    disco_t *x = &discos[d];
    for (u32 k = 0; k <= PARTICOES; k++) x->part[k].ini = x->part[k].tam = x->part[k].tipo = 0;
    if (!x->setores) return;
    x->part[0].tam = x->setores;
    if (ata_ler(d, 0, 1) < 0) return;
    const u8 *s = (const u8 *)buf;
    if (s[510] != 0x55 || s[511] != 0xAA) return;
    int fat_inteiro = (s[0] == 0xEB || s[0] == 0xE9) && s[11] == 0 && s[12] == 2 &&
                      (mem_igual(s + 82, "FAT32", 5) || mem_igual(s + 54, "FAT", 3));
    if (fat_inteiro) return;
    for (u32 k = 0; k < PARTICOES; k++) {
        const u8 *e = s + 446 + 16 * k;
        u32 ini = le32(e + 8), tam = le32(e + 12);
        if (!e[4] || !tam || ini == 0 || ini >= x->setores || tam > x->setores - ini) continue;
        if (e[0] != 0 && e[0] != 0x80) continue;     /* entrada estragada */
        x->part[k + 1].ini  = ini;
        x->part[k + 1].tam  = tam;
        x->part[k + 1].tipo = e[4];
    }
}

static void responder(u32 para, long erro, u32 a0)
{
    msg_t r;
    msg_zera(&r);
    r.tipo = erro ? MSG_ERRO : MSG_OK;
    r.arg[0] = erro ? (u32)(-erro) : a0;
    sys_responder(para, &r);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (u32 d = 0; d < DISCOS_MAX; d++) {
        identificar(d);
        ler_particoes(d);
    }

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        u32 quem = m.origem, dev = m.arg[3];
        u32 d = DEV_DISCO(dev), p = DEV_PARTICAO(dev);
        int existe = d < DISCOS_MAX && p <= PARTICOES && discos[d].part[p].tam;

        if (m.tipo == DISCO_INFO) {
            msg_t r;
            msg_zera(&r);
            r.tipo = MSG_OK;
            if (existe) {
                r.arg[0] = discos[d].part[p].tam;
                r.arg[1] = discos[d].part[p].ini;
                r.arg[2] = discos[d].part[p].tipo;
                str_copia(r.texto, discos[d].modelo, MSG_TEXTO_MAX);
            }
            sys_responder(quem, &r);
            continue;
        }
        /* ler e gravar setores direto passa por cima das permissões dos
         * arquivos: só o root (os servidores de arquivos, o formatar...) */
        identidade_t id;
        if (sys_identidade(quem, &id) < 0 || id.uid != UID_ROOT) {
            responder(quem, ERRO_SEM_PERMISSAO, 0);
            continue;
        }
        if (m.tipo == DISCO_RELER) {
            if (d >= DISCOS_MAX || !discos[d].setores) { responder(quem, ERRO_NAO_ACHADO, 0); continue; }
            ler_particoes(d);
            responder(quem, 0, 0);
            continue;
        }
        if (m.tipo != DISCO_LER && m.tipo != DISCO_ESCREVER) { responder(quem, ERRO_INVALIDO, 0); continue; }
        if (!existe) { responder(quem, ERRO_NAO_ACHADO, 0); continue; }

        u32 lba = m.arg[0], n = m.arg[1], ender = m.arg[2];
        u32 tam = discos[d].part[p].tam;
        if (n == 0 || n > MAX_SETORES || lba >= tam || n > tam - lba) {
            responder(quem, ERRO_INVALIDO, 0);
            continue;
        }
        lba += discos[d].part[p].ini;
        if (lba >= (1u << 28)) { responder(quem, ERRO_INVALIDO, 0); continue; }
        u64 bytes = (u64)n * DISCO_SETOR;
        if (m.tipo == DISCO_LER) {
            if (ata_ler(d, lba, n) < 0) { responder(quem, ERRO_DISCO, 0); continue; }
            if (sys_copiar_para(quem, buf, ender, bytes) < 0) { responder(quem, ERRO_INVALIDO, 0); continue; }
        } else {
            if (sys_copiar_de(quem, buf, ender, bytes) < 0) { responder(quem, ERRO_INVALIDO, 0); continue; }
            if (ata_escrever(d, lba, n) < 0) { responder(quem, ERRO_DISCO, 0); continue; }
        }
        responder(quem, 0, n);
    }
}
