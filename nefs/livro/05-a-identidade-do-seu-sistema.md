# 5. A identidade do seu sistema

## O sistema.conf

```
NOME="Sistema da Maria"
VERSAO="0.1"
LEMA="feito com o NEFS sobre o NovaEra"
PROMPT="Maria"
```

O nome, a versão e o lema aparecem na moldura do boot, e o prompt
aparece no shell (`Maria>`). Acentos funcionam: a tela VGA usa a
tabela CP437, e o `build.sh` converte o que for possível.

Mude, compile e rode de novo:

```
vi sistema.conf
nefs compilar && nefs rodar
```

## Mudando o shell

O shell é um programa comum: `usuario/shell.c`. Para criar um comando,
encontre a função `executar()` e acrescente uma linha antes do último
`else`:

```c
else if (str_igual(cmd, "ola")) escreve("Ola!\n", COR(COR_VERDE_CLARO));
```

Repare que você mudou o sistema **sem tocar no núcleo**. É assim que um
microkernel cresce.

## Mudando as cores

As cores estão em `usuario/ulib.h` (`COR_*`), e o fundo azul vem da
macro `COR()`. No núcleo, o fundo do boot é a constante `FUNDO`, em
`kernel.c`.

## Distribuindo

```
nefs iso
```

O arquivo `<pasta>.iso` é o seu sistema operacional: liga em qualquer PC
x86_64 ou máquina virtual, pelo GRUB. Ele é seu. O NovaEra usa a
licença MIT, então você pode publicar o seu sistema com o seu nome e a
sua própria licença. Basta manter o aviso de copyright do NovaEra no
arquivo `LICENSE`.

## Estilo Raiz ou Estilo Estável?

O NEFS propõe dois jeitos de cuidar de um sistema:

- **Raiz** (à la Arch): o mínimo, sempre atualizado com o NovaEra mais
  novo; você escolhe cada peça.
- **Estável** (à la Debian): um conjunto testado de programas,
  congelado por versão, com lançamentos espaçados.

Os dois começam igual: `nefs novo`.

---

Este é o fim do Livro 1. Os próximos volumes vão tratar de memória
compartilhada, sistema de arquivos, drivers de disco e de como o
próprio compilador do NEFS poderá, um dia, rodar dentro do NovaEra.
