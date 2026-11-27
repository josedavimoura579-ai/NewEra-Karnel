# 8. Sinais: o Ctrl+C e o `kill`

Um programa preso num laço, ou esperando um pipe que nunca vai receber
nada, precisa de um jeito de ser parado de fora. No Unix esse jeito é o
**sinal**: um aviso que chega a qualquer momento, como uma interrupção, só
que para um processo.

## Na prática

Rode algo que não termina e aperte **Ctrl+C**:

```
sleep 100 | cat
^C
[sleep interrompido (SIGINT)]
[cat interrompido (SIGINT)]
```

O Ctrl+C para **a linha inteira**: os dois lados do pipe. O shell avisa
quem foi interrompido e mostra o prompt de novo.

De outro programa, o comando `kill` manda um sinal pelo número do processo
(veja os números com `ps`):

```
kill 12            SIGTERM: pede para o processo 12 terminar
kill -9 12         SIGKILL: termina sem conversa
kill -INT 12       o mesmo que um Ctrl+C
kill -TERM -12     o grupo 12 inteiro (número negativo)
kill -l            a lista dos sinais
```

Os servidores do sistema (vídeo, teclado, shell, arquivos...) não aceitam
sinais: um `kill 1` responde que o processo é um servidor do sistema.

## Tratar um sinal

Um programa pode escolher o que acontece quando um sinal chega:

- a **ação padrão**: quase sempre, encerrar o processo;
- **ignorar** o sinal (`SIG_IGN`);
- rodar uma **função dele**, o *tratador*, e depois continuar de onde
  parou.

O exemplo `sinais.c` conta para sempre e trata o Ctrl+C:

```
cproc -o /tmp/sinais /usr/share/nefs/exemplos/sinais.c
/tmp/sinais
```

```c
static int ctrl_c;

static void tratador(int sinal)
{
    ctrl_c++;               /* só marca; o programa decide o que fazer */
}

int main(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = tratador;
    sigaction(SIGINT, &sa, NULL);
    ...
}
```

A cada Ctrl+C ele avisa e continua contando; no terceiro, termina. O
SIGKILL (9) é o único que nunca pode ser tratado nem ignorado: é a garantia
de que sempre dá para parar um programa.

A nlibc tem o que um programa de Unix espera: `signal`, `sigaction`,
`kill`, `raise`, `killpg`, `sigprocmask` e os `sigset`, `setpgid`,
`getpgrp`, `pause`, `sleep`, `strsignal`, e o `WIFSIGNALED`/`WTERMSIG` do
`waitpid`.

## Erros viram sinais

Quando um programa lê memória que não é dele, a CPU gera uma exceção. O
núcleo a transforma num sinal:

| Exceção da CPU                      | Sinal   |
|-------------------------------------|---------|
| falha de página, proteção geral     | SIGSEGV |
| divisão por zero, erro de ponto flutuante | SIGFPE |
| instrução inválida                  | SIGILL  |

Sem tratador, o processo acaba e o shell conta o que houve:

```
falha
[nucleo] falha foi encerrado: Falha de pagina (SIGSEGV). O sistema continua de pe.
```

E escrever num pipe que ninguém mais lê dá **SIGPIPE** (ou, se o programa o
ignorar, o erro `EPIPE` no `write`).

## Como funciona

**Enviar** um sinal (`SYS_SINAL`) só marca um bit no processo. A ação
padrão é feita na hora, mesmo que o alvo esteja dormindo à espera de uma
mensagem: é por isso que o Ctrl+C solta um pipe travado.

**Entregar** a um tratador acontece quando o processo vai voltar ao anel 3.
O núcleo empilha, na pilha do próprio programa, tudo o que ele tinha nos
registradores (inclusive a FPU) e o endereço de um "restaurador", e faz a
CPU voltar para o tratador em vez do ponto interrompido. Quando o tratador
termina, o `ret` cai no restaurador, um `int 0x80` com `SYS_SINAL_VOLTAR`:
o núcleo copia tudo de volta e o programa continua como se nada tivesse
acontecido. É o mesmo truque do Linux (o `sigreturn`).

**O Ctrl+C** passa por três processos, todos no anel 3:

1. o driver do teclado entrega a tecla (o código 3) ao terminal;
2. o terminal manda o SIGINT ao **grupo do primeiro plano**;
3. o shell põe cada linha de comando num grupo novo (o número do primeiro
   processo dela) e avisa o terminal (`TERM_GRUPO`) antes de esperar.

Os grupos de processos são o que faz o Ctrl+C parar todos os lados de um
pipe, e nada mais: o shell, num grupo próprio, continua de pé.

Quem é encerrado por um sinal sai com o código `0x100 | sinal`; um código
normal vai de 0 a 255, como no Unix. A nlibc traduz isso para o `status`
do `waitpid`.
