#!/bin/sh
# ===========================================================================
#  NEFS - nefs/iso/novaera-iso.sh
#  A ISO do NEFS SEM Linux: o NovaEra puro, que dá boot direto (GRUB) e traz
#  o compilador (cproc + QBE + nefs-cpp/as/ld), o nefs, o código-fonte em
#  /usr/src e o livro. Dentro dela:
#
#      formatar                    # o disco (uma vez)
#      nefs novo /disco/meu Meu Sistema
#      nefs compilar /disco/meu
#      nefs iso -o /disco/meu.iso /disco/meu
#
#  Uso:  sh nefs/iso/novaera-iso.sh      (precisa de internet na 1ª vez,
#        para os portes: c9x.me e git.sr.ht)
#  Resultado: nefs/iso/nefs-novaera-<versão>-x86_64.iso
#  Rodar:     qemu-system-x86_64 -cdrom <a ISO> -m 256M \
#                 -drive file=disco.img,format=raw   (opcional: o disco)
# ===========================================================================
set -eu
cd "$(dirname "$0")/../.."
sh portes/baixar.sh
sh build.sh
mkdir -p build/ferramentas
${HOSTCC:-cc} -O2 -o build/ferramentas/nefs ferramentas/nefs.c
VERSAO="$(sed -n 's/^NEFS_VERSAO="\(.*\)"/\1/p' nefs/nefs)"
build/ferramentas/nefs iso -o "nefs/iso/nefs-novaera-$VERSAO-x86_64.iso"
