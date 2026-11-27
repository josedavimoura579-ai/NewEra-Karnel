# 14. Memória compartilhada: dois programas, a mesma memória

Cada programa do NovaEra tem a sua memória, que nenhum outro enxerga: é o
que impede um programa com defeito de estragar os outros. Para conversar,
eles mandam **mensagens** pelo núcleo. Mas às vezes é muito dado para
copiar de um lado para o outro (uma imagem inteira, a tela de uma janela).
A saída é a **memória compartilhada**: um pedaço de memória que dois
programas enxergam **ao mesmo tempo**. O que um escreve, o outro lê na
hora, sem copiar nada.

É assim que o servidor de janelas da Fase 5 vai receber o desenho de cada
programa.

## Na prática

```
cproc -o /tmp/compartilhada /usr/share/nefs/exemplos/compartilhada.c
/tmp/compartilhada
```

```
Leitor (processo 11) esperando o escritor (processo 12)...
  recado 1: ola!
  recado 2: estou noutro programa
  recado 3: mas escrevo na mesma memoria
  recado 4: sem mandar mensagens
  recado 5: tchau
Pronto.
```

São **dois programas** diferentes (processos 11 e 12). O escritor escreve
os recados num pedaço de memória chamado `/quadro`; o leitor os vê
aparecer.

```c
int fd = shm_open("/quadro", O_CREAT | O_RDWR, 0600);   /* cria (ou abre) pelo nome */
ftruncate(fd, sizeof(quadro_t));                         /* o tamanho */
quadro_t *q = mmap(NULL, sizeof(quadro_t),               /* põe na memória do programa */
                   PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
...
shm_unlink("/quadro");                                   /* tira o nome */
```

Os dois lados chamam o mesmo `shm_open("/quadro")`; cada um recebe as
mesmas páginas, cada um num endereço da sua memória. E, como no capítulo
anterior, um **mutex** e uma **variável de condição** dentro dessa memória
organizam quem mexe quando, agora entre programas diferentes.

## mmap: pedir memória direto ao núcleo

O `mmap` também serve para pedir memória "solta", só sua:

```c
char *m = mmap(NULL, 1024 * 1024, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);     /* 1 MiB, zerado */
munmap(m, 1024 * 1024);                                 /* devolve */
```

Com `PROT_READ` sozinho, a memória é só para ler: escrever nela dá
SIGSEGV. Usar depois do `munmap`, também.

## Como funciona

Lembra das **tabelas de páginas** (capítulo 3)? Cada programa tem as suas:
elas dizem "o endereço 0x71000000 do programa é o pedaço 0x0234F000 da
memória de verdade". Normalmente, cada pedaço de memória de verdade
aparece na tabela de um programa só.

Na memória compartilhada, o núcleo põe **o mesmo pedaço de verdade** na
tabela dos dois programas. É só isso: a memória não é copiada, ela é
apontada duas vezes.

- O objeto (`/quadro`) mora no núcleo: um nome e a lista dos pedaços de
  memória. Ele vive enquanto tiver nome ou alguém o usar.
- Quando um programa termina, o núcleo devolve a memória que era só dele,
  mas **não** a compartilhada (uma marca na tabela de páginas diz qual é
  qual).
- O futex (capítulo 13) é guardado pelo endereço de **verdade** (físico):
  por isso um mutex dentro da memória compartilhada funciona entre
  programas, mesmo cada um vendo o mutex num endereço diferente.

Por enquanto, cada objeto tem até 2 MiB, e o `mmap` de um arquivo comum
(não compartilhado) ainda não existe.
