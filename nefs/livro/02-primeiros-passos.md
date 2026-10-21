# 2. Primeiros passos

## Ligando a ISO

Numa máquina virtual (QEMU, VirtualBox, VMware), use pelo menos **1 GiB
de RAM**: o sistema da ISO inteiro roda na memória.

```
qemu-system-x86_64 -cdrom nefs-0.1-x86_64.iso -m 1G
```

Em poucos segundos aparece o terminal `nefs:~#`.

## Criando o seu sistema

```
nefs novo meu-so "Sistema da Maria"
cd meu-so
```

O `nefs novo` copia o código do NovaEra para a pasta `meu-so/` e grava
o nome que você escolheu em `sistema.conf`. Veja o que foi criado:

```
nefs info
ls
```

| Arquivo | O que é |
|---|---|
| `sistema.conf` e `logo.txt` | a identidade do seu sistema: nome, versão, lema, prompt e logo |
| `programas.lista` | os processos que o núcleo inicia no boot |
| `kernel.c`, `boot.s`, `cpu.s` | o microkernel NovaEra |
| `ferramentas/` | `nefs-as`, `nefs-ld` e `nefs-ar`: o montador, o ligador e as bibliotecas |
| `novaera.h` | o contrato entre o núcleo e os programas |
| `usuario/` | os servidores do anel 3: vídeo, teclado, shell, arquivos, terminal, pipes |
| `usuario/bin/` | os comandos do shell: `ls`, `cat`, `echo`, `novafetch`... |
| `build.sh` | o roteiro de compilação (o `nefs compilar` usa ele) |

## Compilando e rodando

```
nefs compilar
nefs rodar
```

O QEMU mostra a tela do seu sistema dentro do próprio terminal. Para
sair dele: aperte **Alt+2**, digite `quit` e Enter.

Dentro do seu sistema, experimente `ajuda`, `ls /bin`, `novafetch`,
`ps` e `invadir`.

## Gerando a ISO do seu sistema

```
nefs iso
```

O arquivo `meu-so.iso` liga em qualquer PC x86_64, pelo GRUB.

## Atenção: a ISO não guarda nada

Tudo o que você faz fica na memória RAM e some quando a máquina
desliga. Para guardar o seu trabalho, copie a pasta do projeto para
fora antes de desligar. Uma forma simples, numa máquina virtual com um
disco extra (`/dev/sda`) já formatado:

```
mount /dev/sda1 /mnt
cp -r meu-so /mnt/
umount /mnt
```

Um disco persistente para a ISO está no roteiro do NEFS.

Próximo capítulo: `nefs livro 3`.
