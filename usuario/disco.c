/*
 * ===========================================================================
 *  NovaEra - usuario/disco.c   (PID 7, anel 3)
 *  Driver de disco ATA (IDE): o primeiro disco do primeiro canal, como o
 *  "-drive file=disco.img,format=raw" do QEMU ou um HD antigo.
 *
 *  Fala com o disco pelas portas 0x1F0-0x1F7 no modo PIO: o programa pede
 *  os setores e copia cada um, 256 palavras de 16 bits, pela porta de
 *  dados. É o jeito mais simples (e mais lento) de usar um disco, mas
 *  funciona em qualquer PC e não precisa de interrupção: o driver espera
 *  o disco ficar pronto lendo a porta de estado.
 *
 *  Pedidos (novaera.h): DISCO_INFO, DISCO_LER e DISCO_ESCREVER, até 128
 *  setores de 512 bytes por vez. Os dados vão e vêm direto da memória de
 *  quem pediu (SYS_COPIAR_PARA / SYS_COPIAR_DE).
 * ===========================================================================
 */
#include "ulib.h"

void ins16(u16 porta, void *buf, u64 n);         /* sys.s */
void outs16(u16 porta, const void *buf, u64 n);

#define ATA_DADOS     0x1F0
#define ATA_CONTA     0x1F2     /* quantos setores */
#define ATA_LBA0      0x1F3
#define ATA_LBA1      0x1F4
#define ATA_LBA2      0x1F5
#define ATA_DRIVE     0x1F6     /* 0xE0 = disco 0, endereço LBA (+ bits 24-27) */
#define ATA_COMANDO   0x1F7     /* escrevendo */
#define ATA_ESTADO    0x1F7     /* lendo */
#define ATA_CONTROLE  0x3F6

#define EST_ERRO      0x01
#define EST_DRQ       0x08      /* o disco quer (ou tem) dados */
#define EST_FALHA     0x20
#define EST_OCUPADO   0x80

#define CMD_LER       0x20
#define CMD_ESCREVER  0x30
#define CMD_ESVAZIAR  0xE7      /* grava o cache do disco */
#define CMD_IDENTIFICA 0xEC

#define MAX_SETORES   128u

static u32 setores;                              /* 0 = sem disco */
static u16 buf[MAX_SETORES * 256];               /* 64 KiB */

/* ~400 ns: o tempo do disco trocar o registrador de estado. */
static void espera_um_pouco(void)
{
    for (int i = 0; i < 4; i++) inb(ATA_CONTROLE);
}

/* Espera o disco largar o "ocupado". Com 'dados', espera também o DRQ. */
static int espera(int dados)
{
    for (u32 n = 0; n < 50000000u; n++) {
        u8 e = inb(ATA_ESTADO);
        if (e == 0xFF) return -1;                    /* ninguém no barramento */
        if (e & EST_OCUPADO) continue;
        if (e & (EST_ERRO | EST_FALHA)) return -1;
        if (!dados || (e & EST_DRQ)) return 0;
    }
    return -1;                                       /* demorou demais */
}

static void endereco(u32 lba, u32 n)
{
    outb(ATA_DRIVE, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    espera_um_pouco();
    outb(ATA_CONTA, (u8)n);                          /* 0 = 256 */
    outb(ATA_LBA0, (u8)lba);
    outb(ATA_LBA1, (u8)(lba >> 8));
    outb(ATA_LBA2, (u8)(lba >> 16));
}

static void identificar(void)
{
    outb(ATA_CONTROLE, 0x02);                        /* sem interrupções (nIEN) */
    outb(ATA_DRIVE, 0xA0);
    espera_um_pouco();
    outb(ATA_CONTA, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMANDO, CMD_IDENTIFICA);
    u8 e = inb(ATA_ESTADO);
    if (e == 0 || e == 0xFF) return;                 /* não há disco */
    while (inb(ATA_ESTADO) & EST_OCUPADO) { }
    if (inb(ATA_LBA1) || inb(ATA_LBA2)) return;      /* é um CD (ATAPI), não um disco */
    if (espera(1) < 0) return;
    ins16(ATA_DADOS, buf, 256);
    setores = (u32)buf[60] | (u32)buf[61] << 16;     /* endereçáveis em LBA28 */
}

static int ata_ler(u32 lba, u32 n)
{
    if (espera(0) < 0) return -1;
    endereco(lba, n);
    outb(ATA_COMANDO, CMD_LER);
    for (u32 i = 0; i < n; i++) {
        if (espera(1) < 0) return -1;
        ins16(ATA_DADOS, buf + 256 * i, 256);
    }
    return 0;
}

static int ata_escrever(u32 lba, u32 n)
{
    if (espera(0) < 0) return -1;
    endereco(lba, n);
    outb(ATA_COMANDO, CMD_ESCREVER);
    for (u32 i = 0; i < n; i++) {
        if (espera(1) < 0) return -1;
        outs16(ATA_DADOS, buf + 256 * i, 256);
    }
    if (espera(0) < 0) return -1;
    outb(ATA_COMANDO, CMD_ESVAZIAR);
    return espera(0);
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
    identificar();

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        u32 quem = m.origem;
        if (m.tipo == DISCO_INFO) { responder(quem, 0, setores); continue; }
        if (m.tipo != DISCO_LER && m.tipo != DISCO_ESCREVER) { responder(quem, ERRO_INVALIDO, 0); continue; }

        u32 lba = m.arg[0], n = m.arg[1], ender = m.arg[2];
        if (!setores) { responder(quem, ERRO_NAO_ACHADO, 0); continue; }
        if (n == 0 || n > MAX_SETORES || lba >= setores || n > setores - lba || lba >= (1u << 28)) {
            responder(quem, ERRO_INVALIDO, 0);
            continue;
        }
        u64 bytes = (u64)n * DISCO_SETOR;
        if (m.tipo == DISCO_LER) {
            if (ata_ler(lba, n) < 0) { responder(quem, ERRO_DISCO, 0); continue; }
            if (sys_copiar_para(quem, buf, ender, bytes) < 0) { responder(quem, ERRO_INVALIDO, 0); continue; }
        } else {
            if (sys_copiar_de(quem, buf, ender, bytes) < 0) { responder(quem, ERRO_INVALIDO, 0); continue; }
            if (ata_escrever(lba, n) < 0) { responder(quem, ERRO_DISCO, 0); continue; }
        }
        responder(quem, 0, n);
    }
}
