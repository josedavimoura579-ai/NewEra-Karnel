# 1. Antes de começar

Este é o **Livro 2**: instalar o NovaEra **à mão**, passo a passo, como no
guia de instalação do Arch Linux. Nenhum programa faz tudo por você: cada
comando é uma peça, e no fim você sabe exatamente o que tem no seu disco.

## O que você vai fazer

1. ligar o computador pela **ISO** do NovaEra (o sistema roda da memória);
2. **dividir** o disco em partições;
3. **formatar** a partição do sistema (ext2);
4. **copiar** o sistema para ela;
5. **entrar** no sistema novo e configurá-lo (nome, senhas, usuários);
6. instalar o **carregador de boot** (o GRUB);
7. **reiniciar** pelo disco.

## Do que você precisa

- A ISO do sistema, `novaera-<versão>-x86_64.iso` (o lançamento
  **NovaEra**, na página de lançamentos do GitHub), ou o NovaEra compilado
  (`bash run.sh`).
- Um disco para instalar. **Tudo o que estiver nele será apagado.**

No QEMU (Linux, Termux, Windows), o disco é um arquivo:

```
truncate -s 256M disco.img
qemu-system-x86_64 -cdrom novaera-4.2-x86_64.iso -boot d -m 256M \
    -drive file=disco.img,format=raw,index=0,media=disk
```

O `-boot d` manda ligar pelo CD (a ISO). Sem ele, depois que o disco tiver
o GRUB, o QEMU liga pelo disco.

A ISO liga no **modo gráfico** (1024 × 768, com o mouse). No Termux, a
tela gráfica vai por VNC: acrescente `-vnc :1` e conecte um app de VNC em
`localhost:5901`. Para ficar no próprio terminal, em texto, troque por
`-display curses -vga cirrus` (a placa cirrus não tem o modo gráfico, e o
NovaEra liga em texto sozinho). Para sair do QEMU no curses: Esc e depois
2, digite `quit`.

Os comandos deste guia são os mesmos nos dois modos.

## Convenções

Neste guia, o disco é o **`sda`** (o primeiro). Se o seu for outro, troque
o nome em todos os comandos. Para ver os discos: `discos`.

Leia um capítulo com `nefs guia <número>`, dentro do próprio NovaEra.
