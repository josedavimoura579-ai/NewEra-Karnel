<div align="center">

# NovaEra Microkernel

### A Nova Era Começou

**Um microkernel x86_64 escrito do zero — nascido num smartphone, livre para o mundo inteiro.**

![Versão](https://img.shields.io/badge/vers%C3%A3o-3.0-blue)
![Arquitetura](https://img.shields.io/badge/arquitetura-x86__64-informational)
![Boot](https://img.shields.io/badge/boot-Multiboot-success)
![Licença](https://img.shields.io/badge/licen%C3%A7a-MIT-green)
![Status](https://img.shields.io/badge/status-alfa-orange)

[Rodar o kernel](#rodando-o-kernel-no-seu-computador) ·
[Metodologia NEFS](#metodologia-nefs) ·
[Especificação técnica](#especificação-técnica) ·
[Roteiro](#roteiro) ·
[English summary](#english-summary)

</div>

---

## O nascimento

Tudo começou sem um computador. Sem notebook, sem estação de trabalho, sem servidor: apenas um **celular Android**, o terminal **Termux** e uma pergunta ousada — *e se um sistema operacional pudesse nascer do zero, em qualquer lugar, nas mãos de qualquer pessoa?*

A resposta é este repositório.

O **NovaEra** é um **microkernel** de **64 bits** para processadores **x86_64** escrito do começo ao fim em **Assembly (NASM)** e **C puro**, sem biblioteca padrão, sem copiar código de Linux, de Unix ou de Android. Quando você liga a máquina virtual, quem assume o processador é o nosso código: ele sai do modo de 32 bits em que o bootloader o entrega, entra no modo longo de 64 bits, liga a paginação de 4 níveis, cria processos isolados no anel 3 e passa a dividir a CPU entre eles. O vídeo, o teclado e o shell **não moram no núcleo**: são programas separados, cada um na sua própria memória, que conversam trocando mensagens.

É pequeno. É honesto. É nosso — e agora é **seu**.

> Ferramentas como NASM, Clang, LLD e QEMU são software livre e rodam num celular. O que antes pedia uma bancada inteira agora cabe no bolso.

## Filosofia

O NovaEra existe para iniciar uma **nova era** na forma como as pessoas aprendem e constroem sistemas operacionais. Nossos três pilares:

| Pilar | O que significa |
|---|---|
| **Código aberto** | Cada linha pode ser lida, estudada, copiada, modificada e redistribuída. Nada fica escondido numa caixa-preta. |
| **Gratuito** | Sem assinatura, sem licença paga, sem cadastro. Baixou, rodou, é seu. |
| **Independente** | O núcleo é escrito do zero. Não herda código de nenhum sistema existente e não depende da permissão de ninguém para evoluir. |

Acreditamos que **entender o que roda por baixo do seu computador é um direito**, não um privilégio. Um sistema fechado pede que você confie. Um sistema aberto **mostra o código**. Este projeto escolhe mostrar.

---

## Metodologia NEFS

*NEFS = NovaEra From Scratch.*

**NEFS** é o nosso método para que qualquer programador, em qualquer lugar do mundo, **leia** um sistema operacional de ponta a ponta, **entenda** cada camada e **construa o seu próprio** a partir de um núcleo x86 puro.

A ideia central é simples: *você só domina o que construiu desde o começo.*

### Os 5 degraus do NEFS

| Degrau | Nome | O que você faz |
|:---:|---|---|
| 1 | **Ler** | Leia o código na ordem sugerida abaixo. São poucos arquivos, bem comentados em português. |
| 2 | **Rodar** | Execute o `kernel.bin` no QEMU e veja o resultado ([manual](#rodando-o-kernel-no-seu-computador)). |
| 3 | **Modificar** | Mude uma cor, uma mensagem, um comando. Compile. Veja a diferença. |
| 4 | **Estender** | Adicione um driver, um comando, um serviço. O núcleo é o motor; o resto é seu. |
| 5 | **Distribuir** | Publique o seu sistema, com o seu nome, a sua identidade e a sua licença. |

### Ordem de leitura do código

1. **`novaera.h`** — o contrato entre o núcleo e os programas: chamadas de sistema, formato das mensagens, PIDs e o mapa de memória.
2. **`sistema.conf`** e **`programas.lista`** — a identidade do sistema e os processos que o núcleo inicia no boot.
3. **`linker.ld`** — onde cada pedaço do núcleo mora na memória (a partir de 1 MiB).
4. **`boot.asm`** — a porta de entrada: Multiboot, a ponte de 32 para 64 bits, GDT com anéis 0 e 3, os *stubs* de interrupção, a `int 0x80` e a troca de contexto.
5. **`cpu.asm`** — as instruções especiais da CPU (portas, CR3, IDT, TSS), chamadas pelo C como funções comuns.
6. **`kernel.c`** — o microkernel: memória física, paginação, processos, escalonador, IPC e supervisão.
7. **`usuario/`** — o que roda fora do núcleo: `crt0.asm` e `ulib.h` (biblioteca), `video.c`, `teclado.c` e `shell.c`.
8. **`build.sh`** e **`run.sh`** — como tudo vira `kernel.bin` e como rodar.

### Como usar o NovaEra como "motor" do seu sistema

O NovaEra é a **base**: um núcleo que só isola, escalona e entrega mensagens. Sobre ele, cada pessoa constrói camadas, **sempre fora do núcleo**:

```
┌──────────────────────────────────────────────┐
│  5. Distribuição   (o SEU sistema operacional) │  ← sua identidade, seus pacotes
├──────────────────────────────────────────────┤
│  4. Userland       (shell ✔, ferramentas)      │  ← anel 3
├──────────────────────────────────────────────┤
│  3. Serviços       (arquivos, rede, gráficos)  │  ← anel 3 (futuro)
├──────────────────────────────────────────────┤
│  2. Drivers        (teclado ✔, vídeo ✔, ...)   │  ← anel 3, por mensagens
├──────────────────────────────────────────────┤
│  1. Microkernel    (memória, processos, IPC) ✔ │  ← anel 0, este repositório
└──────────────────────────────────────────────┘
```

**Hoje**, o caminho é o comando **`nefs`** (pasta [`nefs/`](nefs/)): `nefs novo meu-so` cria o seu sistema, `nefs programa` cria um processo novo e o registra em `programas.lista`, e `nefs iso` gera a ISO. O núcleo não precisa saber o que o seu programa faz: ele só entrega as mensagens. A interface entre os dois lados é o `novaera.h`, o embrião da **API estável do NovaEra Core** (ver [Roteiro](#roteiro)).

### Dois estilos de sistema construídos sobre o NEFS

O NEFS propõe duas filosofias de distribuição — inspiradas no que a comunidade já conhece e ama. São **diretrizes de projeto** para quem for construir, não produtos prontos:

| | **Estilo Raiz** *(à la Arch)* | **Estilo Estável** *(à la Debian)* |
|---|---|---|
| **Lema** | "Monte do seu jeito." | "Funciona, sempre." |
| **Ponto de partida** | Núcleo mínimo; você escolhe cada componente. | Conjunto fechado e testado de componentes. |
| **Atualizações** | Contínuas, sempre na versão mais nova. | Lançamentos espaçados, com ênfase em estabilidade. |
| **Para quem** | Quem quer controle total e aprender fazendo. | Quem quer um sistema previsível para o dia a dia. |
| **Exemplo de uso do NovaEra** | Fork enxuto: só o Core + os drivers que você escrever. | Fork com um conjunto curado de drivers e comandos congelado por versão. |

### Primeira modificação (5 minutos)

Quer sentir o NEFS na prática? Abra o `usuario/shell.c`, encontre a função `executar()` e adicione um comando **antes** do último `else`:

```c
    else if (str_igual(cmd, "ola"))     escreve("Ola, mundo NEFS!\n", COR(COR_VERDE_CLARO));
```

Compile com `bash build.sh`, rode no QEMU, digite `ola` e aperte Enter. Repare que você mudou o sistema **sem tocar no núcleo**: o shell é só um programa que pede ao servidor de vídeo para escrever. É assim que um microkernel cresce.

---

## Especificação técnica

Estado atual: **NovaEra Microkernel v3.0** (alfa).

| Componente | Detalhes |
|---|---|
| **Arquitetura** | **x86_64**, modo longo de 64 bits. Na inicialização, `boot.asm` confere a CPU (CPUID), monta tabelas de página para o 1º GiB, liga PAE + `EFER.LME` + paginação e salta para o código de 64 bits |
| **Formato do `kernel.bin`** | Binário puro com cabeçalho Multiboot e endereços explícitos (bit 16 das flags, o "a.out kludge"): é assim que um kernel de 64 bits carrega por um protocolo que só entende ELF de 32 bits |
| **Boot** | **Multiboot 1** — assinatura `0x1BADB002`, flags `0x10003`; compatível com `qemu -kernel` e com o GRUB |
| **Endereço de carga** | `0x100000` (1 MiB) |
| **Seções** | `.text` (com `.multiboot` no início), `.rodata`, `.data`, `.bss` — alinhadas em 4 KiB (`linker.ld`) |
| **Segmentação (GDT)** | Código de 64 bits e dados do núcleo (`0x08`/`0x10`, anel 0) e do usuário (`0x1B`/`0x23`, anel 3), mais a TSS de 64 bits (`0x28`, 16 bytes) |
| **Memória física** | Lê `mem_upper` do Multiboot; quadros de 4 KiB acima do núcleo numa lista livre encadeada (alocar e liberar em O(1)); até 1 GiB |
| **Paginação** | 4 níveis (PML4 → PDPT → PD → PT), um espaço de endereçamento por processo. O 1º GiB é do núcleo (páginas de 2 MiB), presente em todos os processos mas só para o anel 0. Programa em `0x40000000` (64 KiB), pilha em `0x40100000` (16 KiB) |
| **Processos** | Até 8. PID 0 = núcleo (ocioso, `hlt`), 1 = vídeo, 2 = teclado, 3 = shell. Cada um tem pilha de núcleo própria; a TSS aponta para ela |
| **Escalonador** | Preemptivo, *round-robin*, com o timer PIT a **100 Hz** (IRQ 0) |
| **Chamadas de sistema** | `int 0x80` (única porta da IDT com DPL 3): `ENVIAR`, `RECEBER`, `CHAMAR`, `RESPONDER`, `CEDER`, `IRQ_ASSINAR`, `TEMPO`, `TAREFA_INFO`, `MEMORIA`, `SAIR` |
| **IPC** | Síncrono, por mensagens de 80 bytes copiadas pelo núcleo entre espaços de endereçamento (estilo L4/MINIX). `RESPONDER` nunca bloqueia, para que um servidor não fique refém de um cliente |
| **Interrupções** | IRQ 0 é do núcleo. As outras viram **mensagens `MSG_IRQ`** para o driver que as assinou; o PIC só libera uma IRQ quando alguém a assina |
| **Drivers** | No anel 3 com IOPL 3 (podem usar `in`/`out`). **Vídeo**: o único processo com `0xB8000` mapeado. **Teclado**: PS/2 *set 1*, layout US, Shift e Caps Lock, também faz o `reboot` pela porta `0x64` |
| **Supervisão** | Uma exceção no anel 3 encerra só o processo culpado, devolve a memória dele e o recarrega (até 5 vezes), avisando-o do motivo. Uma exceção no anel 0 mostra o painel vermelho de pânico |
| **Shell (anel 3)** | `ajuda` · `limpar` · `ps` · `mem` · `tempo` · `sobre` · `invadir` (demonstra a proteção de memória) · `reboot` |
| **Linguagens** | Assembly NASM + C11 *freestanding*, sem assembly embutido nem atributos de compilador: compila com o clang (`CC=clang`, padrão) ou com o cproc + QBE do NEFS (`CC=cproc`). Liga com `ld.lld` ou com o `ld` do binutils |
| **Configuração** | `sistema.conf` (nome, versão, lema, prompt) e `programas.lista` (processos e permissões); o `build.sh` gera `build/sistema.h`, `build/programas.h` e `build/programas.asm` |
| **Dependências em tempo de execução** | Nenhuma. Sem libc; os `#include` são a ABI (`novaera.h`) e os arquivos gerados pelo build |

### Por que agora é um microkernel

Num núcleo **monolítico** (como era o NovaEra 1.0, e como é o Linux), drivers e serviços rodam dentro do núcleo, com poder total: um bug no driver de teclado derruba tudo. Num **microkernel**, o núcleo faz só o indispensável, e todo o resto vira processo comum:

```
   anel 3 │  ┌────────┐  mensagens  ┌─────────┐  mensagens  ┌─────────┐
(usuário) │  │ shell  │ ──────────► │  vídeo  │             │ teclado │
          │  │ PID 3  │ ◄────────────────────────────────── │  PID 2  │
          │  └────────┘                                     └─────────┘
          │        │ int 0x80                                    ▲ MSG_IRQ
   ───────┼────────┼─────────────────────────────────────────────┼──────
   anel 0 │        ▼          NovaEra Microkernel                │
 (núcleo) │   memória · paginação · processos · escalonador · IPC · IRQ 1
```

Experimente no shell:

- **`ps`** mostra os processos e em que anel cada um roda.
- **`invadir`** faz o shell tentar ler a memória do núcleo. A paginação proíbe, a CPU gera uma falha de página, e o núcleo encerra **só o shell**, libera a memória dele e o reinicia. Vídeo e teclado nem percebem. Rode `ps` em seguida e veja o contador de reinícios.

### O que o NovaEra **ainda não** tem

Transparência importa. Ainda faltam: suporte a mais de 1 GiB de RAM, servidor de nomes (os PIDs dos servidores são fixos), carregar programas do disco (hoje eles vão embutidos no `kernel.bin`), mapa de portas de E/S por driver (hoje um driver com IOPL 3 pode acessar qualquer porta), memória compartilhada, driver de disco e sistema de arquivos. Veja o [Roteiro](#roteiro).

### Estrutura do repositório

```
novaera-kernel/
├── novaera.h         # A ABI: syscalls, mensagens, PIDs, mapa de memória
├── sistema.conf      # Nome, versão, lema e prompt do sistema
├── programas.lista   # Os processos que o núcleo inicia no boot
├── boot.asm          # Multiboot, GDT/TSS, stubs, int 0x80, troca de contexto (NASM)
├── cpu.asm           # Instruções especiais da CPU como funções (NASM)
├── kernel.c          # O microkernel: memória, paginação, processos, IPC (C puro)
├── linker.ld         # Mapa de memória do núcleo a partir de 1 MiB
├── usuario/          # Tudo o que roda no anel 3
│   ├── crt0.asm      #   _start, int 0x80 e E/S de portas
│   ├── ulib.h        #   biblioteca: syscalls, mensagens, utilitários
│   ├── usuario.ld    #   todo programa mora em 0x40000000
│   ├── video.c       #   servidor de vídeo (PID 1)
│   ├── teclado.c     #   driver de teclado PS/2 (PID 2)
│   └── shell.c       #   o shell (PID 3)
├── build.sh          # Compila os programas, embute-os e liga o kernel.bin
├── nefs/             # O NEFS: comando nefs, Livro do NEFS e a ISO (separado do núcleo)
├── run.sh            # Atalho para rodar no QEMU (pensado para o Termux)
├── kernel.bin        # Binário pronto para rodar
└── README.md
```

---

## Rodando o kernel no seu computador

Você **não precisa compilar nada**. Basta baixar o `kernel.bin` e rodá-lo no **QEMU**, um emulador livre que simula um PC x86 completo.

### Passo 1 — Baixe o `kernel.bin`

Escolha uma das formas:

- **Pelo navegador:** abra o arquivo `kernel.bin` neste repositório e clique em **Download raw file** (ícone de download).
- **Pelo terminal (Linux e macOS):**
  ```bash
  curl -L -o kernel.bin https://github.com/josedavimoura579-ai/NewEra-Karnel/raw/main/kernel.bin
  ```
- **Pelo PowerShell (Windows):**
  ```powershell
  Invoke-WebRequest -Uri https://github.com/josedavimoura579-ai/NewEra-Karnel/raw/main/kernel.bin -OutFile kernel.bin
  ```

### Passo 2 — Instale o QEMU

<details open>
<summary><b>🪟 Windows</b></summary>

Pelo terminal (PowerShell ou Prompt de Comando):

```powershell
winget install --id SoftwareFreedomConservancy.QEMU
```

Ou baixe o instalador na [página oficial do QEMU](https://www.qemu.org/download/#windows). O QEMU é instalado em `C:\Program Files\qemu`. Se o comando `qemu-system-x86_64` não for reconhecido, adicione essa pasta ao `PATH` do Windows ou use o caminho completo (veja o Passo 3).

</details>

<details>
<summary><b>🍎 macOS</b></summary>

Com o [Homebrew](https://brew.sh):

```bash
brew install qemu
```

</details>

<details>
<summary><b>🐧 Linux</b></summary>

```bash
# Debian / Ubuntu / Mint
sudo apt install qemu-system-x86

# Arch / Manjaro
sudo pacman -S qemu-system-x86

# Fedora
sudo dnf install qemu-system-x86
```

</details>

### Passo 3 — Rode

Na pasta onde está o `kernel.bin`:

```bash
qemu-system-x86_64 -kernel kernel.bin -m 64M
```

> O NovaEra é 64 bits: use `qemu-system-x86_64`. Com `qemu-system-i386`, a tela mostra um aviso vermelho e o sistema para.
>
> **Windows sem `PATH` configurado:**
> ```powershell
> & "C:\Program Files\qemu\qemu-system-x86_64.exe" -kernel kernel.bin -m 64M
> ```

Uma janela abre com um PC virtual. Em menos de um segundo você verá a tela azul com a mensagem:

```
╔════════════════════════════════════════════════════════╗
║  NovaEra Microkernel v3.0 x86_64 - A Nova Era Começou  ║
╚════════════════════════════════════════════════════════╝
```

seguida dos testes de inicialização (`[ OK ]`), da entrega da CPU aos processos do usuário e do prompt `NovaEra>`.

### Passo 4 — Converse com o kernel

Clique dentro da janela do QEMU para dar foco ao teclado e digite:

| Comando | O que acontece |
|---|---|
| `ajuda` | Lista os comandos. |
| `limpar` | Limpa a tela. |
| `ps` | Lista os processos, o anel, o estado e quantas vezes foram reiniciados. |
| `mem` | Mostra a memória física livre. |
| `tempo` | Mostra há quanto tempo o sistema está ligado. |
| `sobre` | Explica, em quatro linhas, o que é um microkernel. |
| `invadir` | O shell tenta ler a memória do núcleo, é encerrado e reiniciado. |
| `reboot` | Reinicia a máquina virtual (quem faz isso é o driver de teclado, fora do núcleo). |

Para sair, feche a janela do QEMU.

### Compilando a partir do código-fonte

**No Linux** (Debian/Ubuntu; em outras distros, instale os equivalentes de `nasm`, `clang`, `lld` e `qemu`):

```bash
sudo apt install nasm clang lld qemu-system-x86
git clone https://github.com/josedavimoura579-ai/NewEra-Karnel.git
cd novaera-kernel
bash build.sh
qemu-system-x86_64 -kernel kernel.bin -m 64M
```

**No Android (Termux)** — o caminho em que o NovaEra nasceu:

```bash
pkg update && pkg install nasm clang lld git
pkg install qemu-system-x86-64     # o nome do pacote pode variar entre versões do Termux
git clone https://github.com/josedavimoura579-ai/NewEra-Karnel.git
cd novaera-kernel
bash build.sh
bash run.sh            # tela de texto dentro do Termux; use "bash run.sh vnc" para janela gráfica via app VNC
```

**No Windows e no macOS:** o jeito mais simples de compilar é usar o WSL (Windows) ou uma máquina virtual/contêiner Linux e seguir os passos acima. Para apenas **rodar**, o `kernel.bin` pronto funciona em qualquer sistema com QEMU.

> **Dica de portabilidade:** os scripts `build.sh` e `run.sh` foram escritos para o Termux. Fora dele, chame-os sempre com `bash build.sh` e `bash run.sh`.

---

## Roteiro

- [x] Boot via Multiboot e controle do processador
- [x] GDT própria e modo protegido de 32 bits
- [x] Driver de vídeo VGA em modo texto (`0xB8000`) com cursor e rolagem
- [x] IDT, tratamento de exceções e PIC remapeado
- [x] Driver de teclado PS/2 (IRQ 1)
- [x] Mini shell do kernel (`limpar`, `reboot`)
- [x] Temporizador (PIT, IRQ 0) a 100 Hz
- [x] Leitura da memória disponível entregue pelo bootloader
- [x] Gerenciador de memória física
- [x] Paginação com um espaço de endereçamento por processo
- [x] Multitarefa preemptiva (*round-robin*)
- [x] Modo usuário (anel 3) e chamadas de sistema (`int 0x80`)
- [x] **Microkernel**: IPC por mensagens, IRQs como mensagens, drivers e shell fora do núcleo
- [x] Supervisão: processo que falha é isolado e reiniciado
- [x] **Versão x86_64** (modo longo, paginação de 4 níveis)
- [ ] Servidor de nomes (sem PIDs fixos)
- [ ] Mapa de portas de E/S por driver (TSS *I/O bitmap*)
- [ ] Memória compartilhada entre processos
- [ ] Carregar programas como módulos Multiboot / do disco
- [ ] **API estável do NovaEra Core** para sistemas derivados (NEFS)
- [ ] Driver de disco e sistema de arquivos (como servidores)
- [ ] Imagem ISO inicializável

### Roteiro do NEFS: o compilador

O objetivo do NEFS é uma ISO com um compilador que roda **dentro** do NovaEra e tem comandos para cada pessoa construir o próprio sistema. A base escolhida é o **[QBE](https://c9x.me/compile/)**, um *backend* de compilador pequeno o bastante para ser lido por uma só pessoa, junto com o **[cproc](https://sr.ht/~mcf/cproc/)**, que entende C e gera código para o QBE:

```
arquivo .c → cproc → QBE → assembler → linker → programa do NovaEra
```

O QBE gera código para x86_64, e foi por isso que o NovaEra passou para 64 bits.

- [ ] **Fase 1** — a ISO do NEFS: um Linux mínimo com cproc + QBE, o comando `nefs` e o Livro 1. Já prontos e testados: o comando `nefs`, o Livro 1, o NovaEra compilável sem `__asm__` e a ISO (boot, QEMU e GRUB dentro dela). Falta testar o cproc + QBE na ISO. Veja [`nefs/README.md`](nefs/README.md)
- [ ] **Fase 2** — preparar o núcleo: programas ELF, sistema de arquivos, criação de processos e uma libc mínima
- [ ] **Fase 3** — portar cproc + QBE + assembler + linker para o NovaEra e gerar a **ISO do compilador NEFS**

---

## Como contribuir

O NEFS cresce com gente de todo o mundo. Você pode ajudar de muitas formas:

1. **Teste** o `kernel.bin` em outros ambientes (QEMU em diferentes sistemas, outros emuladores, hardware real) e abra uma *issue* contando o resultado.
2. **Leia o código** e aponte o que ficou confuso — documentação clara é uma contribuição valiosa.
3. **Implemente** um item do [Roteiro](#roteiro): faça um *fork*, crie uma branch, e abra um *pull request*.
4. **Traduza** este README e os comentários para o seu idioma.
5. **Construa o seu sistema** sobre o NovaEra e conte para nós. A ideia é exatamente essa.

Ao contribuir, mantenha o código **simples, comentado e sem dependências externas**: o NovaEra deve poder ser entendido por inteiro por uma única pessoa.

## Licença

Distribuído sob a licença **MIT**: use, estude, modifique e redistribua livremente, inclusive em projetos próprios. Veja o arquivo [`LICENSE`](LICENSE).

---

## Dedicatória

<div align="center">

*Para quem aprende fazendo, para quem constrói com o que tem na mão,*
*e para todo mundo que acredita que o conhecimento deve ser livre.*

### "O povo te trata como um rei se você for de graça."

— A frase histórica do projeto NovaEra

</div>

---

## English summary

**NovaEra** is a from-scratch **x86_64 (64-bit long mode) microkernel** written in NASM Assembly and freestanding C, started and built entirely on an Android phone using Termux. The kernel (ring 0) only does physical memory management, per-process paging, preemptive round-robin scheduling (PIT at 100 Hz), synchronous message-passing IPC over `int 0x80`, and delivers hardware IRQs as messages. The NEFS toolchain will be built on **QBE** + **cproc**. The **VGA video server, the PS/2 keyboard driver and the shell all run in ring 3** as isolated processes that talk through messages. If a user process faults, the kernel kills only that process, frees its memory and restarts it: try the `invadir` command, which makes the shell read kernel memory.

**NEFS (NovaEra From Scratch)** is our methodology: read the code, run it, modify it, extend it and ship your own operating system on top of a minimal, fully readable core, whether in an Arch-like (minimal, build-it-yourself) or Debian-like (curated, stable) spirit.

To try it: download `kernel.bin`, install [QEMU](https://www.qemu.org), and run:

```bash
qemu-system-x86_64 -kernel kernel.bin -m 64M
```

The project is free, open source (MIT) and independent. Contributions are welcome.
