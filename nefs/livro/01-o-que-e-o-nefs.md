# 1. O que é o NEFS

**NEFS** quer dizer **NovaEra From Scratch**: "NovaEra do zero".

A ideia é parecida com a do Linux From Scratch. Em vez de baixar um
sistema pronto, você **constrói o seu**, peça por peça, e entende cada
uma delas. A diferença é o ponto de partida: no NEFS, a base é o
**NovaEra**, um microkernel pequeno o bastante para ser lido por inteiro
por uma única pessoa.

## O que vem na ISO do NEFS

A ISO liga direto num terminal com tudo o que você precisa:

| Ferramenta | Para que serve |
|---|---|
| `nefs` | o comando principal: cria, compila, roda e empacota o seu sistema |
| `cproc` | o compilador de C |
| `qbe` | o "backend": transforma o código intermediário do cproc em assembly x86_64 |
| `as` e `ld` | o montador e o ligador (binutils) |
| `nasm` | o montador das partes do NovaEra escritas em assembly |
| `qemu-system-x86_64` | uma máquina virtual, para testar o seu sistema |
| `grub-mkrescue` | gera uma ISO inicializável do seu sistema |
| `vi`, `less`... | o BusyBox: editor e comandos básicos |

A cadeia de compilação é esta:

```
arquivo .c → cproc → QBE → as → ld → programa do NovaEra
```

Por que o cproc e o QBE, e não o GCC ou o LLVM? Porque o NEFS segue a
mesma filosofia do NovaEra: **você deve conseguir entender as suas
ferramentas**. O QBE tem cerca de 10 mil linhas de C, e o cproc é do
mesmo tamanho. O LLVM tem milhões.

## O sistema da ISO não é o seu sistema

A ISO do NEFS roda sobre um **Linux mínimo**. Ele é só a sua oficina,
como o "sistema anfitrião" do Linux From Scratch. O sistema que você
cria é outra coisa: um **sistema operacional novo**, com o NovaEra
como núcleo, que você testa no QEMU e grava numa ISO própria.

```
┌─────────────────── ISO do NEFS (a oficina) ───────────────────┐
│  Linux mínimo + BusyBox + cproc + QBE + nefs + QEMU + GRUB    │
│                                                               │
│     nefs novo meu-so  ──►  meu-so/  ──►  meu-so.iso           │
│                            (o SEU sistema, com o NovaEra)     │
└───────────────────────────────────────────────────────────────┘
```

## Os 5 degraus

1. **Ler** o código do NovaEra (este livro te guia).
2. **Rodar** o sistema numa máquina virtual.
3. **Modificar**: nome, cores, comandos do shell.
4. **Estender**: escrever programas e drivers novos.
5. **Distribuir**: gerar a ISO do seu sistema e publicar.

Próximo capítulo: `nefs livro 2`.
