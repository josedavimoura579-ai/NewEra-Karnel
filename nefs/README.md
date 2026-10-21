# NEFS — NovaEra From Scratch

**O NEFS é uma ISO para você criar o seu próprio sistema operacional sobre
o microkernel NovaEra.** Você liga a ISO numa máquina virtual, ela abre um
terminal com o compilador e as ferramentas prontas, e o comando `nefs`
te leva do zero até a ISO do *seu* sistema.

O NEFS **não faz parte do microkernel**: o NovaEra continua puro na raiz
deste repositório. O NEFS é a oficina, separada, que usa o NovaEra como
modelo.

```
┌─────────────────── ISO do NEFS (a oficina) ───────────────────┐
│  Linux mínimo + BusyBox + cproc + QBE + nefs + QEMU + GRUB    │
│                                                               │
│     nefs novo meu-so  ──►  meu-so/  ──►  meu-so.iso           │
│                            (o SEU sistema, com o NovaEra)     │
└───────────────────────────────────────────────────────────────┘
```

## O compilador: cproc + QBE

```
arquivo .c → cproc → QBE → as → ld → programa do NovaEra
```

- **[cproc](https://sr.ht/~mcf/cproc/)** entende C11 e gera o código
  intermediário do QBE.
- **[QBE](https://c9x.me/compile/)** transforma esse código em assembly
  x86_64. São cerca de 10 mil linhas de C: dá para ler inteiro.
- **as** e **ld** (binutils) montam e ligam; **nasm** monta as partes do
  NovaEra escritas em assembly.

Versões testadas juntas: **QBE 1.3** e **cproc** no commit `d1c53dd`
(fixado em `iso/construir.sh`). O pré-processador do cproc ainda não está
completo, então a ISO leva também o `cpp` do GCC (só o pré-processador).

Para compilar com o cproc, o NovaEra respeita os limites dele:

- **sem assembly embutido** (`__asm__`): as instruções especiais ficam
  em `cpu.asm` e `usuario/crt0.asm`;
- **sem `volatile`**: a memória de vídeo é acessada por
  `mem_escreve16()` e `mem_le16()`, também em assembly;
- **sem structs `packed`**: a TSS é um vetor de `u32`.

## O comando `nefs`

| Comando | O que faz |
|---|---|
| `nefs novo <pasta> [nome]` | cria um sistema novo a partir do NovaEra |
| `nefs comando <nome>` | cria um comando do shell em `usuario/bin/` (vai para o initrd) |
| `nefs programa <nome>` | cria um servidor que sobe no boot e o registra em `programas.lista` |
| `nefs compilar` | compila o sistema (cproc, ou clang fora da ISO) |
| `nefs rodar` | liga o sistema no QEMU (na ISO, a tela aparece no próprio terminal) |
| `nefs iso` | gera a ISO inicializável do sistema, com GRUB |
| `nefs info` | mostra nome, versão e programas do sistema |
| `nefs livro [n]` | lê o Livro do NEFS |
| `nefs ferramentas` | mostra o compilador e as ferramentas encontradas |

O `nefs` também funciona fora da ISO, em qualquer Linux ou no Termux:
sem o cproc instalado, ele usa o clang.

```sh
nefs/nefs novo meu-so "Sistema da Maria"
cd meu-so
../nefs/nefs compilar
../nefs/nefs rodar
```

## O Livro do NEFS

A pasta [`livro/`](livro/) tem o Livro 1, em português:

1. [O que é o NEFS](livro/01-o-que-e-o-nefs.md)
2. [Primeiros passos](livro/02-primeiros-passos.md)
3. [Como o NovaEra funciona](livro/03-como-o-novaera-funciona.md)
4. [O seu primeiro programa](livro/04-seu-primeiro-programa.md)
5. [A identidade do seu sistema](livro/05-a-identidade-do-seu-sistema.md)

## Construindo a ISO do NEFS

Num Debian ou Ubuntu x86_64, com internet:

```sh
sudo bash nefs/iso/construir.sh
qemu-system-x86_64 -cdrom nefs/iso/nefs-0.1-x86_64.iso -m 1G
```

O script baixa e compila o QBE (de `c9x.me`) e o cproc (de `git.sr.ht`),
junta o kernel Linux do Ubuntu, o BusyBox, binutils, nasm, QEMU e GRUB,
e gera a ISO. Os fontes ficam guardados em `nefs/iso/fontes/`.

| Arquivo | O que é |
|---|---|
| `iso/construir.sh` | monta a ISO |
| `iso/raiz/init` | o primeiro programa que o Linux da ISO executa |
| `iso/raiz/etc/` | `inittab` (terminais na tela e na serial), `profile` e a mensagem de boas-vindas |

## Estado atual

- [x] Comando `nefs`: novo, programa, compilar, rodar, iso, info, livro
- [x] NovaEra configurável (`sistema.conf`, `programas.lista`) e sem `__asm__` no C
- [x] Livro 1 (5 capítulos)
- [x] ISO: boot, terminal, `nefs`, livro, QEMU dentro da ISO e geração de ISOs com GRUB, testados
- [x] **cproc + QBE dentro da ISO**: `nefs compilar` e `nefs iso`
      testados na própria ISO; o NovaEra compilado pelo cproc roda no QEMU
- [ ] Disco persistente para guardar os projetos
- [ ] Site do NEFS
- [ ] Livro 2: sistema de arquivos, memória compartilhada, drivers de disco
