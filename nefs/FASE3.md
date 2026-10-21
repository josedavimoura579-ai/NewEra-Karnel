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
- [x] **Etapa 2 — servidores de arquivos, terminal e pipes** (anel 3)
  - servidor de **arquivos** (ramfs) montado a partir do initrd, que o núcleo
    mapeia só para ele; o núcleo deixou de ler tar: a criação de processos
    recebe o ELF já lido por quem cria
  - **terminal** (TTY): edição de linha, eco, Ctrl+D; o teclado entrega as
    teclas a ele
  - **pipes**, e o mesmo protocolo de leitura/escrita para terminal, pipes
    e arquivos; entrada/saída padrão e pasta atual herdadas pelos filhos
  - `SYS_COPIAR_PARA`/`SYS_COPIAR_DE`: um servidor copia dados direto da/para
    quem o chamou
  - shell com `cd`, `pwd`, `|`, `>`, `>>`, `<`; comandos `ls`, `cat`, `echo`,
    `mkdir`, `rm` e **`novafetch`**
  - versão do núcleo separada da versão da distribuição (`PURO`, `logo.txt`,
    `/etc/sistema`); o vídeo mostra acentos (UTF-8 → CP437)
  - *Teste: `ls /bin | cat`, `echo oi > /tmp/a`, `cat < /tmp/a`, `novafetch`.*
- [x] **Etapa 3 — a libc do NovaEra ("nlibc")**
  - `stdio` (com `printf("%.17g")` exato e `fscanf("_%lf")`, que o cproc e o
    QBE usam), `stdlib` (`malloc` por classes de tamanho, `qsort` estável,
    `strtod` com arredondamento correto, por bignum), `string`, `ctype`,
    `errno`, `getopt`, `assert`
  - POSIX do cproc: descritores, `pipe`, `posix_spawnp` (com dup2 para a
    entrada e a saída), `waitpid`/`wait`, `mkstemp`, `unlink`
  - pipes com contagem de referências por ponta; `exit()` devolve as
    referências; canal com `CANAL_DONO` para os filhos de `posix_spawn`
  - núcleo: SSE ligado e FPU salva por processo; `SYS_ESPERAR(0)` = qualquer filho
  - *Teste: `testelibc`, 47 testes conferidos antes com a glibc no Linux,
    passa no NovaEra compilado com clang e com cproc + QBE.*
- [x] **Etapa 4 — QBE rodando no NovaEra** (primeiro marco grande)
  - **portes** (`portes/<nome>/receita`): programas de fora compilados do
    código original, sem nenhuma mudança, com a nlibc; `portes/baixar.sh`
    baixa e confere o SHA-256, e o `build.sh` os põe no initrd
  - o QBE 1.3 é o primeiro porte: `/bin/qbe` e exemplos em `/usr/share/qbe`
  - o servidor de vídeo copia a saída para a porta serial (COM1): no QEMU,
    `-serial stdio` mostra tudo do lado de fora
  - *Teste: os 76 arquivos de teste do QBE e o `kernel.c` em SSA (70 KB),
    compilados dentro do NovaEra, dão o mesmo assembly, byte a byte, que o
    QBE no Linux. O QBE compilado com clang e com cproc + QBE.*
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
