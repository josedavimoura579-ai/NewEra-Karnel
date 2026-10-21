# 5. A identidade do seu sistema

## O sistema.conf

```
NOME="Sistema da Maria"
VERSAO="0.1"
LEMA="feito com o NEFS sobre o NovaEra"
PROMPT="Maria"
PURO="nao"
```

O nome, a versão e o lema aparecem na moldura do boot, e o prompt
aparece no shell (`Maria:/>`). Acentos funcionam: a tela VGA usa a
tabela CP437, e o NovaEra converte o que for possível.

## O seu sistema é uma distribuição

É como no Linux: o kernel Linux não sabe se ele é um Ubuntu ou um
Debian; quem diz isso é o arquivo `/etc/os-release` da distribuição. No
NovaEra, o `sistema.conf` faz esse papel. O núcleo tem a versão dele
(`NOVAERA_VERSAO`, em `novaera.h`), e o seu sistema tem a dele.

- O `sistema.conf` do NovaEra puro diz `PURO="sim"`: não há distribuição.
- O de um sistema criado com `nefs novo` diz `PURO="nao"`.

O `build.sh` copia essa identidade para o `/etc/sistema` do seu sistema,
e o logo (o arquivo `logo.txt`) para `/etc/logo.txt`. Desenhe o seu logo
em `logo.txt`, compile e digite `novafetch` no shell:

```
  +-----------------+     Sistema da Maria
  |   seu logo      |     Sistema:   Sistema da Maria 0.1
  |   aqui :)       |     Núcleo:    NovaEra 3.1 x86_64
  +-----------------+     ...
```

O `novafetch` mostra as duas coisas, cada uma no seu lugar: o seu
sistema e o núcleo por baixo dele.

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
