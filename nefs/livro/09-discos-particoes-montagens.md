# 9. Discos, partições e montagens

Até o capítulo 7, o NovaEra tinha um disco só, inteiro, sempre em
`/disco`. Agora são até **quatro discos**, cada um com até **quatro
partições**, e cada sistema de arquivos aparece **onde você quiser**. É o
que se faz no começo da instalação de qualquer Linux (e do NovaEra, no
Livro 2): particionar, formatar, montar.

## Os nomes

Os nomes são os do Linux:

| Nome | O que é |
|------|---------|
| `sda` | o 1º disco (o mestre do 1º canal IDE) |
| `sdb` | o 2º disco (o escravo do 1º canal) |
| `sdc`, `sdd` | o mestre e o escravo do 2º canal |
| `sda1` a `sda4` | as partições de `sda` |

No QEMU, cada disco é um arquivo:

```
truncate -s 128M b.img
qemu-system-x86_64 -kernel kernel.bin -initrd initrd.tar -m 128M \
    -drive file=disco.img,format=raw,index=0,media=disk \
    -drive file=b.img,format=raw,index=1,media=disk
```

Dentro do NovaEra, `discos` mostra o que há (como o `lsblk`):

```
NOME   TAMANHO     TIPO        MONTADO EM
sda    64 MiB      disco       /disco (QEMU HARDDISK)
sdb    128 MiB     disco       (QEMU HARDDISK)
```

## Particionar

Uma **partição** é um pedaço do disco que funciona como um disco
separado. A tabela que diz onde cada uma começa e termina é a **MBR**: o
setor 0 do disco, com espaço para 4 partições.

```
particionar sdb                     mostra a tabela
particionar sdb criar 40M           sdb1: 40 MiB, FAT32
particionar sdb criar resto linux   sdb2: o resto, para o ext2
particionar sdb boot 1              marca sdb1 para dar boot
particionar sdb apagar 2            apaga sdb2
particionar sdb limpar              tabela nova, vazia (APAGA TUDO)
```

```
Dispositivo  Boot  Inicio     Fim        Tamanho    Tipo
sdb1         *     2048       83967      40 MiB     0c FAT32
sdb2               83968      262143     87 MiB     83 Linux (ext2)
```

A primeira partição começa no setor 2048 (1 MiB), como o `fdisk` do
Linux faz. O tipo é só um número na tabela: `0c` diz "FAT32", `83` diz
"Linux". Quem decide o que há dentro é o `formatar`.

## Formatar e montar

```
formatar sdb1                o FAT32 dentro de sdb1
montar sdb1 /mnt             sdb1 aparece em /mnt
echo oi > /mnt/oi.txt
montar                       o que está montado
desmontar /mnt               grava tudo e solta
```

**Montar** é ligar um sistema de arquivos a uma pasta: a pasta `/mnt`
passa a mostrar o que há em `sdb1`. Antes de tirar um disco (ou de
desligar), **desmonte**: o servidor guarda pedaços do disco na memória
(um cache) e só os grava de vez ao desmontar. A pasta tem de existir; crie
outras com `mkdir`.

No boot, o NovaEra monta em `/disco` o primeiro FAT32 que acha: um disco
formatado inteiro, ou a primeira partição FAT32.

Tudo isso confere com o Linux: o `fdisk -l` lê a tabela que o
`particionar` gravou, o `fsck.fat` aprova o que o `formatar` fez, e um
disco particionado no Linux (com `fdisk` e `mkfs.fat`) é montado pelo
NovaEra.

## Como funciona

Três servidores, todos no anel 3:

1. **O driver** (`disco`, PID 7) acha os discos nos dois canais IDE e lê
   a MBR de cada um. Um pedido diz o **dispositivo** (`sdb1`) e o setor
   *dentro dele*: o driver soma o início da partição e confere o fim. Um
   sistema de arquivos não consegue escrever fora da sua partição.
2. **O servidor de arquivos** (`arquivos`, PID 4) é a **raiz** e guarda a
   **tabela de montagens**. Um pedido com um caminho dentro de `/mnt` é
   respondido com "não é comigo: fale com o PID 8", e a biblioteca refaz o
   pedido lá. Ler e escrever não passam por ele: o número do arquivo
   aberto já diz qual servidor o abriu.
3. **O FAT32** (`fat`, PID 8) cuida de até quatro volumes ao mesmo tempo,
   cada um com o seu cache.

Num Linux, tudo isso mora dentro do núcleo (o "VFS"). No NovaEra, são
mensagens entre processos: se o servidor do FAT32 falhar, o núcleo o
reinicia e o resto do sistema continua de pé.

Ainda não há partições estendidas (mais de 4) nem tabelas GPT: o
`particionar` mostra que existem, mas não mexe nelas.
