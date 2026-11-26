# A imagem de boot do GRUB

`eltorito.img` é o **GRUB 2** (https://www.gnu.org/software/grub/), um
programa livre sob a licença **GNU GPL versão 3**. O código-fonte do GRUB
está em https://ftp.gnu.org/gnu/grub/ (a versão exata em `versao.txt`).

Ela dá o boot das ISOs que o `nefs iso` gera: a BIOS carrega a imagem do
CD, o GRUB lê `/boot/grub/grub.cfg` e carrega o `kernel.bin` (Multiboot) e
o `initrd.tar`.

O GRUB não compila dentro do NovaEra, então a imagem vem pronta, feita no
Linux com `gerar.sh` (o `grub-mkimage`, do pacote `grub-pc-bin`).
