#!/bin/sh
# ===========================================================================
#  NEFS - ferramentas/grub/gerar.sh
#  Gera eltorito.img: o GRUB que dá o boot das ISOs do NovaEra ("nefs iso").
#  O GRUB não compila dentro do NovaEra, então a imagem vem pronta, feita
#  no Linux com o grub-mkimage (pacote grub-pc-bin) e estes módulos:
#    biosdisk iso9660   ler o CD pela BIOS
#    normal configfile  ler /boot/grub/grub.cfg
#    multiboot boot     carregar o kernel.bin (Multiboot 1) e o initrd.tar
#    echo               (para mensagens no grub.cfg)
# ===========================================================================
set -eu
cd "$(dirname "$0")"
# inicio.cfg, embutido: o GRUB procura o grub.cfg no CD, (cd), em qualquer
# BIOS (sem isso, ele às vezes aponta para um disco que não existe).
grub-mkimage -O i386-pc-eltorito -p /boot/grub -c inicio.cfg -o eltorito.img \
    biosdisk iso9660 normal configfile multiboot boot echo
grub-mkimage --version > versao.txt
echo "eltorito.img: $(wc -c < eltorito.img) bytes ($(cat versao.txt))"
