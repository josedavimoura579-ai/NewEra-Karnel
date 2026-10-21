# 4. O seu primeiro programa

## Criando

Dentro da pasta do seu sistema:

```
nefs programa ola
```

Isso cria `usuario/ola.c` e acrescenta a linha `ola -` no fim de
`programas.lista`. O `-` quer dizer "programa comum, sem permissões
especiais". Compile e rode:

```
nefs compilar
nefs rodar
```

Na tela do seu sistema aparece `Ola! Aqui e o programa ola (PID 4).`,
e o comando `ps` mostra o processo novo.

## Como ele é por dentro

Abra o arquivo com `vi usuario/ola.c`. A parte principal é esta:

```c
void main(u64 reinicios, u64 falha)
{
    escreve("Ola! ...\n", COR(COR_VERDE_CLARO));

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        /* trate m.tipo aqui */
    }
}
```

Todo programa do NovaEra começa em `main()`. Os dois argumentos vêm do
núcleo: quantas vezes o programa já foi reiniciado e qual falha causou
o último reinício.

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
sys_chamar(4, &m);          /* 4 = o PID do programa ola */
/* agora m.arg[0] vale 42 */
```

## As chamadas de sistema

| Função | O que faz |
|---|---|
| `sys_enviar(pid, &m)` | envia e espera o outro receber |
| `sys_receber(pid, &m)` | espera uma mensagem (`PID_QUALQUER` aceita de todos) |
| `sys_chamar(pid, &m)` | envia e espera a resposta |
| `sys_responder(pid, &m)` | responde a quem fez `sys_chamar`; nunca bloqueia |
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

Próximo capítulo: `nefs livro 5`.
