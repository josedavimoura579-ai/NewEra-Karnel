# 12. Instalando o NovaEra no disco

> O passo a passo completo, com o nome da máquina, o usuário e um disco
> para dados, está no **Livro 2**: `nefs guia`.

Até aqui, o NovaEra ligava "emprestado": o QEMU (ou a ISO) entregava o
sistema pronto na memória, e tudo o que você criava sumia ao desligar.
Agora ele se **instala num disco** e **liga sozinho** por ele, com tela de
login, como um computador de verdade.

## Instalando, passo a passo

Ligue o NovaEra como sempre (`bash run.sh`), com um disco vazio. Depois:

```
particionar sda criar resto linux     1. uma partição do tipo Linux
particionar sda boot 1                2. marca para dar boot
formatar -t ext2 sda1                 3. o ext2 dentro dela
montar sda1 /mnt                      4. o disco novo aparece em /mnt
instalar-sistema /mnt                 5. copia o sistema para lá
instalar-boot sda /mnt                6. põe o GRUB no disco
desligar                              7. grava tudo e desliga
```

Agora ligue **só pelo disco**, sem entregar o sistema pronto. No QEMU:

```
qemu-system-x86_64 -m 128M -display curses \
    -drive file=disco.img,format=raw,index=0,media=disk
```

(Sem `-kernel`, `-initrd` nem `-cdrom`.) Aparece o menu do GRUB, o NovaEra
liga e pede o login:

```
[init] montando a raiz: sda1 em /

NovaEra Microkernel - entre com o seu usuario.
(O root ainda nao tem senha: entre como root, sem senha, e use passwd.)
login:
```

Entre como `root` (sem senha, na primeira vez), defina uma senha com
`passwd`, crie o seu usuário com `adduser`, e pronto: o que você fizer
**fica guardado**. Para desligar, sempre `desligar` (ou `sudo desligar`):
ele grava o que ainda está na memória dos servidores de disco antes.

## O que cada passo faz

| Passo | O que acontece |
|---|---|
| `particionar` | divide o disco: a partição 1 começa no setor 2048 (1 MiB). O espaço antes dela fica livre para o GRUB |
| `formatar -t ext2` | o sistema de arquivos com dono e permissões |
| `instalar-sistema` | copia `/bin`, `/etc`, `/usr` e `/boot` (com o `kernel.bin`) e cria `/home`, `/root`, `/tmp`... (o `pacstrap` do Arch) |
| `instalar-boot` | grava o GRUB no começo do disco e o menu em `/boot/grub/grub.cfg` (o `grub-install` do Linux) |

## Como um computador liga pelo disco

1. A **BIOS** lê o setor 0 do disco (a **MBR**, 512 bytes) e pula para
   ele. Lá estão 440 bytes do GRUB e a tabela de partições.
2. Esse pedacinho carrega o **resto do GRUB**, que mora nos setores 1 a
   2047, antes da primeira partição.
3. O GRUB procura a partição que tem o `/boot/grub/grub.cfg`, lê o menu e
   carrega o `/boot/kernel.bin` com a linha `root=sda1`.
4. O **núcleo** liga e inicia os servidores (vídeo, teclado, discos...). Ele
   não usa a linha de boot, só a guarda.
5. O **shell do boot** (PID 3) vê o `root=sda1`, monta aquela partição na
   **raiz** (`/`) e chama o **`/bin/init`** de lá.
6. O **init** monta os outros discos do `/etc/montagens` e chama o
   **`login`**. Quando a pessoa sai, ele chama o login de novo.

Daí em diante, todo `/bin/ls`, todo `/etc/passwd`, vêm do disco.

## O /etc/montagens

É o `/etc/fstab` do Linux: os discos que o init monta sozinho no boot.

```
# dispositivo   pasta
sdb             /dados
```

Crie a pasta antes (`mkdir /dados`). O que estiver lá aparece em `/dados`
em todo boot.

## Se algo der errado

- Sem o `root=` (no QEMU com `-kernel`, ou pela ISO), o NovaEra roda da
  memória, como antes: use isso para consertar um disco.
- Se a raiz não montar, ou o disco não tiver `/bin/init`, o NovaEra avisa e
  abre o shell do root, na memória.
