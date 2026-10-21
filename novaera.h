/*
 * ===========================================================================
 *  NovaEra Microkernel - novaera.h
 *  A "ABI" do sistema: o contrato entre o núcleo e os programas do usuário.
 *  É o único arquivo compartilhado pelos dois lados. Sem libc, sem nada.
 *
 *  Num microkernel o núcleo só faz quatro coisas:
 *    1. separar a memória dos processos (paginação),
 *    2. dividir o tempo da CPU entre eles (escalonador),
 *    3. deixar que conversem trocando mensagens (IPC),
 *    4. avisar os drivers quando o hardware interrompe (IRQ vira mensagem).
 *  Todo o resto (vídeo, teclado, shell...) roda FORA dele, no anel 3.
 * ===========================================================================
 */
#ifndef NOVAERA_H
#define NOVAERA_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* ---------------------- Chamadas de sistema (int 0x80) -------------------
 * Registradores: EAX = número, EBX = 1º argumento, ECX = 2º argumento.
 * O resultado volta em EAX (negativo = erro).                              */
#define SYS_ENVIAR       0   /* (destino, msg*)  bloqueia até ser recebida  */
#define SYS_RECEBER      1   /* (origem,  msg*)  bloqueia até chegar algo   */
#define SYS_CHAMAR       2   /* (destino, msg*)  envia e espera a resposta  */
#define SYS_CEDER        3   /* ()               passa a vez na CPU          */
#define SYS_IRQ_ASSINAR  4   /* (irq)            só drivers: IRQ -> mensagem */
#define SYS_TEMPO        5   /* ()               ticks desde o boot (100 Hz) */
#define SYS_TAREFA_INFO  6   /* (pid, msg*)      nome/estado de um processo  */
#define SYS_SAIR         7   /* ()               encerra o processo          */
#define SYS_RESPONDER    8   /* (destino, msg*)  responde a um CHAMAR; nunca bloqueia */
#define SYS_MEMORIA      9   /* ()               (quadros livres << 16) | total */

#define ERRO_INVALIDO   (-1) /* argumento ou ponteiro inválido */
#define ERRO_SEM_PERMISSAO (-2)
#define ERRO_PID        (-3) /* processo inexistente ou morto */

/* --------------------------- Processos fixos ----------------------------
 * O NovaEra ainda não tem um servidor de nomes: cada servidor tem um
 * número (PID) conhecido por todos.                                        */
#define PID_KERNEL   0       /* remetente das mensagens de IRQ   */
#define PID_VIDEO    1       /* servidor de vídeo (VGA)          */
#define PID_TECLADO  2       /* driver de teclado PS/2           */
#define PID_SHELL    3       /* shell do usuário                 */
#define PID_QUALQUER 0xFFFFFFFFu   /* em SYS_RECEBER: aceita de qualquer um */

/* ------------------------------ Mensagem --------------------------------
 * Tamanho fixo (80 bytes). O núcleo copia a mensagem inteira de um espaço
 * de endereçamento para o outro: os processos nunca veem a memória alheia. */
#define MSG_TEXTO_MAX 64u

typedef struct {
    u32  origem;                 /* preenchido PELO NÚCLEO: quem enviou */
    u32  tipo;                   /* MSG_* abaixo */
    u32  arg[2];
    char texto[MSG_TEXTO_MAX];
} msg_t;

/* Tipos de mensagem */
#define MSG_IRQ           1      /* núcleo -> driver: arg[0] = número da IRQ */
#define MSG_VIDEO_ESCREVER 10    /* arg[0] = cor, texto = string            */
#define MSG_VIDEO_LIMPAR   11
#define MSG_TECLADO_LER    20    /* resposta: arg[0] = caractere            */
#define MSG_REINICIAR      21    /* o driver do teclado controla a linha RESET */
#define MSG_OK             99

/* Estados de processo (usados por SYS_TAREFA_INFO em arg[0]) */
#define ESTADO_LIVRE      0
#define ESTADO_PRONTO     1
#define ESTADO_ENVIANDO   2
#define ESTADO_RECEBENDO  3
#define ESTADO_MORTO      4

/* ------------------------- Mapa de memória virtual ----------------------
 * 0x00000000 - 0x00FFFFFF  núcleo (16 MiB, mapeado igual à memória física,
 *                          visível SOMENTE no anel 0)
 * 0x40000000 - 0x4000FFFF  programa do usuário (código + dados, 64 KiB)
 * 0x40100000 - 0x40103FFF  pilha do usuário (16 KiB)
 * 0x40200000 - 0x40200FFF  memória de vídeo 0xB8000 (só no servidor de vídeo) */
#define USUARIO_BASE        0x40000000u
#define USUARIO_IMG_TAM     0x00010000u
#define USUARIO_PILHA_BASE  0x40100000u
#define USUARIO_PILHA_TAM   0x00004000u
#define USUARIO_VGA         0x40200000u

#endif
