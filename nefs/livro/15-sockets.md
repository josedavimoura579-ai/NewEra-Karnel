# 15. Sockets: uma linha de conversa entre programas

Já vimos três jeitos de programas conversarem: **mensagens** (o núcleo
leva um bilhete de um para o outro), **pipes** (`ls | cat`: a saída de um
vira a entrada do outro, num sentido só) e **memória compartilhada** (os
dois mexem no mesmo pedaço de memória). O **socket** é o quarto: uma linha
de telefone entre dois programas, nos **dois sentidos**, com **nome** para
ligar.

É o jeito mais usado no Linux para um programa pedir coisas a um serviço:
o servidor gráfico (X11, Wayland), o D-Bus, bancos de dados. E é o que a
Fase 5 vai usar: cada programa vai se conectar ao servidor de janelas por
um socket.

## Na prática

```
cproc -o /tmp/sockets /usr/share/nefs/exemplos/sockets.c
/tmp/sockets
```

```
Servidor (processo 12) ouvindo em /tmp/maiusculas.sock
  cliente 13 mandou "ola, servidor" e recebeu "OLA, SERVIDOR"
  cliente 14 mandou "tudo bem?" e recebeu "TUDO BEM?"
  cliente 15 mandou "sockets sao legais" e recebeu "SOCKETS SAO LEGAIS"
Servidor: atendi 3 clientes.
```

## Como se usa

É como uma central telefônica:

| Quem | Faz | Comparação |
|---|---|---|
| servidor | `socket()` | compra um telefone |
| servidor | `bind("/tmp/x.sock")` | ganha um número |
| servidor | `listen()` | liga a campainha |
| cliente | `socket()` + `connect("/tmp/x.sock")` | disca o número |
| servidor | `accept()` | atende: ganha uma linha só daquela ligação |
| os dois | `write()` e `read()` | conversam |
| os dois | `close()` | desligam |

```c
/* servidor */
int s = socket(AF_UNIX, SOCK_STREAM, 0);
bind(s, (struct sockaddr *)&endereco, sizeof endereco);
listen(s, 8);
int c = accept(s, NULL, NULL);          /* espera alguém ligar */
read(c, buf, sizeof buf);
write(c, "oi", 2);

/* cliente */
int fd = socket(AF_UNIX, SOCK_STREAM, 0);
connect(fd, (struct sockaddr *)&endereco, sizeof endereco);
write(fd, "ola", 3);
read(fd, buf, sizeof buf);
```

O servidor pode atender vários clientes ao mesmo tempo: o `poll()` espera
até **algum** deles ter algo para ler. E o `socketpair()` dá dois sockets
já ligados um no outro, sem nome, para dois pedaços do mesmo programa (ou
um programa e o filho).

Quando um lado fecha, o `read` do outro devolve 0 (fim). Escrever para
quem já desligou dá `EPIPE` (e o sinal SIGPIPE), como num pipe.

## Como funciona no NovaEra

Os sockets são mais um servidor no anel 3, o **PID 11**, parecido com o
dos pipes:

- ele guarda os nomes (`/tmp/maiusculas.sock`) e quem está ouvindo em
  cada um;
- cada lado de uma conexão tem um buffer de 16 KiB: o que um lado escreve
  vai para o buffer do outro;
- quem lê um buffer vazio (ou escreve num cheio) fica esperando a
  resposta, e o servidor responde quando houver dados (ou espaço): o
  servidor em si nunca fica parado esperando ninguém.

Para os programas, um socket é um descritor de arquivo como outro
qualquer: o mesmo `read`, `write` e `close`.

No Linux, o `bind` cria um arquivo especial na pasta. No NovaEra, o nome
mora só dentro do servidor de sockets, e some quando quem ouvia fecha. E
ainda não há sockets de **rede** (`AF_INET`): eles vêm quando o NovaEra
tiver um driver de placa de rede.
