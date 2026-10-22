# Portes: programas de fora rodando no NovaEra

Um **porte** é um programa de outro projeto, compilado do código original
**sem nenhuma mudança** para rodar no NovaEra, com a libc do NovaEra
(`libc/`). Se um porte precisa de algo que falta, quem muda é o NovaEra
(a libc ou o núcleo), não o programa.

| Porte | Versão | O que é | Vai para |
|---|---|---|---|
| `qbe` | 1.3 | o *backend* do compilador do NEFS: lê SSA, escreve assembly x86_64 | `/bin/qbe`, exemplos em `/usr/share/qbe` |
| `cproc` | `d1c53dd` (git) | o compilador de C: o driver `cproc` e o compilador `cproc-qbe` | `/bin/cproc`, `/bin/cproc-qbe` |

## Como funciona

Cada porte tem uma pasta com:

- `receita`: nome, versão, endereço, SHA-256 e a lista de arquivos `.c`
- arquivos que o NovaEra acrescenta (o `config.h` do QBE, que o Makefile
  original gera com `uname`)
- `exemplos/`, opcional: vão para o initrd

```
portes/baixar.sh     # baixa as fontes para portes/fontes/ e confere o SHA-256
./build.sh           # compila cada porte que já tem fontes e o põe no initrd
```

Sem as fontes, o `build.sh` avisa e constrói o sistema sem o porte.

## Experimente o QBE dentro do NovaEra

```
qbe /usr/share/qbe/ola.ssa
qbe -o /tmp/soma.s /usr/share/qbe/soma.ssa
cat /tmp/soma.s
```

O assembly gerado é idêntico, byte a byte, ao do QBE no Linux.

## Compile C dentro do NovaEra

```
cproc -o /tmp/ola /usr/share/nefs/exemplos/ola.c
/tmp/ola Maria
```

O `cproc` chama, ligados por pipes, o `nefs-cpp` (pré-processador),
o `cproc-qbe` (C → SSA), o `qbe` (SSA → assembly) e o `nefs-as`
(assembly → objeto); depois o `nefs-ld` liga com a libc. `cproc -v` mostra
cada passo. Veja `portes/cproc/config.h`.

## Receitas

Uma receita diz de onde vêm as fontes e quais programas saem delas:

```
NOME=cproc
VERSAO=d1c53dd
GIT=https://git.sr.ht/~mcf/cproc           # do git, num commit fixo...
COMMIT=d1c53ddf56571573a7025324c8dd5c6d547a4d1f
PASTA=cproc-d1c53dd
PROGRAMAS="cproc cproc-qbe"                # ...ou URL= e SHA256=
FONTES_cproc="driver.c util.c"             # os .c de cada programa
FONTES_cproc_qbe="..."                     # (- vira _ no nome)
```
