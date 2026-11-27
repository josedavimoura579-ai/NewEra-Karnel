# 18. Desenhar

Até aqui, só o servidor de vídeo desenhava na tela: as letras do terminal e
a seta do mouse. Agora **qualquer programa** pode desenhar: pontos, linhas,
retângulos, círculos, degradês, texto e **fotos** (PNG, JPEG e BMP).

## Na prática

Ligue em modo gráfico (`bash run.sh grafico`, ou `bash run.sh vnc` no
celular) e rode:

```
ver /usr/share/nefs/exemplos/imagens/logo.png
```

A tela inteira vira a foto: a imagem no meio, com uma sombra, e embaixo uma
barra com o nome e o tamanho. Clique (ou aperte Enter) e o terminal volta,
do jeito que estava.

Tem mais duas fotos para testar: `tela.jpg` (uma foto da tela do NovaEra,
1024 × 768) e `logo.bmp` (a logo pequena, 64 × 64). Uma foto maior que a
tela é diminuída para caber; uma menor é aumentada, sem deformar.

## A tela emprestada

O servidor de vídeo é o dono da tela. Para um programa desenhar nela, o
servidor **empresta** a tela:

1. O programa cria uma **memória compartilhada** (Fase 4, Etapa 7) do
   tamanho da tela: 1024 × 768 pontos × 4 bytes = 3 MiB.
2. Pede ao vídeo: "use esta memória como tela" (`VIDEO_TELA_ABRIR`).
3. Desenha na memória, à vontade, sem pedir nada a ninguém.
4. Quando termina um desenho, avisa: "mostre este pedaço"
   (`VIDEO_TELA_MOSTRAR`). O vídeo copia o pedaço para a placa.
5. No fim, devolve a tela (`VIDEO_TELA_FECHAR`), e o vídeo redesenha o
   terminal.

Enquanto a tela está emprestada, o terminal continua funcionando "por
baixo": o vídeo guarda cada letra numa grade e redesenha tudo na volta. E se
o programa morrer sem devolver a tela, o vídeo percebe e pega ela de volta.

Desenhar primeiro na memória e só depois mostrar tem outra vantagem: a tela
nunca mostra um desenho pela metade (sem "piscar").

## A biblioteca gráfica

Os programas usam a `grafico.h`, que faz parte da libc do NovaEra:

```c
#include <grafico.h>

int main(void)
{
    gr_imagem_t *tela = gr_tela_abrir();
    gr_degrade(tela, 0, 0, tela->largura, tela->altura, GR_NOITE, GR_RGB(30, 60, 110), 1);
    gr_disco(tela, 512, 384, 100, GR_RGB(255, 170, 40));          /* o sol */
    gr_texto(tela, 432, 520, "Olá, tela!", GR_BRANCO, 2);        /* letras 2x */
    gr_tela_mostrar_tudo(tela);
    gr_mouse(0, 0, 0);                                           /* espera o mouse */
    gr_tela_fechar(tela);
    return 0;
}
```

| Função | O que faz |
|---|---|
| `gr_ponto`, `gr_ler` | pinta / lê um ponto |
| `gr_linha` | uma linha de um ponto a outro |
| `gr_retangulo`, `gr_preencher` | o contorno / o retângulo cheio |
| `gr_circulo`, `gr_disco` | o contorno / o círculo cheio |
| `gr_degrade` | um degradê (de cima para baixo, ou da esquerda para a direita) |
| `gr_copiar` | copia um pedaço de uma imagem para outra |
| `gr_escalar` | copia aumentando ou diminuindo |
| `gr_texto` | escreve (com acentos) com a fonte 8×16, em 1x, 2x, 3x... |
| `gr_carregar` | abre um PNG, JPEG ou BMP |
| `gr_salvar_ppm` | salva uma imagem num arquivo PPM |
| `gr_tela_abrir`, `gr_tela_mostrar`, `gr_tela_fechar` | a tela emprestada |
| `gr_mouse` | espera o mouse se mexer ou um botão |

### As cores e a transparência

Cada ponto é um número de 32 bits: `0xAARRGGBB`. RR, GG e BB dizem quanto
de vermelho, verde e azul (de 0 a 255); AA diz o quanto a cor **cobre** o
que já está embaixo: 255 cobre tudo, 0 é invisível, 128 é meio a meio.

```c
gr_preencher(tela, 100, 100, 200, 200, GR_RGBA(0, 0, 0, 128));   /* uma sombra */
```

A sombra do `ver` é assim: um retângulo preto meio transparente embaixo da
foto.

## As fotos: o stb_image

Ler um PNG ou um JPEG é muito trabalho: os dois guardam a foto
**comprimida** (o PNG sem perder nada; o JPEG jogando fora o que o olho não
vê). O NovaEra usa o **stb_image**, de Sean Barrett: um único arquivo em C,
em domínio público, usado em milhares de jogos e programas. Ele está em
`libc/src/stb/stb_image.h`, do jeito que veio, e a `imagem.c` liga ele à
libc: lê o arquivo inteiro para a memória e pede ao stb para abrir.

É a primeira vez que o NovaEra usa um código de fora tão grande (quase 8 mil
linhas) e ele compila sem mudar nada, com o cproc, dentro do próprio
NovaEra.

## Como saber se o desenho está certo

A `grafico.c` não depende do NovaEra: compila também no Linux. O programa
`testegrafico` desenha uma cena (todas as funções, com transparência) e
calcula uma "impressão digital" (um hash) de cada ponto. Os números no
NovaEra e no Linux têm de ser **iguais**:

```
testegrafico /usr/share/nefs/exemplos/imagens/logo.png /usr/share/nefs/exemplos/imagens/tela.jpg
```

```
cena 320x240: 5540b5b8
logo.png 256x256: e5c29a54
tela.jpg 1024x768: 42f500ee
```

Se um único ponto estiver diferente, o número muda todo.

Na Etapa 5, cada janela vai ser uma imagem dessas, desenhada com a mesma
biblioteca; o servidor de janelas só junta as imagens na tela.
