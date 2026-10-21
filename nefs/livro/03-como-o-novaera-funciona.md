# 3. Como o NovaEra funciona

O NovaEra é um **microkernel**. O núcleo (anel 0, o modo mais poderoso
da CPU) faz só quatro coisas:

1. **separa a memória** de cada processo (paginação);
2. **divide o tempo da CPU** entre eles (escalonador);
3. **leva mensagens** de um processo para outro (IPC);
4. **avisa os drivers** quando o hardware interrompe (IRQ vira mensagem).

Todo o resto roda no **anel 3**, sem poder nenhum sobre o hardware nem
sobre a memória alheia:

| PID | Servidor | O que faz |
|---|---|---|
| 1 | `video` | o único com a memória de vídeo; mostra o texto (e os acentos) |
| 2 | `teclado` | o driver PS/2: transforma a IRQ 1 em teclas e as entrega ao terminal |
| 3 | `shell` | lê comandos e executa programas |
| 4 | `arquivos` | pastas e arquivos na memória; monta `/bin` e `/etc` a partir do initrd |
| 5 | `terminal` | o TTY: edita a linha, mostra o eco, é a entrada e a saída padrão |
| 6 | `tubos` | os pipes (`ls /bin \| cat`) |

Os comandos (`ls`, `cat`, `novafetch`...) são programas comuns em `/bin`.
Para executar um, o shell lê o arquivo do servidor de arquivos e entrega
os bytes ao núcleo, que cria o processo. **O núcleo não sabe o que é um
arquivo.**

## Ordem de leitura

Leia nesta ordem. Os arquivos são comentados em português.

1. `novaera.h` — chamadas de sistema, formato das mensagens, PIDs.
2. `linker.ld` — onde o núcleo mora na memória (a partir de 1 MiB).
3. `boot.asm` — do boot em 32 bits até o modo de 64 bits.
4. `kernel.c` — memória, paginação, processos, escalonador e IPC.
5. `usuario/` — os servidores: `video.c`, `teclado.c`, `shell.c`,
   `arquivos.c`, `terminal.c` e `tubos.c`; e `usuario/bin/`, os comandos.

Use `less kernel.c` para ler e `/palavra` para procurar.

## A vida de um boot

1. O GRUB (ou o QEMU) carrega o `kernel.bin` em 1 MiB e pula para
   `_start`, em `boot.asm`, com a CPU em 32 bits.
2. `boot.asm` monta as primeiras tabelas de página, liga o modo de
   64 bits e chama `kernel_main()`.
3. `kernel_main()` prepara a memória, a IDT, o PIC e o timer, e cria um
   processo para cada linha de `programas.lista`.
4. A primeira interrupção do timer chama o escalonador, e os processos
   começam a rodar.

## Uma mensagem viajando

Quando um programa escreve `escreve("oi\n", COR(COR_BRANCO))`, ele não
toca na memória de vídeo. A `ulib` monta uma mensagem para a **saída
padrão** do programa, que normalmente é o terminal:

```c
msg_t m;
msg_zera(&m);
m.tipo   = MSG_ESCREVER;
m.arg[0] = canal;                  /* qual pipe ou arquivo (no terminal, 0) */
m.arg[1] = COR(COR_BRANCO);
m.arg[2] = 3;                      /* quantos bytes */
/* ... copia "oi\n" para m.texto ... */
sys_chamar(PID_TERMINAL, &m);      /* envia e espera a resposta */
```

O núcleo copia a mensagem para o terminal, que a repassa ao servidor de
vídeo, o único processo com a memória VGA mapeada. Nenhum deles enxerga
a memória do outro. Se o shell ligar a saída do programa a um pipe ou a
um arquivo (`ls > /tmp/lista`), a mesma mensagem vai para o servidor de
pipes ou de arquivos, e o programa nem percebe a diferença.

## Quando algo dá errado

Digite `invadir` no shell do seu sistema. O shell tenta ler a memória
do núcleo, a paginação proíbe, a CPU gera uma falha de página, e o
núcleo encerra **só o shell**, libera a memória dele e o reinicia.
Num núcleo monolítico, esse erro derrubaria tudo.

Próximo capítulo: `nefs livro 4`.
