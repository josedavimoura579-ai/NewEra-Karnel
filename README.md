<div align="center">

# NovaEra Kernel

### A Nova Era Começou

**Um núcleo x86 escrito do zero — nascido num smartphone, livre para o mundo inteiro.**

![Versão](https://img.shields.io/badge/vers%C3%A3o-1.0-blue)
![Arquitetura](https://img.shields.io/badge/arquitetura-x86%2032--bit-informational)
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

O **NovaEra Kernel** é um núcleo para processadores **x86** escrito do começo ao fim em **Assembly (NASM)** e **C puro**, sem biblioteca padrão, sem copiar código de Linux, de Unix ou de Android. Quando você liga a máquina virtual, quem assume o processador é o nosso código: ele configura o modo protegido de 32 bits, constrói as próprias tabelas de interrupção, acorda o teclado, escreve diretamente na memória de vídeo e conversa com você através de um mini shell.

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

1. **`linker.ld`** — onde cada pedaço do kernel mora na memória (a partir de 1 MiB).
2. **`boot.asm`** — a porta de entrada: cabeçalho Multiboot, GDT, pilha, e os 48 *stubs* de interrupção.
3. **`kernel.c`** — o coração: driver VGA, terminal, IDT, PIC, teclado e o mini shell.
4. **`build.sh`** e **`run.sh`** — como tudo vira `kernel.bin` e como rodar.

### Como usar o NovaEra como "motor" do seu sistema

O NovaEra é a **base**: boot, memória de vídeo, interrupções e entrada de teclado. Sobre ele, cada pessoa constrói camadas:

```
┌──────────────────────────────────────────────┐
│  5. Distribuição   (o SEU sistema operacional) │  ← sua identidade, seus pacotes
├──────────────────────────────────────────────┤
│  4. Userland       (shell, ferramentas, apps)  │  ← futuro: modo usuário
├──────────────────────────────────────────────┤
│  3. Serviços       (arquivos, rede, gráficos)  │  ← futuro
├──────────────────────────────────────────────┤
│  2. Drivers        (teclado ✔, vídeo ✔, ...)   │  ← você adiciona
├──────────────────────────────────────────────┤
│  1. NovaEra Core   (boot, GDT, IDT, PIC)  ✔    │  ← este repositório
└──────────────────────────────────────────────┘
```

**Hoje**, o caminho é: faça um *fork* deste repositório, adicione arquivos `.c`/`.asm` para os seus drivers e comandos e ajuste o `build.sh` para compilá-los. **No futuro**, o NEFS definirá uma API estável do núcleo (ver [Roteiro](#roteiro)) para que o seu sistema possa atualizar o núcleo sem reescrever tudo.

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

Quer sentir o NEFS na prática? Abra o `kernel.c`, encontre a função `shell_executar()` e adicione um comando **antes** do último `else`:

```c
    } else if (str_igual(cmd, "ola")) {
        term_escreve("Ola, mundo NEFS!\n", ATTR(VGA_VERDE_CLARO, FUNDO));
```

Compile com `bash build.sh`, rode no QEMU, digite `ola` e aperte Enter. Você acabou de estender um sistema operacional que você entende por inteiro.

---

## Especificação técnica

Estado atual: **NovaEra Kernel v1.0** (alfa).

| Componente | Detalhes |
|---|---|
| **Arquitetura** | x86 (i386), modo protegido de 32 bits |
| **Formato do `kernel.bin`** | Executável ELF 32 bits com cabeçalho Multiboot |
| **Boot** | **Multiboot 1** — assinatura `0x1BADB002`, flags `0x3`; compatível com `qemu -kernel` e com o formato esperado por bootloaders como o GRUB |
| **Endereço de carga** | `0x100000` (1 MiB) |
| **Seções** | `.text` (com `.multiboot` no início), `.rodata`, `.data`, `.bss` — alinhadas em 4 KiB (`linker.ld`) |
| **Segmentação (GDT)** | 3 descritores *flat* de 4 GiB em anel 0: nulo, código (`0x08`) e dados (`0x10`) |
| **Pilha** | 16 KiB na seção `.bss` |
| **Interrupções (IDT)** | 256 entradas; vetores 0–31 = exceções da CPU, 32–47 = IRQs; *interrupt gates* (`0x8E`) |
| **PIC (8259)** | Remapeado: IRQ 0–7 → vetores 32–39, IRQ 8–15 → vetores 40–47. Apenas a **IRQ 1** (teclado) fica desmascarada |
| **Teclado** | PS/2 (i8042) via **IRQ 1**; dados na porta `0x60`, status/comando na `0x64`; *scancodes* set 1, layout US; Shift e Caps Lock; fila circular de 256 teclas |
| **Vídeo** | Modo texto VGA 80×25 com escrita **direta na memória física `0xB8000`** (16 bits por célula: caractere CP437 + atributo de cor), cursor de hardware via portas `0x3D4`/`0x3D5`, rolagem por software |
| **Mini shell** | `limpar` (limpa a tela) · `reboot` (envia `0xFE` à porta `0x64`; plano B: *triple fault*) · qualquer outra coisa → "Comando desconhecido" |
| **Diagnóstico** | Exceções da CPU mostram um painel vermelho com vetor, código de erro, EIP, CS e EFLAGS |
| **Linguagens** | Assembly NASM + C *freestanding* (`clang --target=i386-none-elf`), ligado com `ld.lld` |
| **Dependências em tempo de execução** | Nenhuma. Sem libc, sem `#include` |

### O que o NovaEra **ainda não** é

Transparência importa. Hoje o NovaEra é um núcleo mínimo de aprendizado, **ainda monolítico e sem**: gerenciador de memória, paginação, multitarefa, modo usuário/chamadas de sistema, driver de disco ou sistema de arquivos. A meta de longo prazo é evoluir para um **microkernel**, com serviços fora do núcleo. Veja o [Roteiro](#roteiro).

### Estrutura do repositório

```
novaera-kernel/
├── boot.asm          # Multiboot, GDT, pilha, stubs de interrupção (NASM)
├── kernel.c          # VGA, terminal, IDT, PIC, teclado, mini shell (C puro)
├── linker.ld         # Mapa de memória a partir de 1 MiB
├── build.sh          # Compila e liga: boot.asm + kernel.c -> kernel.bin
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

Ou baixe o instalador na [página oficial do QEMU](https://www.qemu.org/download/#windows). O QEMU é instalado em `C:\Program Files\qemu`. Se o comando `qemu-system-i386` não for reconhecido, adicione essa pasta ao `PATH` do Windows ou use o caminho completo (veja o Passo 3).

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
qemu-system-i386 -kernel kernel.bin -m 64M
```

> Se o seu QEMU não tiver `qemu-system-i386`, use `qemu-system-x86_64` com os mesmos parâmetros.
>
> **Windows sem `PATH` configurado:**
> ```powershell
> & "C:\Program Files\qemu\qemu-system-i386.exe" -kernel kernel.bin -m 64M
> ```

Uma janela abre com um PC virtual. Em menos de um segundo você verá a tela azul com a mensagem:

```
╔════════════════════════════════════════════╗
║  NovaEra Kernel v1.0 - A Nova Era Começou  ║
╚════════════════════════════════════════════╝
```

seguida dos testes de inicialização (`[ OK ]`) e do prompt `NovaEra>`.

### Passo 4 — Converse com o kernel

Clique dentro da janela do QEMU para dar foco ao teclado e digite:

| Comando | O que acontece |
|---|---|
| `limpar` | Limpa a tela. |
| `reboot` | Reinicia a máquina virtual (o kernel sobe de novo). |
| *qualquer outra coisa* | Mostra "Comando desconhecido" e a lista de comandos. |

Para sair, feche a janela do QEMU.

### Compilando a partir do código-fonte

**No Linux** (Debian/Ubuntu; em outras distros, instale os equivalentes de `nasm`, `clang`, `lld` e `qemu`):

```bash
sudo apt install nasm clang lld qemu-system-x86
git clone https://github.com/josedavimoura579-ai/NewEra-Karnel.git
cd novaera-kernel
bash build.sh
qemu-system-i386 -kernel kernel.bin -m 64M
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
- [ ] Temporizador (PIT, IRQ 0): relógio do sistema e `sleep`
- [ ] Leitura do mapa de memória entregue pelo bootloader
- [ ] Gerenciador de memória física
- [ ] Paginação e *heap* do kernel (`kmalloc`)
- [ ] Multitarefa cooperativa e depois preemptiva
- [ ] Modo usuário e chamadas de sistema
- [ ] **API estável do NovaEra Core** para sistemas derivados (NEFS)
- [ ] Driver de disco e sistema de arquivos
- [ ] Imagem ISO inicializável
- [ ] Evolução para **microkernel** (serviços fora do núcleo)
- [ ] Versão x86_64

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

**NovaEra Kernel** is a from-scratch **x86 (32-bit protected mode)** kernel written in NASM Assembly and freestanding C, started and built entirely on an Android phone using Termux. It boots via **Multiboot**, sets up its own GDT, IDT and remapped 8259 PIC, handles the **PS/2 keyboard on IRQ 1**, writes directly to **VGA text memory at `0xB8000`**, and offers a tiny kernel shell (`limpar` clears the screen, `reboot` resets the machine through the keyboard controller port `0x64`).

**NEFS (NovaEra From Scratch)** is our methodology: read the code, run it, modify it, extend it and ship your own operating system on top of a minimal, fully readable core, whether in an Arch-like (minimal, build-it-yourself) or Debian-like (curated, stable) spirit.

To try it: download `kernel.bin`, install [QEMU](https://www.qemu.org), and run:

```bash
qemu-system-i386 -kernel kernel.bin -m 64M
```

The project is free, open source (MIT) and independent. Contributions are welcome.
