# Fase 6 — a área de trabalho do NovaEra e o modo pronto

**Objetivo:** o NovaEra virar um sistema que qualquer pessoa usa sem
decorar comandos. Uma **área de trabalho** própria, leve, no estilo do
XFCE (painel, menu, gerenciador de arquivos, editor), **pacotes** para
instalar programas e um **instalador gráfico** no estilo do Debian/Ubuntu.

**Entrega:** o modo pronto: liga o CD, clica em "Instalar o NovaEra" e, em
poucos minutos, tem um sistema com área de trabalho no disco.

## O NovaEra e o NEFS

A Fase 6 é do **NovaEra**, o sistema pronto: ela termina com o lançamento
**NovaEra 5.0**. O **NEFS** (a oficina de tela preta, no estilo do Linux
From Scratch) continua sendo a oficina: o livro ganha os capítulos de cada
etapa, e quem monta o próprio sistema pelo NEFS herda tudo isso no código
(lançamento **NEFS 0.4**).

## Decisões

- **Sem rede ainda.** A rede (placa, TCP/IP) é a Fase 7. Os pacotes vêm do
  CD ou de um disco, não da internet.
- **A área de trabalho é o servidor de janelas da Fase 5, crescido.** O
  `janelas` ganha tipos de janela (o painel, os menus), minimizar e
  maximizar, e a lista de janelas abertas. Nada de X11 (Fase 7).
- **O teclado brasileiro.** O layout **ABNT2**, com ç e os acentos (as
  "teclas mortas": ´ ` ~ ^ ¨), e as teclas que ainda faltam: setas, Home,
  End, Page Up/Down, Delete, F1 a F12, Alt. O `/etc/teclado` escolhe `br`
  ou `us`.
- **Ícones próprios**, desenhados para o NovaEra (pequenos PNGs com as
  cores da logo), nada copiado de outro sistema.
- **Os aplicativos se anunciam** com um arquivo pequeno em
  `/usr/share/aplicativos/` (nome, ícone, comando, categoria), como os
  `.desktop` do Linux: o menu e o gerenciador de arquivos leem dali.
- **O instalador** roda na sessão ao vivo do CD, como no Ubuntu; e o
  manual (o Livro 2, `nefs guia`) continua lá, para quem quiser fazer à mão.

## Etapas

Cada etapa termina com algo testável no QEMU (e uma foto da tela).

- [ ] **Etapa 1 — o teclado completo**
  - o layout ABNT2 (ç, acentos com as teclas mortas, o / e o ? da tecla
    do lado do Shift direito) e o US; o `/etc/teclado`
  - as teclas especiais: setas, Home, End, Page Up/Down, Insert, Delete,
    F1–F12, Alt, a tecla Windows
  - no terminal, as setas como no Linux (`Esc [ A`...): o shell ganha o
    histórico (seta para cima) e a edição da linha (setas, Home, End)
  - nas janelas, as teclas especiais chegam como eventos próprios
  - *Teste: digitar "ação, você, à, pôr" no terminal e no bloco de notas;
    a seta para cima traz o comando anterior.*
- [ ] **Etapa 2 — o painel e o menu**
  - o painel embaixo: o botão do menu (com a logo), a lista das janelas
    abertas, o relógio e o botão de sair
  - o menu dos aplicativos (lidos de `/usr/share/aplicativos/`), por
    categoria
  - o servidor de janelas: tipos de janela (painel, menu), minimizar,
    maximizar (duplo clique na barra), Alt+Tab
  - *Teste: abrir três programas pelo menu, minimizar um e trazê-lo de
    volta pelo painel.*
- [ ] **Etapa 3 — mais controles**
  - para a `janela.h`: lista com rolagem, barra de rolagem, área de texto
    de várias linhas, menus (o de cima da janela e o do botão direito),
    caixas de diálogo (mensagem, "tem certeza?", abrir e salvar arquivo)
  - a área de transferência (copiar e colar entre programas), no servidor
    de janelas
  - *Teste: um programa de exemplo usa todos os controles; copiar num e
    colar no outro.*
- [ ] **Etapa 4 — o gerenciador de arquivos**
  - as pastas e os arquivos com ícones; navegar (voltar, subir, a pasta
    pessoal); o caminho no alto
  - abrir: texto no editor, imagem no `ver`, programa roda
  - copiar, recortar, colar, apagar, renomear, nova pasta; as
    propriedades (tamanho, dono, permissões)
  - os discos montados ao lado (`/`, `/dados`...)
  - *Teste: criar uma pasta, copiar uma foto para ela, abrir a foto com
    dois cliques.*
- [ ] **Etapa 5 — o editor de texto**
  - abrir, salvar, salvar como; vários arquivos (abas)
  - o cursor com as setas e o mouse, selecionar, copiar, recortar, colar,
    desfazer; localizar e substituir
  - as cores do código C (o "realce de sintaxe"), para programar no
    próprio NovaEra; compilar com o cproc por um botão
  - *Teste: escrever um "olá" em C no editor, compilar e rodar sem sair
    da área de trabalho.*
- [ ] **Etapa 6 — as configurações**
  - um painel de configurações: papel de parede (escolher uma foto), o
    fuso, o teclado, o nome do computador, os usuários (criar, trocar a
    senha), o tema (claro ou escuro)
  - o `ver` numa janela (com anterior/próxima) e a calculadora no menu
  - *Teste: trocar o papel de parede e o tema pelas configurações; a
    tela de entrada também muda.*
- [ ] **Etapa 7 — os pacotes**
  - um formato simples (`.nep`: um tar com um manifesto: nome, versão,
    descrição, dependências, a lista dos arquivos)
  - o comando `pacote`: instalar, remover, listar, procurar, informações;
    o registro do que está instalado (`/var/lib/pacotes`)
  - um repositório no CD (e num disco), sem internet
  - o `nefs pacote` para empacotar um programa feito no NEFS
  - uma "loja" simples na área de trabalho
  - *Teste: instalar um pacote do CD, usá-lo e removê-lo, sem sobrar
    arquivos.*
- [ ] **Etapa 8 — o instalador gráfico**
  - o CD liga na sessão ao vivo, com o ícone "Instalar o NovaEra"
  - os passos: o teclado, o disco (usar o disco inteiro, ou escolher a
    partição), o usuário e a senha, o nome do computador, o nome e a logo
    do sistema, "com área de trabalho" ou "só terminal"; a barra de
    progresso; reiniciar
  - por baixo, os mesmos comandos do Livro 2 (`particionar`, `formatar`,
    `instalar-sistema`, `instalar-boot`)
  - *Teste: instalar num disco vazio só com o mouse e ligar pelo disco
    direto na área de trabalho.*
- [ ] **Etapa 9 — o livro e o lançamento**
  - o Livro 1 ganha os capítulos da área de trabalho, dos pacotes e do
    instalador; o Livro 2 ganha o caminho gráfico ao lado do manual
  - os lançamentos **NovaEra 5.0** (o CD com a sessão ao vivo e o
    instalador) e **NEFS 0.4** (a oficina, com o livro novo)
  - *Entrega: um sistema com área de trabalho em poucos minutos.*
