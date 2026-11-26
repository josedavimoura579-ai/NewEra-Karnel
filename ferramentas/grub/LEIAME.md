# As imagens de boot do GRUB

Estes arquivos `.img` são o **GRUB 2** (https://www.gnu.org/software/grub/), um
programa livre sob a licença **GNU GPL versão 3**. O código-fonte do GRUB
está em https://ftp.gnu.org/gnu/grub/ (a versão exata em `versao.txt`).

`eltorito.img` dá o boot das ISOs que o `nefs iso` gera: a BIOS carrega a
imagem do CD, o GRUB lê `/boot/grub/grub.cfg` e carrega o `kernel.bin`
(Multiboot) e o `initrd.tar`.

`boot.img` (a MBR, 440 bytes de código) e `disco.img` (o resto do GRUB,
nos setores antes da 1ª partição) dão o boot de um disco rígido: o
`instalar-boot` os grava. O GRUB procura a partição com o
`/boot/grub/grub.cfg` (`disco.cfg`, embutido) e carrega o `kernel.bin`
com `root=sdXN`.

O GRUB não compila dentro do NovaEra, então a imagem vem pronta, feita no
Linux com `gerar.sh` (o `grub-mkimage`, do pacote `grub-pc-bin`).
