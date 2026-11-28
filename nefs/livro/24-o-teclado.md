# 24. O teclado brasileiro

Até aqui, o NovaEra só conhecia o teclado americano: nada de ç, nada de
acentos, e as setas não faziam nada. Agora ele tem o **ABNT2** (o teclado
brasileiro) como padrão, os acentos, as setas e as outras teclas
especiais, e a linha do terminal se edita como no Linux.

## Na prática

```
echo ação, você, à, pôr
```

- **O ç** é a tecla ao lado do L.
- **Os acentos** são "teclas mortas": sozinhas, não escrevem nada; esperam a
  próxima tecla. ´ e depois a = á; ~ e depois a = ã; Shift+~ (o ^) e
  depois e = ê; Shift+´ (o `) e depois a = à; Shift+6 (o ¨) e depois u =
  ü. A tecla morta e depois o espaço escreve o próprio acento.
- **A seta para cima** traz o comando anterior (e a de baixo volta). As
  setas para os lados andam na linha; Home e End vão ao começo e ao fim;
  Delete apaga a letra embaixo do cursor.
- **O teclado americano:** `layout us` (e `layout br` volta). Para valer
  em todo boot, o root troca o `/etc/teclado`.

No Termux, o `run.sh` já liga o QEMU com o mapa brasileiro (`-k pt-br`).

## O que uma tecla é, para o computador

O teclado não sabe que letra está desenhada em cada tecla. Ele só manda o
**número da tecla** (o "scancode"): a tecla ao lado do L é a 0x27, em
qualquer teclado do mundo. Quem decide que a 0x27 é o **ç** (no ABNT2) ou o
**;** (no americano) é o **layout**, uma tabela no driver do teclado.

```
tecla 0x27  ->  layout ABNT2  ->  ç  ->  UTF-8: 0xC3 0xA7  ->  terminal
```

O driver tem três tabelas por layout: a tecla sozinha, com Shift e com
AltGr (no ABNT2, AltGr+Q = /, AltGr+E = °).

## As teclas mortas

Quando chega uma tecla morta, o driver só **guarda** o acento. Na tecla
seguinte, ele procura numa tabela: ´ com a vira á, ~ com o vira õ. Se não
combinar (´ com k), saem os dois: ´k. Tudo isso acontece no driver; o
terminal e os programas já recebem a letra pronta, em UTF-8.

## As setas viram texto

Uma seta não é uma letra. Para ela caber no mesmo caminho das letras, o
driver a escreve como uma pequena sequência que começa com o Esc (o byte
27), a mesma dos terminais do Linux:

| Tecla | Sequência |
|---|---|
| ↑ ↓ → ← | Esc [ A, Esc [ B, Esc [ C, Esc [ D |
| Home, End | Esc [ H, Esc [ F |
| Delete | Esc [ 3 ~ |
| Page Up, Page Down | Esc [ 5 ~, Esc [ 6 ~ |
| F1 a F4 | Esc O P, Esc O Q, Esc O R, Esc O S |
| F5 a F12 | Esc [ 15 ~ ... Esc [ 24 ~ |
| Alt+x | Esc x |

Cada tecla vai numa mensagem só ao terminal: assim, quem lê recebe a
sequência inteira de uma vez e não confunde a seta com um Esc sozinho.

## A linha que se edita

Quem edita a linha é o **servidor de terminal** (não o shell): por isso,
qualquer programa que lê uma linha (o shell, o `login`, um programa seu
com `fgets`) ganha as setas e o histórico de graça. O Windows faz assim
no console; o Linux deixa isso para cada programa (a biblioteca
"readline").

Para mover o cursor na tela sem apagar o que está escrito, o terminal
manda ao vídeo outras sequências do Linux: Esc [ n D (o cursor n letras
para a esquerda), Esc [ n C (para a direita) e Esc [ K (apaga até o fim
da linha). Inserir uma letra no meio é: escrever a letra e o resto da
linha, e voltar o cursor. O vídeo e o `term` (o terminal das janelas)
entendem as três.

O histórico guarda as últimas 32 linhas de cada terminal. As senhas não
entram: com o eco desligado, nada é lembrado.

## Nas janelas

No modo gráfico, o `janelas` lê as teclas no modo cru e as traduz para os
programas: uma letra chega como o seu código Unicode (`'a'`, `0xE7` para o
ç) e uma tecla especial como um código próprio (`JN_CIMA`, `JN_F(5)`...),
com o Alt em `botoes`. O `term` faz o caminho de volta: transforma o código
na sequência de novo e manda ao seu terminal virtual.
