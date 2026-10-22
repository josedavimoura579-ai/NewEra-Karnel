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

/* A versão do NÚCLEO. A do sistema (a "distribuição") fica em sistema.conf,
 * como o /etc/os-release do Linux: o núcleo não sabe qual é a distro. */
#define NOVAERA_VERSAO     "3.2"
#define NOVAERA_ARQUITETURA "x86_64"

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
#define SYS_CRIAR       10   /* (criar_t*)       cria um processo a partir de um ELF; devolve o PID */
#define SYS_ESPERAR     11   /* (pid)            espera o filho terminar; devolve o código */
#define SYS_SBRK        12   /* (incremento)     aumenta o heap; devolve o fim anterior */
#define SYS_PID         13   /* ()               o PID de quem chama          */
#define SYS_INITRD      14   /* ()               só o servidor de arquivos: tamanho do initrd */
#define SYS_COPIAR_PARA 15   /* (pid, copia_t*)  copia da minha memória para a de quem me CHAMOU */
#define SYS_COPIAR_DE   16   /* (pid, copia_t*)  copia da memória de quem me CHAMOU para a minha */
#define SYS_AMBIENTE    17   /* (msg*)           entrada, saída padrão e pasta atual */
#define SYS_PASTA       18   /* (caminho)        muda a pasta atual          */
#define SYS_NUCLEO      19   /* (msg*)           texto = "NovaEra 3.2 x86_64" (como o uname) */

#define ERRO_INVALIDO   (-1) /* argumento ou ponteiro inválido */
#define ERRO_SEM_PERMISSAO (-2)
#define ERRO_PID        (-3) /* processo inexistente ou morto */
#define ERRO_NAO_ACHADO (-4) /* arquivo ou pasta não existe */
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
#define PID_ARQUIVOS 4       /* servidor de arquivos (ramfs)     */
#define PID_TERMINAL 5       /* terminal: entrada e saída padrão */
#define PID_TUBOS    6       /* pipes                            */
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
#define MSG_TECLA          20    /* teclado -> terminal: arg[0] = caractere */
#define MSG_REINICIAR      21    /* o driver do teclado controla a linha RESET */

/* Entrada e saída: o MESMO protocolo no terminal, nos pipes e nos arquivos.
 * Um "canal" diz qual pipe ou qual arquivo aberto (no terminal, sempre 0). */
#define MSG_ESCREVER       30    /* arg[0] = canal, arg[1] = cor, arg[2] = bytes, texto = dados;
                                    resposta: arg[0] = bytes escritos       */
#define MSG_LER            31    /* arg[0] = canal, arg[1] = máximo, arg[2] = endereço do
                                    buffer; resposta: arg[0] = bytes (0 = fim).
                                    Os endereços do usuário ficam abaixo de
                                    2 GiB: cabem num u32 da mensagem.      */

/* Servidor de arquivos: caminhos ABSOLUTOS em texto */
#define ARQ_INFO           40    /* resposta: arg[0] = tipo (ARQ_TIPO_*), arg[1] = tamanho */
#define ARQ_LISTAR         41    /* arg[0] = índice; resposta: texto = nome, arg[0] = tipo, arg[1] = tamanho */
#define ARQ_ABRIR          42    /* arg[0] = modo (ARQ_MODO_*); resposta: arg[0] = canal */
#define ARQ_FECHAR         43    /* arg[0] = canal */
#define ARQ_PASTA          44    /* cria uma pasta */
#define ARQ_APAGAR         45    /* apaga um arquivo ou uma pasta vazia */
#define ARQ_TIPO_ARQUIVO   1
#define ARQ_TIPO_PASTA     2
#define ARQ_MODO_LER       1
#define ARQ_MODO_ESCREVER  2     /* cria ou apaga o conteúdo */
#define ARQ_MODO_ANEXAR    3     /* cria ou escreve no fim  */

/* Pipes. Cada ponta (leitura, escrita) conta quantas referências abertas
 * ela tem. Sem escritores, quem lê recebe o fim; sem leitores, quem escreve
 * recebe erro; sem nenhum dos dois, o tubo é liberado. */
#define TUBO_CRIAR         50    /* resposta: arg[0] = canal (1 leitor e 1 escritor) */
#define TUBO_ABRIR         51    /* arg[0] = canal, arg[1] = TUBO_LEITURA ou TUBO_ESCRITA */
#define TUBO_FECHAR        52    /* arg[0] = canal, arg[1] = a ponta       */
#define TUBO_LEITURA       0
#define TUBO_ESCRITA       1
/* Num canal de pipe passado a um filho, este bit diz: "a referência é sua,
 * feche ao terminar" (a libc faz isso no exit). O shell não liga o bit:
 * ele mesmo fecha as pontas depois que os filhos terminam. */
#define CANAL_DONO         0x80000000u

#define MSG_OK             99
#define MSG_ERRO           98    /* arg[0] = -ERRO_* (o código, positivo)   */

/* SYS_COPIAR_PARA / SYS_COPIAR_DE: um servidor copia dados direto da/para
 * a memória de um cliente, mas só enquanto o cliente espera a resposta
 * dele (está num SYS_CHAMAR). Assim um arquivo de 1 MiB não precisa
 * viajar em milhares de mensagens de 64 bytes. */
typedef struct {
    u64 meu;                     /* endereço na memória de quem chama a syscall */
    u64 dele;                    /* endereço na memória do cliente */
    u64 bytes;
} copia_t;

/* SYS_CRIAR: o programa vem pronto na memória de quem chama (lido do
 * servidor de arquivos). entrada/saida = 0: herda as de quem cria. */
typedef struct {
    u64  imagem;                 /* o ELF inteiro */
    u64  tamanho;
    u64  argv;                   /* char *argv[], terminado em 0 */
    u32  entrada, entrada_canal; /* servidor e canal da entrada padrão */
    u32  saida, saida_canal;     /* servidor e canal da saída padrão  */
} criar_t;

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
 * 0x68000000 - ...         o initrd, só leitura (só no servidor de arquivos)
 * 0x70000000 - 0x70000FFF  memória de vídeo 0xB8000 (só no servidor de vídeo)
 * 0x7FF00000 - 0x7FFFEFFF  pilha (1 MiB), cresce para baixo sob demanda;
 *                          no topo ficam os argumentos (argc, argv)       */
#define USUARIO_BASE        0x40000000ull
#define USUARIO_PROG_FIM    0x50000000ull
#define USUARIO_HEAP_FIM    0x60000000ull
#define USUARIO_INITRD      0x68000000ull
#define USUARIO_VGA         0x70000000ull
#define USUARIO_PILHA_TOPO  0x7FFFF000ull
#define USUARIO_PILHA_TAM   0x00100000ull
#define USUARIO_FIM         USUARIO_PILHA_TOPO

#define ARGV_MAX      256u   /* argumentos por programa (o cproc passa ~50) */
#define ARGV_BYTES  16384u   /* soma do tamanho dos textos   */
#define CAMINHO_MAX    64u   /* caminho de arquivo, com o '\0' (cabe numa mensagem) */

#endif
