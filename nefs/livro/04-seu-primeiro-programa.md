# 4. O seu primeiro programa

O NovaEra tem dois tipos de programa, os dois rodando no anel 3:

| Tipo | Onde fica | Quando roda | Como criar |
|---|---|---|---|
| **Comando** | `usuario/bin/<nome>.c`, vai para o initrd | quando você digita o nome no shell | `nefs comando <nome>` |
| **Servidor** | `usuario/<nome>.c`, embutido no `kernel.bin` | desde o boot, para sempre | `nefs programa <nome>` |

## Um comando

```
nefs comando saudacao
nefs compilar
nefs rodar
```

No shell do seu sistema, digite `saudacao oi tudo bem`. O arquivo
`usuario/bin/saudacao.c` é um programa em C padrão, como em qualquer
sistema:

```c
#include <stdio.h>

int main(int argc, char **argv)
{
    printf("Ola do comando saudacao!\n");
    for (int i = 1; i < argc; i++)
        printf("  argumento %d: %s\n", i, argv[i]);
    return 0;
}
```

Os comandos usam a **libc do NovaEra** (pasta `libc/`): `printf`,
`fopen`, `malloc`, `qsort`, `strtod`, `getopt`... Por baixo, cada uma
dessas funções vira mensagens para os servidores (terminal, arquivos,
pipes). O `testelibc`, em `usuario/bin/`, testa todas elas.

`argc` e `argv` são as palavras que você digitou, e o valor devolvido é
o **código de saída**: se não for 0, o shell mostra. Os comandos têm heap
(`sys_sbrk`) e uma pilha de até 1 MiB que cresce sozinha. Veja exemplos
em `usuario/bin/`: `ola`, `codigo`, `memoria`, `falha` e `filho`. Este
último cria outro processo com `executar()` e espera por ele com
`sys_esperar()`. Em C padrão, o mesmo se faz com `posix_spawnp()` e
`waitpid()`.

## Um servidor

Dentro da pasta do seu sistema:

```
nefs programa relogio
```

Isso cria `usuario/relogio.c` e acrescenta a linha `relogio -` no fim de
`programas.lista`. O `-` quer dizer "programa comum, sem permissões
especiais". Compile e rode:

```
nefs compilar
nefs rodar
```

Na tela do seu sistema aparece `Ola! Aqui e o programa relogio (PID 4).`,
e o comando `ps` mostra o processo novo.

## Como ele é por dentro

Abra o arquivo com `vi usuario/relogio.c`. A parte principal é esta:

```c
int main(int argc, char **argv)
{
    escreve("Ola! ...\n", COR(COR_VERDE_CLARO));

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        /* trate m.tipo aqui */
    }
}
```

Todo programa do NovaEra começa em `main(argc, argv)`. Um servidor do boot
recebe só o próprio nome em `argv[0]`. Se ele falhar, o núcleo o recarrega;
para saber quantas vezes isso já aconteceu, ele pergunta ao núcleo com
`sys_tarefa_info(sys_pid(), &m)` (veja o começo de `usuario/shell.c`).

O laço `for (;;)` transforma o programa num **servidor**: ele dorme em
`sys_receber()` até alguém mandar uma mensagem. Enquanto dorme, não
gasta CPU nenhuma.

## Respondendo a pedidos

Para o seu programa responder a outro, use um tipo de mensagem novo e
`sys_responder()`:

```c
#define MSG_OLA_PERGUNTA 100

    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        if (m.tipo == MSG_OLA_PERGUNTA) {
            m.arg[0] = 42;                  /* a resposta */
            sys_responder(m.origem, &m);
        }
    }
```

Do outro lado (no shell, por exemplo), quem pergunta usa `sys_chamar()`,
que envia e espera a resposta:

```c
msg_t m;
msg_zera(&m);
m.tipo = MSG_OLA_PERGUNTA;
sys_chamar(4, &m);          /* 4 = o PID do programa relogio */
/* agora m.arg[0] vale 42 */
```

## As chamadas de sistema

| Função | O que faz |
|---|---|
| `sys_enviar(pid, &m)` | envia e espera o outro receber |
| `sys_receber(pid, &m)` | espera uma mensagem (`PID_QUALQUER` aceita de todos) |
| `sys_chamar(pid, &m)` | envia e espera a resposta |
| `sys_responder(pid, &m)` | responde a quem fez `sys_chamar`; nunca bloqueia |
| `executar(caminho, argv, 0, 0, 0, 0)` | lê o programa do servidor de arquivos e cria o processo; devolve o PID |
| `sys_esperar(pid)` | espera o filho terminar; devolve o código de saída |
| `sys_sbrk(bytes)` | aumenta o heap; devolve o início da área nova |
| `sys_pid()` | o próprio PID |
| `sys_tempo()` | ticks desde o boot (100 por segundo) |
| `sys_ceder()` | passa a vez na CPU |
| `sys_irq_assinar(irq)` | só drivers: recebe uma IRQ como mensagem |

## Escrevendo um driver

Um driver é um programa com a permissão `driver` em `programas.lista`:

```
meudriver   driver
```

Com ela, o programa pode usar `inb()` e `outb()` para falar com as
portas do hardware e `sys_irq_assinar()` para receber interrupções.
O `usuario/teclado.c` é um driver completo: leia-o como exemplo.

## O compilador dentro do NovaEra

O NovaEra já roda o **QBE**, a metade de trás do compilador do NEFS. No
shell do seu sistema:

```
qbe /usr/share/qbe/ola.ssa
```

Ele lê a linguagem intermediária do QBE (SSA) e escreve o assembly x86_64,
igual ao que o QBE escreve no Linux. O QBE não foi reescrito: é o código
original compilado com a libc do NovaEra (veja a pasta `portes/`).

O montador e o ligador do NEFS também rodam lá dentro. Do SSA até um
programa que roda, sem sair do NovaEra:

```
qbe -o /tmp/ola.s /usr/share/qbe/ola.ssa     # SSA -> assembly
nefs-as -o /tmp/ola.o /tmp/ola.s             # assembly -> objeto
nefs-ld -o /tmp/ola /tmp/ola.o -lc           # objeto + libc -> programa
/tmp/ola
```

O `-lc` é a libc do NovaEra (`/usr/lib/nefs/libc.a`): o `nefs-ld` pega
dela só o que o programa usa, inclusive o começo (`_start`, em `crt0.s`).

Próximo capítulo: `nefs livro 5`.
