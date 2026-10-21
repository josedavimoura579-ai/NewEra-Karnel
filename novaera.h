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
typedef unsigned long long u64;

/* ---------------------- Chamadas de sistema (int 0x80) -------------------
 * Registradores: RAX = número, RBX = 1º argumento, RCX = 2º argumento.
 * O resultado volta em RAX (negativo = erro).                              */
#define SYS_ENVIAR       0   /* (destino, msg*)  bloqueia até ser recebida  */
#define SYS_RECEBER      1   /* (origem,  msg*)  bloqueia até chegar algo   */
#define SYS_CHAMAR       2   /* (destino, msg*)  envia e espera a resposta  */
#define SYS_CEDER        3   /* ()               passa a vez na CPU          */
#define SYS_IRQ_ASSINAR  4   /* (irq)            só drivers: IRQ -> mensagem */
#define SYS_TEMPO        5   /* ()               ticks desde o boot (100 Hz) */
#define SYS_TAREFA_INFO  6   /* (pid, msg*)      nome/estado de um processo  */
#define SYS_SAIR         7   /* (código)         encerra o processo          */
#define SYS_RESPONDER    8   /* (destino, msg*)  responde a um CHAMAR; nunca bloqueia */
#define SYS_MEMORIA      9   /* ()               (KiB livres << 32) | KiB total */
#define SYS_CRIAR       10   /* (caminho, argv)  executa um programa do initrd; devolve o PID */
#define SYS_ESPERAR     11   /* (pid)            espera o filho terminar; devolve o código */
#define SYS_SBRK        12   /* (incremento)     aumenta o heap; devolve o fim anterior */
#define SYS_PID         13   /* ()               o PID de quem chama          */
#define SYS_INITRD_INFO 14   /* (índice, msg*)   texto = nome, arg[0] = tamanho de um arquivo */

#define ERRO_INVALIDO   (-1) /* argumento ou ponteiro inválido */
#define ERRO_SEM_PERMISSAO (-2)
#define ERRO_PID        (-3) /* processo inexistente ou morto */
#define ERRO_NAO_ACHADO (-4) /* arquivo não existe no initrd */
#define ERRO_SEM_MEMORIA (-5)
#define ERRO_FORMATO    (-6) /* o arquivo não é um programa ELF x86_64 válido */
#define ERRO_LOTADO     (-7) /* não há vaga para mais processos */

/* --------------------------- Processos fixos ----------------------------
 * O NovaEra ainda não tem um servidor de nomes: cada servidor tem um
 * número (PID) conhecido por todos.                                        */
#define PID_KERNEL   0       /* remetente das mensagens de IRQ   */
#define PID_VIDEO    1       /* servidor de vídeo (VGA)          */
#define PID_TECLADO  2       /* driver de teclado PS/2           */
#define PID_SHELL    3       /* shell do usuário                 */
#define PID_QUALQUER 0xFFFFFFFFu   /* em SYS_RECEBER: aceita de qualquer um */

/* ------------------------------ Mensagem --------------------------------
 * Tamanho fixo (88 bytes). O núcleo copia a mensagem inteira de um espaço
 * de endereçamento para o outro: os processos nunca veem a memória alheia. */
#define MSG_TEXTO_MAX 64u

typedef struct {
    u32  origem;                 /* preenchido PELO NÚCLEO: quem enviou */
    u32  tipo;                   /* MSG_* abaixo */
    u32  arg[4];
    char texto[MSG_TEXTO_MAX];
} msg_t;

/* Tipos de mensagem */
#define MSG_IRQ           1      /* núcleo -> driver: arg[0] = número da IRQ */
#define MSG_VIDEO_ESCREVER 10    /* arg[0] = cor, texto = string            */
#define MSG_VIDEO_LIMPAR   11
#define MSG_TECLADO_LER    20    /* resposta: arg[0] = caractere            */
#define MSG_REINICIAR      21    /* o driver do teclado controla a linha RESET */
#define MSG_OK             99

/* Estados de processo. SYS_TAREFA_INFO devolve: arg[0] = estado,
 * arg[1] = reinícios, arg[2] = exceção da última falha, arg[3] = PID do pai. */
#define ESTADO_LIVRE      0
#define ESTADO_PRONTO     1
#define ESTADO_ENVIANDO   2
#define ESTADO_RECEBENDO  3
#define ESTADO_MORTO      4      /* servidor que falhou demais e desistiu  */
#define ESTADO_ESPERANDO  5      /* em SYS_ESPERAR, aguardando um filho    */
#define ESTADO_ZUMBI      6      /* terminou; o pai ainda não leu o código */

/* Código de saída de quem foi encerrado por uma exceção: 128 + vetor
 * (o mesmo costume do Unix). Ex.: falha de página (14) = 142. */
#define SAIDA_FALHA_BASE  128

/* ------------------------- Mapa de memória virtual ----------------------
 * 0x00000000 - 0x3FFFFFFF  núcleo (1 GiB, mapeado igual à memória física,
 *                          visível SOMENTE no anel 0)
 * 0x40000000 - 0x4FFFFFFF  programa (segmentos do ELF), até 256 MiB
 * ...depois do programa    heap, cresce para cima com SYS_SBRK, até 0x60000000
 * 0x70000000 - 0x70000FFF  memória de vídeo 0xB8000 (só no servidor de vídeo)
 * 0x7FF00000 - 0x7FFFEFFF  pilha (1 MiB), cresce para baixo sob demanda;
 *                          no topo ficam os argumentos (argc, argv)       */
#define USUARIO_BASE        0x40000000ull
#define USUARIO_PROG_FIM    0x50000000ull
#define USUARIO_HEAP_FIM    0x60000000ull
#define USUARIO_VGA         0x70000000ull
#define USUARIO_PILHA_TOPO  0x7FFFF000ull
#define USUARIO_PILHA_TAM   0x00100000ull
#define USUARIO_FIM         USUARIO_PILHA_TOPO

#define ARGV_MAX       16u   /* argumentos por programa      */
#define ARGV_BYTES   1024u   /* soma do tamanho dos textos   */

#endif
