# Fase 5 — gráficos

**Objetivo:** sair da tela de texto. Uma tela em alta resolução, o mouse,
letras bonitas (com contorno suave) e **janelas**: cada programa desenha a
sua, e um servidor de janelas monta a tela. A área de trabalho completa
(painel, menu, gerenciador de arquivos, instalador gráfico) é a Fase 6.

## Decisões

- **A placa de vídeo:** a **BGA** (o adaptador gráfico do Bochs), que o
  QEMU (`-vga std`), o VirtualBox e o Bochs oferecem: o driver escolhe a
  resolução sozinho, ligando pelo GRUB ou pelo `-kernel`. Num PC de
  verdade, o **framebuffer que o GRUB prepara** (VBE). Sem nenhum dos dois,
  o NovaEra continua em modo texto, como hoje.
- **Resolução padrão:** 1024 × 768, 32 bits por ponto (milhões de cores).
- **O núcleo continua sem saber o que é um gráfico.** Ele só ganha duas
  coisas para os drivers: mapear a memória de um dispositivo (a tela) e
  conversar com o barramento PCI (para achar a placa). Desenhar é trabalho
  dos servidores, no anel 3.
- **As janelas: um protocolo próprio, no estilo do Wayland.** Cada
  programa desenha a sua janela sozinho, numa **memória compartilhada**
  (Fase 4, Etapa 7); o servidor de janelas só **compõe** a tela e conversa
  com os programas por **sockets** (Fase 4, Etapa 8). O X11 fica para a
  Fase 7.
- **As letras:** o porte do **stb_truetype** (domínio público, um arquivo
  C) e uma fonte livre (a **DejaVu Sans**). Até ele chegar, uma fonte
  bitmap 8×16 desenha o terminal.
- **Ver a tela no Termux:** o QEMU do Termux (`-headless`) desenha em
  texto (`-display curses`), que não mostra gráficos. Para a Fase 5, a
  tela vai por **VNC**: `bash run.sh vnc` e um app de VNC no celular
  (bVNC, RealVNC...). No PC, a janela normal do QEMU.

## Etapas

Cada etapa termina com algo testável no QEMU (e uma foto da tela).

- [ ] **Etapa 1 — a tela gráfica**
  - núcleo: mapear memória de dispositivo (só drivers) e o PCI
  - o driver da BGA (e o framebuffer do GRUB); 1024 × 768 × 32
  - o terminal desenhado com uma fonte bitmap 8×16 (128 × 48 letras): o
    mesmo shell de sempre, agora em modo gráfico
  - a **logo** (o sol nascendo) na tela de boot
  - `run.sh vnc` e `run.sh grafico`
  - *Teste: o NovaEra liga no QEMU com `-vga std`, mostra a logo e o shell
    funciona na tela gráfica.*
- [ ] **Etapa 2 — o mouse**
  - o driver do mouse PS/2 (a IRQ 12): movimento, botões, roda
  - a seta na tela; quem quiser recebe os eventos
  - *Teste: a seta acompanha o mouse; um programa mostra os cliques.*
- [ ] **Etapa 3 — desenhar: a biblioteca gráfica**
  - pontos, linhas, retângulos, preencher, copiar áreas, transparência
  - imagens: o porte do **stb_image** (PNG, JPEG, BMP)
  - um visualizador de imagens (`ver foto.png`)
  - *Teste: o visualizador mostra um PNG; os desenhos batem com uma imagem
    de referência feita no Linux.*
- [ ] **Etapa 4 — letras bonitas**
  - o porte do **stb_truetype** e a fonte **DejaVu Sans**
  - texto com contorno suave (anti-aliasing), em qualquer tamanho, com
    acentos (UTF-8)
  - *Teste: um texto com "ação, coração, Você" em três tamanhos.*
- [ ] **Etapa 5 — o servidor de janelas**
  - um servidor novo no anel 3: os programas se conectam por um socket e
    desenham na sua memória compartilhada; o servidor compõe a tela
  - barra de título, fechar, arrastar, trazer para a frente
  - o teclado vai para a janela da frente; o mouse, para a janela embaixo
    da seta
  - *Teste: três janelas de programas diferentes, arrastadas e
    sobrepostas; fechar uma não derruba as outras.*
- [ ] **Etapa 6 — a biblioteca das janelas**
  - para os programas: criar uma janela, desenhar nela, receber eventos
    (tecla, clique, redimensionar, fechar)
  - botões, caixas de texto e rótulos simples
  - exemplos: "olá, janela", um relógio, uma calculadora
  - *Teste: os exemplos compilados com o cproc dentro do NovaEra rodam
    em janelas.*
- [ ] **Etapa 7 — o terminal gráfico**
  - terminais virtuais (os "pty"): o servidor de terminal atende vários
    terminais ao mesmo tempo
  - um programa de terminal numa janela, com o shell dentro; vários
    abertos
  - *Teste: dois terminais lado a lado, um compilando e o outro com o
    `ps`; o Ctrl+C só vale para o da frente.*
- [ ] **Etapa 8 — o modo gráfico no boot**
  - o `init` liga o servidor de janelas e uma **tela de login gráfica**
    (com a logo); o modo texto continua sendo uma opção (no GRUB e no
    `/etc`)
  - um papel de parede e um lugar reservado para o mascote
  - *Teste: o sistema instalado liga direto no login gráfico; entrar abre
    um terminal gráfico.*
- [ ] **Etapa 9 — o livro e o lançamento**
  - o Livro 1 ganha os capítulos da parte gráfica; o guia de instalação,
    o modo gráfico
  - o lançamento **NEFS 0.3**: a ISO que liga em modo gráfico
  - *Entrega: janelas, mouse e texto bonito na tela.*
