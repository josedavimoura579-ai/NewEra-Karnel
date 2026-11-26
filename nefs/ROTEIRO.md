# Roteiro do NEFS — do terminal à área de trabalho

O NEFS existe para que qualquer pessoa crie o próprio sistema operacional
sobre o núcleo NovaEra. O destino final são **dois modos de criar um
sistema**:

| Modo | Como é | Inspiração |
|---|---|---|
| **Manual** | a pessoa monta o sistema peça por peça: escolhe os servidores, compila cada programa, configura e gera a ISO, seguindo o livro | Arch Linux, Linux From Scratch |
| **Pronto** | um instalador faz perguntas (nome, logo, usuário, "com ou sem área de trabalho") e entrega um sistema completo | Debian, Ubuntu |

O caminho é em degraus. Cada fase entrega algo que já dá para usar, em vez
de esperar anos por tudo de uma vez.

## Fase 3 — o NEFS rodando 100% sobre o NovaEra (atual)

Plano detalhado em [`FASE3.md`](FASE3.md): QBE, assembler e ligador
próprios, cproc, autossuficiência e disco. **Entrega:** a ISO do NEFS sem
nenhum Linux por baixo.

## Fase 4 — base POSIX e o modo manual

O que quase todo programa de Unix espera do sistema:

- sinais e `kill` (hoje um programa travado num pipe trava quem lê dele)
- threads e mutex
- `mmap` e memória compartilhada
- sockets locais (`AF_UNIX`), a base do X11, do Wayland e do D-Bus
- usuários, grupos, donos e permissões de arquivo; `login` e `sudo`
- o **modo manual** completo: o livro ensina a montar o sistema do zero,
  partição por partição, como no guia de instalação do Arch

**Entrega:** um sistema multiusuário em modo texto que a pessoa monta à mão.

## Fase 5 — gráficos

- framebuffer em alta resolução (VESA/VBE pelo GRUB)
- driver de mouse PS/2 (e depois USB)
- fontes TrueType (um rasterizador pequeno)
- **servidor de janelas** no anel 3: cada programa desenha na própria área
  de memória compartilhada e o servidor compõe a tela

**Entrega:** janelas, mouse e texto bonito na tela.

## Fase 6 — a área de trabalho do NovaEra e o modo pronto

Uma área de trabalho própria, leve, no estilo do XFCE:

- painel com menu, relógio e lista de janelas
- gerenciador de arquivos, terminal gráfico, editor de texto
- **pacotes**: um formato simples e um gerenciador (o "apt" do NovaEra)
- **instalador estilo Debian/Ubuntu**: escolhe disco, usuário, nome e logo
  do sistema, e "com área de trabalho" ou "só terminal"

**Entrega:** o modo pronto; um sistema com área de trabalho em poucos minutos.

## Fase 7 — rede e o XFCE4 de verdade

O XFCE4 em si é pequeno, mas depende de uma pilha grande: X11 (ou
Wayland), GTK3, GLib, cairo, pango, freetype, fontconfig, harfbuzz e
D-Bus. Isso exige o POSIX quase completo e um compilador maior que o
cproc + QBE.

- rede: driver de placa (virtio-net, e1000) e TCP/IP como servidores
- bibliotecas dinâmicas (`.so`)
- porte do GCC ou do clang
- porte do X11, do GTK3 e do **XFCE4**

**Entrega:** o instalador ganha a opção "área de trabalho XFCE4".
