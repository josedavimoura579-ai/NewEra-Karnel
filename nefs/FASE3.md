# Fase 3 — o NEFS rodando 100% sobre o NovaEra

**Objetivo:** uma ISO que dá boot **no NovaEra** e traz o cproc + QBE, o
`nefs` e as ferramentas, sem nenhum Linux por baixo. Hoje a ISO do NEFS
usa um Linux mínimo como "oficina" (Fase 1); a Fase 3 troca essa oficina
pelo próprio NovaEra.

## O ponto de partida (medido)

| Peça | O que precisa do sistema | Tamanho |
|---|---|---|
| **QBE 1.3** | só C padrão: `stdio`, `stdlib`, `string`, `ctype`, `getopt` | ~17 mil linhas |
| **cproc** | C padrão + criar processos (`posix_spawnp`), `pipe`, `waitpid`, `mkstemp`, `unlink` | ~8 mil linhas |
| **Memória** | compilar o `kernel.c` usa cerca de 10 MiB por processo (cproc-qbe e qbe) | — |
| **Assembly** | o QBE gera só ~55 instruções diferentes e 15 diretivas | — |

No início da Fase 3, cada programa do NovaEra tinha só 64 KiB, e não
existiam arquivos, disco nem criação de processos. É isso que a fase
constrói.

## Decisões

- **Assembler e ligador:** escrever os nossos (`nefs-as` e `nefs-ld`),
  pequenos e legíveis, para o subconjunto que o QBE gera. O GNU `as`/`ld`
  depende da libbfd e é grande demais.
- **Pré-processador:** começar com um pequeno, só do NEFS; contribuir com
  o pré-processador do cproc depois.
- **Sistema de arquivos no disco:** decidir na Etapa 8 (FAT32 ou formato
  próprio).

## Etapas

Cada etapa termina com algo testável no QEMU e vira um pull request.

- [x] **Etapa 1 — núcleo pronto para programas de verdade**
  - programas **ELF** (os embutidos e os do initrd)
  - programas entregues pelo GRUB/QEMU num **initrd** (`initrd.tar`, módulo Multiboot)
  - memória maior por processo: **heap** (`SYS_SBRK`) e pilha que cresce sob demanda (até 1 MiB)
  - **criar processo** com argumentos, **terminar** com código de saída e **esperar** o filho
  - *Teste: o shell executa programas do initrd, com argumentos e código de saída.*
- [ ] **Etapa 2 — servidores de arquivos e terminal** (anel 3)
  - servidor de arquivos (VFS) com ramfs a partir do initrd; a criação de
    processos passa a receber o executável dele
  - servidor de terminal: entrada e saída padrão sobre vídeo e teclado, e pipes
  - *Teste: `ls`, `cat`, `echo > arquivo`.*
- [ ] **Etapa 3 — a libc do NovaEra ("nlibc")**
  - `malloc`, `stdio`, `string`, `ctype`, `stdlib`, `getopt`, `errno`,
    `posix_spawnp`, `pipe`, `waitpid`, por cima das mensagens
  - *Teste: programas em C padrão compilados pelo cproc rodando no NovaEra.*
- [ ] **Etapa 4 — QBE rodando no NovaEra** (primeiro marco grande)
  - *Teste: `qbe arquivo.ssa` gera, dentro do NovaEra, o mesmo assembly que no Linux.*
- [ ] **Etapa 5 — `nefs-as` e `nefs-ld`**
  - assembler para o subconjunto que o QBE gera; ligador ELF simples
  - os `.asm` do NovaEra reescritos na sintaxe AT&T, para um único assembler bastar
  - *Teste: o kernel montado e ligado por eles dá boot.*
- [ ] **Etapa 6 — cproc rodando no NovaEra**
  - pré-processador próprio do NEFS
  - *Teste: `cproc -c ola.c` dentro do NovaEra.*
- [ ] **Etapa 7 — autossuficiência** (o grande marco)
  - o NovaEra compila o próprio NovaEra; o `nefs` vira um programa nativo
  - *Teste: o `kernel.bin` compilado lá dentro dá boot.*
- [ ] **Etapa 8 — a ISO NEFS-sobre-NovaEra**
  - driver de disco (ATA) e sistema de arquivos para guardar os projetos
  - geração de ISO dentro do NovaEra
  - *Entrega: a ISO do NEFS sem Linux.*
