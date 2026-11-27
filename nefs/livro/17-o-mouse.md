# 17. O mouse

Na tela gráfica (capítulo 16), aparece a **seta** do mouse. Mexa o mouse
e ela anda; clique e gire a roda, e os programas ficam sabendo.

## Na prática

Ligue em modo gráfico (`bash run.sh grafico`, ou `bash run.sh vnc` no
celular) e rode:

```
mouse
```

```
Mexa o mouse, clique, gire a roda. Ctrl+C para sair.
A seta está em (662, 464).
botão esquerdo apertado em (662, 464)
botão esquerdo solto em (662, 464)
roda: +1 (para cima) em (622, 584)
```

A posição é em pontos da tela: (0, 0) é o canto de cima à esquerda; a tela
vai até (1023, 767).

## Como o mouse fala com o computador

O mouse PS/2 (o da bolinha, e hoje os USB "fingindo" ser PS/2) usa o mesmo
controlador do teclado. Cada vez que ele se mexe, manda um **pacote** de 3
bytes (4, se tiver roda):

| Byte | O que diz |
|---|---|
| 1º | quais botões estão apertados, e se o movimento é para a esquerda ou para baixo |
| 2º | quanto andou na horizontal (−256 a 255) |
| 3º | quanto andou na vertical |
| 4º | quanto a roda girou |

O mouse não sabe onde a seta está: ele só diz "andei 5 para a direita e 3
para cima". Quem soma os movimentos e decide onde fica a seta é o
servidor de vídeo.

A roda tem uma curiosidade: o mouse só a liga depois de uma "batida
secreta", três pedidos seguidos de taxas diferentes (200, 100 e 80). Um
mouse com roda responde "sou o modelo 3"; um sem roda continua "modelo 0".

## Como funciona no NovaEra

1. O **driver do teclado** (PID 2) também é o driver do mouse: os dois
   chegam pela mesma porta (`0x60`), e um bit do estado do controlador diz
   de quem é cada byte. O mouse usa a **IRQ 12**.
2. Ele junta os bytes em pacotes e manda o movimento ao **servidor de
   vídeo** (`MSG_MOUSE`).
3. O servidor de vídeo move a **seta**. Antes de desenhá-la, guarda os
   pontos que estavam embaixo; para movê-la (ou para escrever texto ali),
   devolve esses pontos e desenha de novo no lugar novo.
4. Um programa que quer os eventos pergunta (`MOUSE_ESPERAR`) e só recebe a
   resposta quando o mouse fizer alguma coisa. O servidor de vídeo nunca
   fica parado esperando um programa: a resposta é que fica guardada até
   ter um evento.

Na Etapa 5, quem recebe os eventos passa a ser o servidor de janelas, que
manda cada clique para a janela que estiver embaixo da seta.
