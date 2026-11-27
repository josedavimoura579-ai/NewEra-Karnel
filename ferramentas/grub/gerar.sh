#!/bin/sh
# ===========================================================================
#  NEFS - ferramentas/grub/gerar.sh
#  Gera eltorito.img (o GRUB das ISOs, "nefs iso") e disco.img + boot.img
#  (o GRUB de um disco rígido, "instalar-boot").
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
# disco.img: o GRUB de um disco rígido (o instalar-boot o grava nos setores
# 1 a 2047, antes da 1ª partição), com o que ele precisa para achar e ler
# o /boot/grub/grub.cfg numa partição ext2 ou FAT (disco.cfg, embutido).
grub-mkimage -O i386-pc -p /boot/grub -c disco.cfg -o disco.img \
    biosdisk part_msdos ext2 fat search search_fs_file normal configfile multiboot boot echo
# boot.img: os 440 bytes que vão na MBR (o setor 0); vem pronto no pacote.
cp /usr/lib/grub/i386-pc/boot.img boot.img
grub-mkimage --version > versao.txt
echo "eltorito.img: $(wc -c < eltorito.img) bytes, disco.img: $(wc -c < disco.img) bytes ($(cat versao.txt))"
