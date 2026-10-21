# 3. Como o NovaEra funciona

O NovaEra é um **microkernel**. O núcleo (anel 0, o modo mais poderoso
da CPU) faz só quatro coisas:

1. **separa a memória** de cada processo (paginação);
2. **divide o tempo da CPU** entre eles (escalonador);
3. **leva mensagens** de um processo para outro (IPC);
4. **avisa os drivers** quando o hardware interrompe (IRQ vira mensagem).

Todo o resto (vídeo, teclado, shell e os seus programas) roda no
**anel 3**, sem poder nenhum sobre o hardware nem sobre a memória alheia.

## Ordem de leitura

Leia nesta ordem. Os arquivos são comentados em português.

1. `novaera.h` — chamadas de sistema, formato das mensagens, PIDs.
2. `linker.ld` — onde o núcleo mora na memória (a partir de 1 MiB).
3. `boot.asm` — do boot em 32 bits até o modo de 64 bits.
4. `kernel.c` — memória, paginação, processos, escalonador e IPC.
5. `usuario/` — os programas: `video.c`, `teclado.c` e `shell.c`.

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

Quando o shell quer escrever na tela, ele não toca na memória de vídeo.
Ele monta uma mensagem e pede ao núcleo para entregá-la:

```c
msg_t m;
msg_zera(&m);
m.tipo   = MSG_VIDEO_ESCREVER;
m.arg[0] = COR(COR_BRANCO);
/* ... copia o texto para m.texto ... */
sys_enviar(PID_VIDEO, &m);
```

O núcleo copia a mensagem do shell para o servidor de vídeo, que é o
único processo com a memória VGA mapeada. Nenhum dos dois enxerga a
memória do outro.

## Quando algo dá errado

Digite `invadir` no shell do seu sistema. O shell tenta ler a memória
do núcleo, a paginação proíbe, a CPU gera uma falha de página, e o
núcleo encerra **só o shell**, libera a memória dele e o reinicia.
Num núcleo monolítico, esse erro derrubaria tudo.

Próximo capítulo: `nefs livro 4`.
