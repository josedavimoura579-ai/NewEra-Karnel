/*
 * usuario/bin/instalar-boot.c - põe o GRUB no disco, para ele dar boot
 * sozinho (como o grub-install do Linux). Só o root.
 *
 *   instalar-boot sda /mnt
 *
 * sda é o disco (o GRUB vai para o começo dele) e /mnt é onde a partição
 * com o sistema está montada (com o /boot/kernel.bin: instalar-sistema).
 *
 * Um disco que dá boot, por dentro:
 *   setor 0         a MBR: 440 bytes de código (o boot.img do GRUB), a
 *                   tabela de partições e a assinatura 55 AA. A BIOS
 *                   carrega este setor e pula para ele.
 *   setores 1-2047  o resto do GRUB (disco.img), no espaço livre antes da
 *                   1ª partição. O boot.img sabe que ele começa no setor 1.
 *   a partição      o GRUB a acha (procurando o /boot/grub/grub.cfg), lê o
 *                   grub.cfg e carrega o /boot/kernel.bin com "root=sda1".
 */
#include <stdio.h>
#include <string.h>
#include "ulib.h"

#define SETOR 512u

static u8 mbr[SETOR];

static int disco_es(u32 tipo, u32 dev, u32 lba, u32 n, void *buf)
{
    while (n) {
        u32 k = n > 64 ? 64 : n;
        msg_t m;
        msg_zera(&m);
        m.tipo = tipo;
        m.arg[0] = lba;
        m.arg[1] = k;
        m.arg[2] = (u32)(u64)buf;
        m.arg[3] = dev;
        if (sys_chamar(PID_DISCO, &m) < 0 || m.tipo != MSG_OK) return -1;
        lba += k;
        n -= k;
        buf = (u8 *)buf + k * SETOR;
    }
    return 0;
}

static int gravar(const char *caminho, const char *texto)
{
    long f = arq_abrir(caminho, ARQ_MODO_ESCREVER);
    if (f < 0) return -1;
    long r = escreve_em(ARQ_SERVIDOR(f), (u32)f, texto, str_tam(texto), 0);
    arq_fechar((u32)f);
    return r < 0 ? -1 : 0;
}

int main(int argc, char **argv)
{
    identidade_t eu;
    sys_identidade(0, &eu);
    if (argc != 3) { fprintf(stderr, "uso: instalar-boot DISCO PASTA   (ex.: instalar-boot sda /mnt)\n"); return 1; }
    if (eu.uid != UID_ROOT) { fprintf(stderr, "instalar-boot: precisa ser o root (use: sudo instalar-boot ...)\n"); return 1; }

    long disco = dispositivo_de(argv[1]);
    msg_t m;
    if (disco < 0 || DEV_PARTICAO((u32)disco) || !disco_info((u32)disco, &m)) {
        fprintf(stderr, "instalar-boot: %s não é um disco (sda, sdb...; veja: discos)\n", argv[1]);
        return 1;
    }
    char raiz[CAMINHO_MAX], pasta[CAMINHO_MAX], nome[8];
    caminho_absoluto(argv[2], raiz);
    u32 srv, dev, part = 0;
    for (u32 i = 0; montagem(i, pasta, &srv, &dev) == 0; i++)
        if (str_igual(pasta, raiz)) part = dev;
    if (!part || DEV_DISCO(part) != DEV_DISCO((u32)disco) || !DEV_PARTICAO(part)) {
        fprintf(stderr, "instalar-boot: em %s tem de estar montada uma partição de %s\n", raiz, argv[1]);
        return 1;
    }
    char c[CAMINHO_MAX];
    str_copia(c, raiz, sizeof c);
    str_copia(c + str_tam(c), "/boot/kernel.bin", sizeof c - str_tam(c));
    if (arq_info(c, 0) != ARQ_TIPO_ARQUIVO) {
        fprintf(stderr, "instalar-boot: não achei %s (antes: instalar-sistema %s)\n", c, raiz);
        return 1;
    }

    /* o GRUB: as duas partes vêm prontas no sistema */
    u32 tam_boot, tam_core;
    u8 *boot = arq_carregar("/usr/lib/nefs/grub/boot.img", &tam_boot);
    u8 *core = arq_carregar("/usr/lib/nefs/grub/disco.img", &tam_core);
    if (!boot || !core || tam_boot != SETOR) {
        fprintf(stderr, "instalar-boot: não achei o GRUB em /usr/lib/nefs/grub\n");
        return 1;
    }
    u32 setores_core = (tam_core + SETOR - 1) / SETOR;

    if (disco_es(DISCO_LER, (u32)disco, 0, 1, mbr) < 0 || mbr[510] != 0x55 || mbr[511] != 0xAA) {
        fprintf(stderr, "instalar-boot: %s não tem tabela de partições (particionar %s)\n", argv[1], argv[1]);
        return 1;
    }
    u32 primeira = 0xFFFFFFFFu;                      /* o GRUB tem de caber antes dela */
    for (u32 k = 0; k < 4; k++) {
        const u8 *e = mbr + 446 + 16 * k;
        u32 ini = (u32)e[8] | (u32)e[9] << 8 | (u32)e[10] << 16 | (u32)e[11] << 24;
        if (e[4] && ini < primeira) primeira = ini;
    }
    if (1 + setores_core > primeira) {
        fprintf(stderr, "instalar-boot: o GRUB (%u setores) não cabe antes da 1ª partição (setor %u)\n",
                setores_core, primeira);
        return 1;
    }

    /* a MBR nova: o código do GRUB e, do byte 440 em diante, a tabela que já
     * estava (a assinatura do disco, as partições e o 55 AA) */
    memcpy(mbr, boot, 440);
    mbr[0x66] = 0x90;                                /* disco rígido: sem o teste de disquete */
    mbr[0x67] = 0x90;
    static u8 resto[2047 * SETOR];
    memset(resto, 0, sizeof resto);
    memcpy(resto, core, tam_core);
    if (disco_es(DISCO_ESCREVER, (u32)disco, 1, setores_core, resto) < 0 ||
        disco_es(DISCO_ESCREVER, (u32)disco, 0, 1, mbr) < 0) {
        fprintf(stderr, "instalar-boot: erro ao gravar no disco\n");
        return 1;
    }

    /* o grub.cfg, na partição */
    str_copia(c, raiz, sizeof c);
    str_copia(c + str_tam(c), "/boot/grub", sizeof c - str_tam(c));
    if (arq_info(c, 0) != ARQ_TIPO_PASTA) arq_pasta(c);
    str_copia(c + str_tam(c), "/grub.cfg", sizeof c - str_tam(c));
    char cfg[512];
    dispositivo_nome(part, nome);
    snprintf(cfg, sizeof cfg,
             "# /boot/grub/grub.cfg: o menu do GRUB (instalar-boot)\n"
             "set timeout=3\n"
             "menuentry \"NovaEra (%s)\" {\n"
             "    multiboot /boot/kernel.bin root=%s\n"
             "}\n", nome, nome);
    if (gravar(c, cfg) < 0) { fprintf(stderr, "instalar-boot: não consegui gravar %s\n", c); return 1; }

    printf("GRUB instalado em %s (%u setores); o sistema está em %s.\n", argv[1], setores_core, nome);
    printf("Desligue e ligue de novo pelo disco (no QEMU: sem -kernel, -initrd e -cdrom).\n");
    return 0;
}
