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
#define NOVAERA_VERSAO     "3.7"
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
#define SYS_NUCLEO      19   /* (msg*)           texto = "NovaEra 3.7 x86_64" (como o uname) */
#define SYS_SINAL       20   /* (pid, sinal)     envia um sinal; pid < 0: ao grupo -pid; 0: ao meu grupo */
#define SYS_SINAL_TRATAR 21  /* (sinal, sinal_acao_t*) troca o tratador; devolve o antigo na mesma struct */
#define SYS_SINAL_VOLTAR 22  /* ()               fim do tratador: volta ao ponto interrompido */
#define SYS_SINAL_MASCARA 23 /* (como, mascara)  bloqueia/desbloqueia sinais; devolve a máscara antiga */
#define SYS_GRUPO       24   /* (pid, grupo)     grupo de processos; grupo = -1: só consulta */
#define SYS_IDENTIDADE  25   /* (pid, identidade_t*) quem é o processo (0 = eu) */
#define SYS_IDENTIDADE_MUDAR 26 /* (pid, identidade_t*) só o root: muda a de um filho,
                                de um cliente esperando a minha resposta, ou a minha */
#define SYS_LINHA_BOOT  27   /* (msg*)           texto = a linha de comando que o GRUB passou
                                ("/boot/kernel.bin root=sda1"); vazio se não houve */

#define ERRO_INVALIDO   (-1) /* argumento ou ponteiro inválido */
#define ERRO_SEM_PERMISSAO (-2)
#define ERRO_PID        (-3) /* processo inexistente ou morto */
#define ERRO_NAO_ACHADO (-4) /* arquivo ou pasta não existe */
#define ERRO_SEM_MEMORIA (-5)
#define ERRO_FORMATO    (-6) /* o arquivo não é um programa ELF x86_64 válido */
#define ERRO_LOTADO     (-7) /* não há vaga para mais processos */
#define ERRO_EXISTE     (-8) /* o arquivo ou a pasta já existe */
#define ERRO_DISCO      (-9) /* erro de leitura ou escrita no disco */
#define ERRO_SEM_ESPACO (-10) /* o disco está cheio */
#define ERRO_INTERROMPIDO (-11) /* a espera foi interrompida por um sinal (o EINTR) */
#define ERRO_TUBO       (-12) /* escrever num pipe que ninguém mais lê (o EPIPE) */
#define ERRO_OCUPADO    (-13) /* em uso (desmontar com arquivos abertos: o EBUSY) */

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
#define PID_DISCO    7       /* driver de disco (ATA)            */
#define PID_FAT      8       /* os discos FAT32 montados         */
#define PID_EXT2     9       /* os discos ext2 montados          */
#define PID_CONTAS   10      /* usuários e senhas (login, su, sudo, passwd) */
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
#define MSG_REINICIAR      21    /* o driver do teclado controla a linha RESET (só o root) */
#define MSG_DESLIGAR       22    /* desliga a máquina (ACPI do QEMU/VirtualBox; só o root) */

/* Entrada e saída: o MESMO protocolo no terminal, nos pipes e nos arquivos.
 * Um "canal" diz qual pipe ou qual arquivo aberto (no terminal, sempre 0). */
#define MSG_ESCREVER       30    /* arg[0] = canal, arg[1] = cor, arg[2] = bytes, texto = dados;
                                    resposta: arg[0] = bytes escritos.
                                    Nos servidores de arquivos, arg[3] != 0
                                    é o endereço dos dados em quem chamou
                                    (até ESCRITA_MAX bytes de uma vez):
                                    o servidor copia com SYS_COPIAR_DE.     */
#define ESCRITA_MAX     65536u
#define MSG_LER            31    /* arg[0] = canal, arg[1] = máximo, arg[2] = endereço do
                                    buffer; resposta: arg[0] = bytes (0 = fim).
                                    Os endereços do usuário ficam abaixo de
                                    2 GiB: cabem num u32 da mensagem.      */

/* Servidor de arquivos: caminhos ABSOLUTOS em texto */
#define ARQ_INFO           40    /* resposta: arg[0] = tipo (ARQ_TIPO_*), arg[1] = tamanho */
#define ARQ_LISTAR         41    /* arg[0] = índice; resposta: texto = nome, arg[0] = tipo, arg[1] = tamanho */
#define ARQ_ABRIR          42    /* arg[0] = modo (ARQ_MODO_*), arg[1] = permissões se criar (0 = 0644);
                                    resposta: arg[0] = canal */
#define ARQ_PERMISSOES     55    /* arg[0] = o modo novo (o chmod): só o dono ou o root */
#define ARQ_DONO           56    /* arg[0] = uid, arg[1] = gid (o chown): só o root */
#define ARQ_FECHAR         43    /* arg[0] = canal */
#define ARQ_PASTA          44    /* cria uma pasta; arg[0] = modo (0 = 0755) */
#define ARQ_APAGAR         45    /* apaga um arquivo ou uma pasta vazia */
/* ARQ_INFO e ARQ_LISTAR: no ext2, arg[2] = o modo (permissões, como no
 * Unix: 0644, 0755...) e arg[3] = dono | grupo << 16. No FAT32: 0. */
#define ARQ_TIPO_ARQUIVO   1
#define ARQ_TIPO_PASTA     2
#define ARQ_MODO_LER       1
#define ARQ_MODO_ESCREVER  2     /* cria ou apaga o conteúdo */
#define ARQ_MODO_ANEXAR    3     /* cria ou escreve no fim  */
/* Montagens (como o /etc/fstab e o mount do Linux). O servidor de
 * arquivos (PID_ARQUIVOS) é a raiz e guarda a tabela: um pedido com um
 * caminho que fica dentro de um ponto de montagem (o mais longo vence)
 * é respondido com ARQ_OUTRO_SERVIDOR, e a ulib refaz o pedido lá. O canal
 * que a ulib entrega leva o servidor nos bits 8-15 (ARQ_SERVIDOR); o
 * servidor só olha os bits 0-7. */
#define ARQ_OUTRO_SERVIDOR 46    /* resposta: arg[0] = o servidor que cuida do caminho */
#define MONT_ADICIONAR     47    /* texto = pasta, arg[0] = servidor, arg[1] = dispositivo */
#define MONT_REMOVER       48    /* texto = pasta; resposta: arg[0] = servidor, arg[1] = dispositivo */
#define MONT_LISTAR        49    /* arg[0] = índice; resposta: texto = pasta, arg[0] = servidor, arg[1] = dispositivo */
#define MONT_MAX           8u
#define ARQ_DISCO          "/disco"    /* onde o boot monta o primeiro FAT32 que achar */
#define ARQ_SERVIDOR(c)    (((u32)(c) >> 8) & 0xFFu)
#define ARQ_CANAL(c)       ((u32)(c) & 0xFFu)
/* Quem guarda arquivos (e não é o terminal nem um pipe)? */
#define SERVIDOR_DE_ARQUIVOS(s) ((s) != PID_TERMINAL && (s) != PID_TUBOS)

/* Driver de disco (ATA: até 4 discos, nos dois canais IDE; setores de 512
 * bytes, até 128 por pedido). Um DISPOSITIVO é disco * 8 + partição:
 * partição 0 é o disco inteiro, 1 a 4 são as primárias da tabela MBR. Os
 * nomes são os do Linux: sda = 0, sda1 = 1, sdb = 8, sdb2 = 10...
 * O LBA de cada pedido conta a partir do começo do dispositivo. */
#define DISCO_INFO         60    /* arg[3] = dispositivo; resposta: arg[0] = setores (0 = não existe),
                                    arg[1] = onde começa no disco, arg[2] = tipo da partição (MBR),
                                    texto = o modelo do disco */
#define DISCO_LER          61    /* arg[0] = setor (LBA), arg[1] = quantos, arg[2] = endereço do buffer,
                                    arg[3] = dispositivo */
#define DISCO_ESCREVER     62    /* idem, os dados vêm do buffer de quem chamou */
#define DISCO_RELER        64    /* arg[3] = dispositivo: lê a tabela de partições de novo */
#define DISCO_SETOR        512u
#define DISCOS_MAX         4u
#define DISCO_DEV(d, p)    ((d) * 8u + (p))
#define DEV_DISCO(dev)     ((dev) / 8u)
#define DEV_PARTICAO(dev)  ((dev) % 8u)

/* Servidores de sistemas de arquivos (FAT32, ext2): cada um cuida de até
 * 4 volumes montados ao mesmo tempo. Quem monta pergunta a um por vez: o
 * que não reconhece o disco responde ERRO_FORMATO. */
#define SA_MONTAR          63    /* arg[0] = dispositivo, texto = pasta */
#define SA_DESMONTAR       65    /* texto = pasta; ERRO_OCUPADO se há arquivos abertos */
#define FAT_MONTAR         SA_MONTAR
#define FAT_DESMONTAR      SA_DESMONTAR

/* Tipos de partição (o byte 4 da entrada na MBR) */
#define PART_FAT32         0x0C  /* FAT32 com LBA */
#define PART_LINUX         0x83  /* ext2 (e qualquer sistema de arquivos do Linux) */
#define PART_SWAP          0x82

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

/* O terminal manda o Ctrl+C (SIGINT) ao grupo do "primeiro plano": o
 * comando que o shell está rodando. 0 = nenhum (o shell espera a linha). */
#define TERM_GRUPO         32    /* arg[0] = grupo do primeiro plano; resposta: arg[0] = o anterior */
#define TERM_ECO           33    /* arg[0] = 0: não mostra o que se digita (senhas); 1: mostra */

/* Servidor de contas (PID_CONTAS). texto = "usuario\0senha\0" (até 31
 * letras cada). Os arquivos: /etc/passwd, /etc/group e /etc/sombra. */
#define CONTAS_ENTRAR      70    /* vira o usuário (login, su); o root não precisa da senha.
                                    resposta: arg[0] = uid, arg[1] = gid, texto = a pasta pessoal */
#define CONTAS_SUDO        71    /* vira o root; texto = a senha de quem pede (que tem de estar
                                    no grupo sudo ou wheel) */
#define CONTAS_CONFERIR    72    /* só confere a senha (o passwd, antes de trocar) */
#define CONTAS_SENHA       73    /* troca a senha: o root, ou o próprio usuário logo depois
                                    de um CONTAS_CONFERIR */

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
    u32  grupo;                  /* 0 = o de quem cria; GRUPO_NOVO = um grupo só dele */
    u32  reservado;
} criar_t;
#define GRUPO_NOVO 0xFFFFFFFFu

/* ------------------------- Usuários e grupos -----------------------------
 * Cada processo é de um usuário (uid) e de um grupo (gid), e pode estar em
 * mais alguns grupos. O root (uid 0) pode tudo. Um filho herda a
 * identidade de quem o cria; só o root a muda (SYS_IDENTIDADE_MUDAR). É o
 * servidor de contas, que roda como root, quem muda a identidade de um
 * programa depois de conferir a senha (login, su, sudo).
 * Os servidores de arquivos perguntam ao núcleo quem é o cliente
 * (SYS_IDENTIDADE) e conferem as permissões rwx de cada arquivo. */
#define GRUPOS_MAX 8u
typedef struct {
    u32 uid, gid;
    u32 n;                       /* quantos grupos extras */
    u32 grupos[GRUPOS_MAX];
} identidade_t;
#define UID_ROOT 0u

/* Permissões (como no Unix): 3 grupos de 3 bits, dono / grupo / outros */
#define PERM_LER       4u
#define PERM_ESCREVER  2u
#define PERM_EXECUTAR  1u        /* num arquivo: rodar; numa pasta: entrar */
#define MODO_STICKY    01000u    /* numa pasta (/tmp): só o dono apaga o que é seu */

/* ------------------------------- Sinais ---------------------------------
 * Um sinal é um aviso assíncrono a um processo: "pare" (Ctrl+C), "morra",
 * "seu filho terminou", "você leu memória que não é sua". Os números são
 * os do Linux (o "kill -9" é o mesmo em toda parte).
 *
 * Cada sinal tem um destino: o padrão (quase todos encerram o processo;
 * SIGCHLD, SIGCONT, SIGURG e SIGWINCH são ignorados), ignorar, ou uma
 * função do programa (o tratador). O tratador roda no anel 3, na pilha do
 * programa; quando ele volta, cai no "restaurador" (um int 0x80 com
 * SYS_SINAL_VOLTAR) e o núcleo devolve tudo como estava.
 *
 * Quem é encerrado por um sinal sai com o código SAIDA_SINAL(s); um código
 * normal (SYS_SAIR) vai de 0 a 255, como no Unix. */
#define SINAL_HUP     1
#define SINAL_INT     2      /* Ctrl+C */
#define SINAL_QUIT    3
#define SINAL_ILL     4      /* instrução inválida */
#define SINAL_TRAP    5
#define SINAL_ABRT    6      /* abort() */
#define SINAL_BUS     7
#define SINAL_FPE     8      /* divisão por zero */
#define SINAL_KILL    9      /* não pode ser tratado nem ignorado */
#define SINAL_USR1   10
#define SINAL_SEGV   11      /* memória inválida (falha de página, GPF) */
#define SINAL_USR2   12
#define SINAL_PIPE   13      /* escreveu num pipe sem leitor */
#define SINAL_ALRM   14
#define SINAL_TERM   15      /* o padrão do "kill" */
#define SINAL_CHLD   17      /* um filho terminou (ignorado por padrão) */
#define SINAL_CONT   18
#define SINAL_STOP   19      /* (ainda sem controle de tarefas: ignorados) */
#define SINAL_TSTP   20
#define SINAL_TTIN   21
#define SINAL_TTOU   22
#define SINAL_URG    23
#define SINAL_WINCH  28
#define SINAL_MAX    32      /* sinais de 1 a 31 */

#define SINAL_PADRAO  0ull   /* tratador = 0: a ação padrão */
#define SINAL_IGNORAR 1ull   /* tratador = 1: ignorar        */

typedef struct {
    u64  tratador;           /* SINAL_PADRAO, SINAL_IGNORAR ou o endereço da função */
    u64  restaurador;        /* para onde o tratador volta (o __sinal_volta da ulib) */
    u32  mascara;            /* sinais bloqueados enquanto o tratador roda */
    u32  mudar;              /* 0 = só consulta; a resposta vem na mesma struct */
} sinal_acao_t;

#define SINAL_BLOQUEAR    0  /* SYS_SINAL_MASCARA: como o SIG_BLOCK   */
#define SINAL_DESBLOQUEAR 1  /*                    SIG_UNBLOCK         */
#define SINAL_DEFINIR     2  /*                    SIG_SETMASK         */

#define SAIDA_SINAL(s)     (0x100 | (s))
#define SAIDA_FOI_SINAL(c) (((c) & 0x100) != 0)

/* Estados de processo. SYS_TAREFA_INFO devolve: arg[0] = estado,
 * arg[1] = reinícios, arg[2] = exceção da última falha, arg[3] = PID do pai. */
#define ESTADO_LIVRE      0
#define ESTADO_PRONTO     1
#define ESTADO_ENVIANDO   2
#define ESTADO_RECEBENDO  3
#define ESTADO_MORTO      4      /* servidor que falhou demais e desistiu  */
#define ESTADO_ESPERANDO  5      /* em SYS_ESPERAR, aguardando um filho    */
#define ESTADO_ZUMBI      6      /* terminou; o pai ainda não leu o código */

/* Uma exceção da CPU num programa vira um sinal: falha de página e GPF
 * viram SIGSEGV, divisão por zero vira SIGFPE, opcode inválido vira
 * SIGILL. Sem tratador, o processo sai com SAIDA_SINAL(sinal), e
 * SYS_TAREFA_INFO guarda o vetor em arg[2] (FALHA_NENHUMA se não houve). */
#define FALHA_NENHUMA     0xFFFFFFFFu

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
