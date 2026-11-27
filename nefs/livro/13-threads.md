# 13. Threads: um programa fazendo várias coisas ao mesmo tempo

Um programa comum faz uma coisa de cada vez: uma linha, depois a outra.
Mas muita coisa precisa acontecer **ao mesmo tempo** dentro do mesmo
programa: um navegador baixa uma página enquanto você rola outra; um jogo
desenha a tela enquanto toca a música. Cada uma dessas "linhas de
trabalho" é uma **thread** (um "fio").

## Processo e thread

- Um **processo** é um programa rodando, com a **sua memória**, que ninguém
  mais vê.
- Uma **thread** é um fio de execução **dentro** de um processo. Um
  processo começa com uma thread; pode criar outras. Todas **dividem a
  mesma memória**: as mesmas variáveis globais, o mesmo heap. Cada uma tem
  só os seus registradores e a sua pilha.

Dividir a memória é a graça (uma thread enxerga o que a outra fez, sem
mandar mensagens) e é o perigo: duas threads mexendo na mesma variável ao
mesmo tempo podem estragar a conta.

## Na prática

```
cproc -o /tmp/threads /usr/share/nefs/exemplos/threads.c
/tmp/threads
```

```
Três threads procurando primos ao mesmo tempo...
  a thread de 0 a 100000 achou 9592 primos
  a thread de 100000 a 200000 achou 8392 primos
  a thread de 200000 a 300000 achou 8013 primos
Total: 25997 primos abaixo de 300000 (o certo é 25997).
```

```c
pthread_t t;
pthread_create(&t, NULL, procurar, &faixa);   /* começa a rodar procurar(&faixa) */
pthread_join(t, NULL);                        /* espera ela terminar */
```

## O mutex: um de cada vez

`total++` parece uma coisa só, mas para a CPU são três: ler o número,
somar 1, guardar. Se uma thread é interrompida entre o "ler" e o
"guardar", e a outra soma no meio, uma das somas some.

O **mutex** ("exclusão mútua") é uma tranca: só uma thread por vez passa
do `lock` até o `unlock`. As outras esperam na porta.

```c
pthread_mutex_lock(&trava);
total++;                       /* só uma thread aqui dentro */
pthread_mutex_unlock(&trava);
```

A **variável de condição** (`pthread_cond_wait`, `pthread_cond_signal`)
serve para "espere até ter algo para fazer": uma thread dorme até a outra
avisar. É o clássico produtor e consumidor.

A nlibc tem: `pthread_create`, `pthread_join`, `pthread_exit`,
`pthread_detach`, `pthread_self`, `pthread_mutex_*`, `pthread_cond_*`,
`pthread_once` e `sched_yield`. O `malloc` e o `printf` já são seguros com
várias threads; o `errno`, ainda não (é um só para o programa todo).

## Como funciona no NovaEra

- **No núcleo**, cada thread é uma tarefa, com o seu número (aparece no
  `ps` com o mesmo nome do programa). Todas as do programa apontam para o
  mesmo espaço de memória e para a mesma "líder". Uma falha ou um `exit()`
  em qualquer uma encerra o programa todo; o programa acaba quando a
  última thread sai.
- **O futex** ("mutex rápido do espaço do usuário", a mesma ideia do
  Linux): travar e destravar um mutex sem ninguém esperando é **uma única
  instrução atômica** (`xchg`, `cmpxchg`), sem entrar no núcleo. Só quando
  precisa esperar, a thread pede ao núcleo "me acorde quando este número
  mudar" (`SYS_FUTEX_ESPERAR`), e quem destrava pede "acorde quem espera
  aqui" (`SYS_FUTEX_ACORDAR`).
- Uma instrução **atômica** é uma que ninguém consegue ver pela metade:
  nem outra thread, nem uma interrupção do relógio no meio dela.

O NovaEra ainda roda num processador só: as threads se revezam muito
rápido (100 vezes por segundo), e parecem rodar juntas. Com vários
processadores, elas rodariam juntas de verdade; o código das threads já é
o mesmo.
