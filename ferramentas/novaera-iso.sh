#!/bin/sh
# ===========================================================================
#  NovaEra - ferramentas/novaera-iso.sh
#  A ISO do SISTEMA NovaEra, pronto para usar: dá boot direto (GRUB), em
#  modo gráfico (o texto é a 2ª opção do menu), e se instala num disco
#  (nefs guia). Traz também o compilador (cproc + QBE + nefs-cpp/as/ld), o
#  comando nefs, o código-fonte em /usr/src e os livros.
#
#  (Não é o NEFS. O NEFS é a oficina, no estilo do Linux From Scratch:
#   a ISO de tela preta de nefs/iso/construir.sh, para montar o SEU sistema.)
#
#  Uso:  sh ferramentas/novaera-iso.sh   (precisa de internet na 1ª vez,
#        para os portes: c9x.me e git.sr.ht)
#  Resultado: build/novaera-<versão>-x86_64.iso   (a versão do novaera.h)
#  Rodar:     qemu-system-x86_64 -cdrom <a ISO> -m 256M \
#                 -drive file=disco.img,format=raw   (opcional: o disco)
# ===========================================================================
set -eu
cd "$(dirname "$0")/.."
sh portes/baixar.sh
sh build.sh
mkdir -p build/ferramentas
${HOSTCC:-cc} -O2 -o build/ferramentas/nefs ferramentas/nefs.c
VERSAO="$(sed -n 's/^#define NOVAERA_VERSAO *"\(.*\)".*/\1/p' novaera.h)"
build/ferramentas/nefs iso -o "build/novaera-$VERSAO-x86_64.iso"
