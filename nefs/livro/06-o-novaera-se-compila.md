# 6. O NovaEra se compila sozinho

Um sistema é **autossuficiente** quando ele compila a si mesmo: o
compilador, a libc, o núcleo, tudo, sem ajuda de outro sistema. O Linux
chegou lá em 1992. O NovaEra também chegou.

## Dentro do NovaEra

Todo NovaEra traz o próprio código em `/usr/src/novaera`, e o do
compilador em `/usr/src/qbe-1.3` e `/usr/src/cproc-d1c53dd`. No shell:

```
cd /usr/src/novaera
nefs compilar
```

Em menos de um minuto (no QEMU de um computador; no celular, um pouco
mais) aparecem:

| Arquivo | O que é |
|---|---|
| `build/kernel.bin` | o núcleo, com os servidores do boot |
| `build/initrd.tar` | os programas, a libc, os cabeçalhos e o código-fonte |

O `nefs` daqui não é o mesmo da ISO: ele é um programa em C
(`ferramentas/nefs.c`) que faz o que o `build.sh` faz no Linux, na mesma
ordem:

1. lê `sistema.conf` e `programas.lista` e gera `build/sistema.h`,
   `build/programas.h` e `build/programas.s`
2. compila os servidores do boot (`usuario/*.c`)
3. compila a libc (`libc.a`), os comandos (`usuario/bin`) e as ferramentas
4. compila os portes: o QBE e o cproc
5. monta o núcleo (`kernel.bin`) e o initrd (`initrd.tar`)

Para ver cada comando, use `nefs compilar -v`. Para ir mais rápido, sem
o QBE e o cproc, `nefs compilar -sem-portes` (mas aí o sistema gerado não
tem compilador).

## O seu sistema, criado lá dentro

```
nefs novo /tmp/meu Meu Sistema
nefs compilar /tmp/meu
```

O `nefs novo` copia `/usr/src/novaera` para a pasta e escreve um
`sistema.conf` com o nome novo. Mude o que quiser (com o `vi`, quando o
NovaEra tiver um editor; por enquanto, com `echo ... > arquivo`) e compile.

## Como saber que deu certo

A prova da autossuficiência tem três gerações:

1. **Geração 1**: o NovaEra compilado no Linux.
2. **Geração 2**: o NovaEra que a geração 1 compilou, dentro dela mesma.
3. **Geração 3**: o NovaEra que a geração 2 compilou.

A geração 3 é **idêntica, byte a byte**, à geração 2: o `kernel.bin` e o
`initrd.tar` inteiros. Isso quer dizer que o compilador compilado pelo
NovaEra gera exatamente o mesmo código que o compilador que o compilou: o
sistema chegou a um "ponto fixo". E a geração 2 também é idêntica à
compilada no Linux com o mesmo compilador (`CC=cproc ./build.sh`).

Para tirar os arquivos de dentro do NovaEra no teste, usamos a porta
serial: `base64 build/kernel.bin` escreve o arquivo como texto, e o QEMU
(`-serial file:saida.txt`) guarda esse texto do lado de fora.

## O que ainda falta

Os arquivos de `/tmp` e de `build/` ficam na memória: somem quando o
sistema desliga. Para o NovaEra guardar o que compilou e dar boot no
sistema novo sozinho, falta um **disco** — a Etapa 8 da Fase 3.

Este é o fim do Livro 1. Os próximos volumes vão tratar de disco, sistema
de arquivos, memória compartilhada, gráficos e de como montar uma área de
trabalho (veja `nefs/ROTEIRO.md`).
