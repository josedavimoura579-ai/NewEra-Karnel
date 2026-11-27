# A fonte do terminal gráfico

`usuario/fonte8x16.h` é a fonte de 8 × 16 pontos que o servidor de vídeo
usa para desenhar o terminal na tela gráfica. Ela é gerada por `gerar.py`
a partir da `Lat15-Fixed16.psf.gz` do pacote `console-setup` do Debian,
que vem das fontes **"Fixed" do X11**, de **domínio público**. Tem as
letras acentuadas do português (e do resto da Europa ocidental) e os
desenhos de molduras.

Para gerar de novo (no Linux, com o `console-setup-linux` instalado):

    python3 ferramentas/fonte/gerar.py /usr/share/consolefonts/Lat15-Fixed16.psf.gz
