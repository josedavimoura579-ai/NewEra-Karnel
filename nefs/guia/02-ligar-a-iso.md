# 2. Ligar pela ISO

Ligue o computador (ou o QEMU) pela ISO. O GRUB mostra o menu e o NovaEra
liga **da memória**: nada do que você fizer agora mexe no disco, até você
mandar.

Você cai direto no shell do **root** (o administrador). Confira:

```
whoami
```

## A data

```
data
```

O relógio do computador guarda a hora em UTC (o horário de Londres): três
horas a mais que Brasília.

## Os discos

```
discos
```

```
NOME   TAMANHO     TIPO        MONTADO EM
sda    256 MiB     disco       (QEMU HARDDISK)
```

O `sda` é o disco onde o NovaEra vai morar. Se ele aparecer montado em
`/disco` (já tinha um FAT32), solte antes:

```
desmontar /disco
```
