# 7. O disco e a ISO

Até aqui, tudo o que você fazia no NovaEra sumia ao desligar: os arquivos
ficavam na memória. Agora o NovaEra tem um **disco**.

## O disco: /disco

O disco aparece na pasta `/disco`. Ele usa o **FAT32**, o mesmo sistema de
arquivos de pendrives e cartões de memória: o que você gravar lá também
abre no Windows, no Linux e no celular.

No QEMU, o disco é um arquivo (`disco.img`). O `run.sh` e o `nefs rodar`
criam um de 64 MiB no primeiro uso. Dentro do NovaEra, prepare-o uma vez:

```
formatar
```

O `formatar` apaga tudo e pergunta antes (digite `sim`). Depois:

```
echo ola > /disco/teste.txt
mkdir /disco/projetos
cp /usr/share/nefs/exemplos/ola.c /disco/projetos
ls /disco/projetos
```

Desligue, ligue de novo: os arquivos continuam lá.

Do lado de fora (Linux ou Termux, com o pacote `mtools`), dá para ver e
copiar os arquivos do disco sem ligar o NovaEra:

```
mdir -i disco.img ::/projetos
mcopy -i disco.img ::/projetos/ola.c .
```

## Como funciona

São dois servidores novos, no anel 3, como todos os outros:

| PID | Servidor | O que faz |
|---|---|---|
| 7 | `disco` | o driver do disco (ATA): lê e grava setores de 512 bytes pelas portas de E/S |
| 8 | `fat` | o sistema de arquivos FAT32: pastas, nomes longos, arquivos |

A biblioteca (`ulib.h`) manda todo caminho que começa em `/disco` para o
servidor `fat`; o resto vai para o servidor de arquivos da memória. Os
dois falam o mesmo protocolo, então `cat`, `ls`, `cp`, o shell e a libc
funcionam igual nos dois.

## O seu sistema, guardado no disco

```
nefs novo /disco/meu Meu Sistema
nefs compilar /disco/meu
```

O código, o `build/kernel.bin` e o `build/initrd.tar` ficam no disco.

## A ISO

```
nefs iso -o /disco/meu.iso /disco/meu
```

O `nefs iso` gera uma ISO inicializável (ISO 9660 com boot "El Torito"):
o GRUB dá o boot e carrega o seu `kernel.bin` e o seu `initrd.tar`. Tire
o arquivo do disco com o `mcopy` e dê boot nele:

```
mcopy -i disco.img ::/meu.iso .
qemu-system-x86_64 -cdrom meu.iso -m 256M
```

Funciona também num PC de verdade (gravada num CD, ou num pendrive com um
programa como o Ventoy).

## A ISO do NEFS sem Linux

A própria ISO do NEFS agora é assim: o NovaEra puro, sem nenhum Linux por
baixo, com o compilador, o `nefs`, o código e este livro
(`nefs/iso/novaera-iso.sh`). Você liga, formata o disco e começa a criar.

Este é o fim do Livro 1. Os próximos volumes vão tratar de sinais e
processos, usuários e permissões, gráficos e de como montar uma área de
trabalho (veja `nefs/ROTEIRO.md`).
