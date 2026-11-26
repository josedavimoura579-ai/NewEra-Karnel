# Fase 4 — base POSIX e o modo manual

**Objetivo:** um sistema multiusuário em modo texto que a pessoa monta à
mão, como no guia de instalação do Arch: particionar o disco, formatar,
copiar o sistema, instalar o boot, criar os usuários e dar boot no disco.
E, por baixo, o que quase todo programa de Unix espera: sinais, threads,
memória compartilhada e sockets.

## Decisões

- **Discos:** **ext2** para o sistema (dono, grupo e permissões `rwx` de
  verdade; o Linux lê e grava, e o `mkfs.ext2`/`fsck.ext2` servem de
  gabarito nos testes) e **FAT32** em `/disco`, como um pendrive, para
  trocar arquivos com o Windows e o celular. No FAT32 não há dono: tudo
  aparece como do root, como no Linux.
- **Montagens:** uma tabela de pontos de montagem (como o `/etc/fstab`); a
  ulib manda cada caminho para o servidor do ponto de montagem mais longo.
- **Senhas:** guardadas com sal e SHA-256 em `/etc/sombra` (o `/etc/shadow`),
  só o root lê.

## Etapas

Cada etapa termina com algo testável no QEMU.

- [x] **Etapa 1 — sinais e `kill`** (núcleo 3.3; [livro, capítulo 8](livro/08-sinais.md))
  - sinais no núcleo: enviar, tratar (função do programa, com volta pelo
    núcleo), ignorar; o padrão encerra o processo
  - grupos de processos: **Ctrl+C** encerra o comando do primeiro plano
  - uma falha (falha de página, divisão por zero) vira SIGSEGV/SIGFPE
  - nlibc: `signal`, `sigaction`, `kill`, `raise`, `WIFSIGNALED`; SIGPIPE
  - comando `kill`; o shell mostra quem foi interrompido
  - *Teste: Ctrl+C num programa em laço e num pipe travado; um tratador de
    SIGINT roda e o programa continua.*
  - Feito: `SYS_SINAL`, `SYS_SINAL_TRATAR`, `SYS_SINAL_VOLTAR`,
    `SYS_SINAL_MASCARA`, `SYS_GRUPO`; o quadro do sinal (registradores, FPU,
    máscara, siginfo) vai na pilha do programa; o terminal manda o SIGINT ao
    grupo do primeiro plano (`TERM_GRUPO`); `kill` e `sleep`; o `testelibc`
    ganhou 22 testes de sinais (inclusive sinais no meio de contas com
    vírgula). Quem espera a resposta de um servidor não é interrompido por
    um tratador (o sinal espera a resposta, como o `SA_RESTART`); ainda não
    há `SIGSTOP`/`SIGCONT` de verdade (controle de tarefas).
- [x] **Etapa 2 — discos, partições e montagens** (núcleo 3.4; [livro, capítulo 9](livro/09-discos-particoes-montagens.md))
  - o driver ATA com os quatro discos dos dois canais; partições MBR
  - `particionar` (o fdisk do NovaEra), `montar` e `desmontar`
  - *Teste: um disco com duas partições, uma FAT32 montada onde se quiser.*
  - Feito: o driver acha os 4 discos (sda a sdd) e lê a MBR; o dispositivo
    é disco × 8 + partição e o LBA conta do começo da partição. O servidor
    de arquivos é a raiz e guarda a tabela de montagens (responde
    `ARQ_OUTRO_SERVIDOR`); o servidor FAT32 cuida de até 4 volumes.
    Comandos: `discos`, `particionar`, `montar`, `desmontar`, e o
    `formatar DISPOSITIVO`. Conferido com o Linux: `sfdisk`/`fdisk -l` leem
    a tabela, o `fsck.fat` aprova a partição, e um disco particionado no
    Linux é montado em `/disco` no boot.
- [x] **Etapa 3 — o ext2** (núcleo 3.5; [livro, capítulo 10](livro/10-o-ext2.md))
  - servidor ext2: pastas, arquivos, links, permissões guardadas no disco
  - `formatar -t ext2` (o mkfs.ext2 do NovaEra)
  - *Teste: o `fsck.ext2` do Linux aprova o que o NovaEra grava, e o
    NovaEra lê um disco feito pelo `mkfs.ext2`.*
  - Feito: o servidor `ext2` (PID 9) lê e grava blocos de 1, 2 e 4 KiB,
    inodes de 128 ou 256 bytes, blocos indiretos (até o triplo), pastas,
    links (a contagem; apagar um nome não apaga o arquivo enquanto houver
    outro), permissões e dono no inode (`ARQ_INFO` devolve o modo). O
    `formatar -t ext2` faz o que o `mkfs.ext2` faz (sparse_super,
    filetype, lost+found). O `montar` pergunta a cada servidor (FAT32,
    ext2) quem reconhece o disco. Conferido: o `e2fsck -f` aprova sem
    nenhum erro (blocos de 1 e 4 KiB, arquivos de 800 KB, apagar pastas e
    sobrescrever), e um disco feito pelo `mkfs.ext2` do Linux (com
    resize_inode, dir_index, ext_attr) é lido e gravado. Ainda não: criar
    links (`ln`) e links simbólicos; um disco ext3/ext4 é recusado.
- [x] **Etapa 4 — usuários e permissões** (núcleo 3.6; [livro, capítulo 11](livro/11-usuarios-e-permissoes.md))
  - usuário e grupo de cada processo no núcleo; os servidores de arquivos
    conferem as permissões
  - `/etc/passwd`, `/etc/group`, `/etc/sombra`; `login`, `su`, **`sudo`**,
    `passwd`, `adduser`, `chmod`, `chown`, `id`, `whoami`, `ls -l`
  - *Teste: um usuário comum não lê a `/etc/sombra` nem escreve em `/bin`;
    com `sudo`, escreve.*
  - Feito: a identidade (uid, gid, até 8 grupos) em cada processo
    (`SYS_IDENTIDADE`, `SYS_IDENTIDADE_MUDAR` só para o root); sinais só
    para os próprios processos. O servidor de contas (PID 10) confere as
    senhas (SHA-256 com sal, 1000 rodadas) e muda a identidade de quem
    pediu: `login`, `su`, `sudo` (grupo `sudo` ou `wheel`), `passwd`. O
    ramfs e o ext2 conferem `rwx` (com o x das pastas no caminho e o sticky
    do `/tmp`); o FAT32 aparece como 0777; disco cru e montar, só o root.
    Comandos: `adduser`, `chmod`, `chown`, `id`, `whoami`, `ls -l`, e o
    shell como `/bin/sh`. O terminal esconde a senha (`TERM_ECO`). Os
    usuários ainda moram no `/etc` da memória: ficam no disco na Etapa 5.
- [ ] **Etapa 5 — boot pelo disco**
  - o GRUB no disco (`instalar-boot`); a raiz no ext2; um `init` que monta
    os discos (`/etc/montagens`) e chama o `login`
  - *Teste: o sistema instalado no disco dá boot sem a ISO.*
- [ ] **Etapa 6 — threads e mutex**
  - threads no núcleo (o mesmo espaço de memória); espera/acorda
  - nlibc: `pthread_create`, `pthread_join`, `pthread_mutex_*`
- [ ] **Etapa 7 — `mmap` e memória compartilhada**
  - `mmap` anônimo; memória compartilhada entre processos (`shm_open`)
- [ ] **Etapa 8 — sockets locais (`AF_UNIX`)**
  - um servidor de sockets: `socket`, `bind`, `listen`, `accept`, `connect`
  - a base do X11, do Wayland e do D-Bus (Fases 5 a 7)
- [ ] **Etapa 9 — o modo manual (estilo Arch)**
  - o Livro 2: instalar o NovaEra à mão, passo a passo
  - a ISO de instalação
  - *Entrega: um sistema multiusuário montado à mão, dando boot no disco.*
