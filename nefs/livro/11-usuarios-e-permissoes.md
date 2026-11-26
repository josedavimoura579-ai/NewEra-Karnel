# 11. Usuários, senhas e permissões

Até aqui, todo mundo no NovaEra era o **root**: o dono de tudo, que pode
tudo. Agora o sistema tem **usuários**: cada um com o seu nome, a sua
senha, a sua pasta, e arquivos que os outros não podem mexer.

## Na prática

```
adduser ana                 cria a ana (pede a senha duas vezes)
adduser ana sudo            deixa a ana usar o sudo
passwd                      troca a senha do root (no começo, ele não tem)
su ana                      vira a ana
whoami                      ana
id                          uid=1000(ana) gid=1000(ana) grupos=1000(ana),27(sudo)
cat /etc/sombra             cat: permissao negada
echo x > /bin/teste         Permissao negada
sudo cat /etc/sombra        pede a senha DA ANA e mostra
sair                        volta a ser o root
login                       entra com usuário e senha, na pasta pessoal
```

## Quem é quem

| | |
|---|---|
| **usuário** | uma pessoa (ou um serviço). Tem um número, o **uid**. O root é o 0; os usuários comuns começam em 1000 |
| **grupo** | um conjunto de usuários, com um número (o **gid**). Cada usuário tem o seu grupo e pode estar em outros (`sudo`, `users`...) |
| **root** | o administrador. Pode tudo, mesmo sem permissão |
| **sudo** | "faça isto como root": roda **um** comando como root, pedindo a **sua** senha. Só para quem está no grupo `sudo` (ou `wheel`) |
| **su** | "vire outro usuário": abre um shell como ele, pedindo a senha **dele** |

Os usuários moram em três arquivos de texto, como no Linux:

```
/etc/passwd   ana:x:1000:1000:ana:/home/ana:/bin/sh
/etc/group    sudo:x:27:ana
/etc/sombra   ana:$nefs$3f9a...$c41d...:
```

A senha **não** fica guardada: só uma "impressão digital" dela (o
SHA-256, misturado com um "sal" sorteado e repetido mil vezes). Para
conferir, o sistema calcula a impressão do que você digitou e compara.
Mesmo quem lê o arquivo não descobre a senha. E só o root lê a
`/etc/sombra`.

## As permissões: rwx

```
ls -l /etc
-rw-r--r-- root  root   30 passwd
-rw------- root  root   20 sombra
drwxr-xr-x ana   ana     0 ana
```

Cada arquivo tem um **dono**, um **grupo** e três trios de letras:

```
-  rw-   r--   r--
|  dono  grupo outros
d = pasta
```

- `r` (ler): ver o conteúdo; numa pasta, listar
- `w` (escrever): mudar; numa pasta, criar e apagar coisas dentro
- `x` (executar): rodar o programa; numa pasta, **entrar** nela

Em números (o jeito mais usado), cada trio é a soma de 4 (r), 2 (w) e 1
(x):

| número | letras | quer dizer |
|---|---|---|
| `644` | `rw-r--r--` | o dono muda, todos leem (um texto) |
| `755` | `rwxr-xr-x` | o dono muda, todos rodam (um programa, uma pasta) |
| `600` | `rw-------` | só o dono (a `/etc/sombra`) |
| `700` | `rwx------` | uma pasta só sua |

```
chmod 600 segredo.txt       só eu leio
chmod +x meu_programa       todos podem rodar
sudo chown ana arquivo      o dono passa a ser a ana (só o root muda o dono)
```

A pasta `/tmp` é de todos (`rwxrwxrwt`): qualquer um cria arquivos lá,
mas o `t` (*sticky*) faz cada um só conseguir apagar o que é seu.

No **ext2** as permissões ficam gravadas no disco. No **FAT32** não há
onde guardar: tudo aparece como `rwxrwxrwx`, de todos.

## Como funciona

- **O núcleo** guarda, em cada processo, de quem ele é (uid, gid e os
  grupos). Um filho herda do pai. Só um processo do root pode mudar essa
  identidade. Um usuário só manda sinais (o `kill`) para os seus processos.
- **Os servidores de arquivos** perguntam ao núcleo "quem está me pedindo
  isto?" e conferem o `rwx` antes de abrir, criar, apagar ou listar.
- **O servidor de contas** (PID 10) é o único que lê a `/etc/sombra`. O
  `su`, o `sudo`, o `login` e o `passwd` mandam a senha para ele; se ela
  confere, **ele** muda a identidade do programa que pediu.

No Linux, o `su` e o `sudo` funcionam de outro jeito: o arquivo deles tem
um bit especial (*setuid*) que diz "rode sempre como root", e o próprio
programa decide se deixa. No NovaEra, quem decide é um servidor separado,
e o programa só vira root **depois** da senha certa.

Por enquanto, os usuários moram na memória (o `/etc` vem do initrd) e
somem ao desligar. Quando o sistema morar no disco ext2 (a próxima etapa),
eles ficam.
