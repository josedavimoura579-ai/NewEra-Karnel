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

- [x] **Etapa 1 — a tela gráfica** (núcleo 4.1; [livro, capítulo 16](livro/16-a-tela-grafica.md))
  - núcleo: mapear memória de dispositivo (só drivers) e o PCI
  - o driver da BGA (e o framebuffer do GRUB); 1024 × 768 × 32
  - o terminal desenhado com uma fonte bitmap 8×16 (128 × 48 letras): o
    mesmo shell de sempre, agora em modo gráfico
  - a **logo** (o sol nascendo) na tela de boot
  - `run.sh vnc` e `run.sh grafico`
  - *Teste: o NovaEra liga no QEMU com `-vga std`, mostra a logo e o shell
    funciona na tela gráfica.*
  - Feito: `SYS_MAPEAR_FISICO` (só drivers; só memória de dispositivo,
    nunca a RAM; marcada para não ser liberada como se fosse RAM); `inw`,
    `outl` e `inl` em `sys.s`. O servidor de vídeo liga a BGA (achada pelo
    PCI) com `grafico` na linha de boot: a faixa com a logo desenhada ponto
    a ponto (16 amostras por ponto), o terminal em 128 × 43 letras com a
    fonte 8×16 "Fixed" (domínio público, `ferramentas/fonte/gerar.py`) e
    os acentos pelo Unicode; as mensagens do boot (em CP437) redesenhadas.
    `run.sh grafico` e `run.sh vnc`; o menu do GRUB (ISO e `instalar-boot`)
    tem a opção "modo gráfico"; o modo texto continua o padrão. Ainda não:
    o framebuffer do GRUB (VBE) para PCs sem a BGA.
- [x] **Etapa 2 — o mouse** ([livro, capítulo 17](livro/17-o-mouse.md))
  - o driver do mouse PS/2 (a IRQ 12): movimento, botões, roda
  - a seta na tela; quem quiser recebe os eventos
  - *Teste: a seta acompanha o mouse; um programa mostra os cliques.*
  - Feito: o driver do teclado é também o do mouse PS/2 (o bit 5 do estado
    separa os bytes; IRQ 12; a roda pela "batida" 200/100/80); manda os
    movimentos ao vídeo (`MSG_MOUSE`). O vídeo desenha a seta (guarda o
    que havia embaixo; esconde para escrever texto) e entrega os eventos a
    quem pergunta (`MOUSE_ESPERAR`, com a resposta adiada e uma fila por
    programa). Comando `mouse`. Conferido no QEMU pelo monitor
    (`mouse_move`, `mouse_button`).
  - E o **vmmouse** (a "porta dos fundos" do VMware, `0x5658`): numa
    máquina virtual, a posição exata (0-65535), e a seta fica embaixo do
    dedo no VNC; sem dados nele, volta ao PS/2 relativo. Conferido com um
    cliente VNC de teste: o dedo em sete pontos (cantos e meio) e a seta
    exatamente lá.
- [x] **Etapa 3 — desenhar: a biblioteca gráfica** ([livro, capítulo 18](livro/18-desenhar.md))
  - pontos, linhas, retângulos, preencher, copiar áreas, transparência
  - imagens: o porte do **stb_image** (PNG, JPEG, BMP)
  - um visualizador de imagens (`ver foto.png`)
  - *Teste: o visualizador mostra um PNG; os desenhos batem com uma imagem
    de referência feita no Linux.*
  - Feito: a `grafico.h` na libc (pontos, linhas, retângulos, círculos,
    degradê, copiar, escalar, texto com a fonte 8×16; cores `0xAARRGGBB`
    com transparência). A **tela emprestada**: o programa cria uma memória
    compartilhada do tamanho da tela e o vídeo passa a mostrá-la
    (`VIDEO_TELA_ABRIR`/`MOSTRAR`/`FECHAR`); o terminal continua numa grade
    e é redesenhado na volta, e o vídeo pega a tela de volta se o dono
    morrer. A memória compartilhada passou de 2 para 8 MiB por objeto. O
    **stb_image** 2.30 do jeito que veio (`libc/src/stb/`), compilado
    pelo cproc no próprio NovaEra. O `ver` (clique ou Enter para voltar) e
    três fotos em `/usr/share/nefs/exemplos/imagens/`. O `testegrafico`
    desenha uma cena e calcula um hash dos pontos: os números no NovaEra e
    no Linux são iguais (a cena e as três fotos; o PNG e o BMP batem também
    com o Python/PIL).
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
