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

- [ ] **Etapa 1 — sinais e `kill`**
  - sinais no núcleo: enviar, tratar (função do programa, com volta pelo
    núcleo), ignorar; o padrão encerra o processo
  - grupos de processos: **Ctrl+C** encerra o comando do primeiro plano
  - uma falha (falha de página, divisão por zero) vira SIGSEGV/SIGFPE
  - nlibc: `signal`, `sigaction`, `kill`, `raise`, `WIFSIGNALED`; SIGPIPE
  - comando `kill`; o shell mostra quem foi interrompido
  - *Teste: Ctrl+C num programa em laço e num pipe travado; um tratador de
    SIGINT roda e o programa continua.*
- [ ] **Etapa 2 — discos, partições e montagens**
  - o driver ATA com os quatro discos dos dois canais; partições MBR
  - `particionar` (o fdisk do NovaEra), `montar` e `desmontar`
  - *Teste: um disco com duas partições, uma FAT32 montada onde se quiser.*
- [ ] **Etapa 3 — o ext2**
  - servidor ext2: pastas, arquivos, links, permissões guardadas no disco
  - `formatar -t ext2` (o mkfs.ext2 do NovaEra)
  - *Teste: o `fsck.ext2` do Linux aprova o que o NovaEra grava, e o
    NovaEra lê um disco feito pelo `mkfs.ext2`.*
- [ ] **Etapa 4 — usuários e permissões**
  - usuário e grupo de cada processo no núcleo; os servidores de arquivos
    conferem as permissões
  - `/etc/passwd`, `/etc/group`, `/etc/sombra`; `login`, `su`, **`sudo`**,
    `passwd`, `adduser`, `chmod`, `chown`, `id`, `whoami`, `ls -l`
  - *Teste: um usuário comum não lê a `/etc/sombra` nem escreve em `/bin`;
    com `sudo`, escreve.*
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
