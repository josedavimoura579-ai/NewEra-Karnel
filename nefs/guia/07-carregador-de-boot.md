# 7. O carregador de boot

Sem um carregador de boot, o computador não sabe que existe um sistema no
disco.

```
instalar-boot sda /mnt
cat /mnt/boot/grub/grub.cfg
```

Ele grava o **GRUB** no começo do disco `sda` e cria o menu
`/boot/grub/grub.cfg` na partição montada em `/mnt`:

```
set timeout=3
menuentry "NovaEra (sda1)" {
    multiboot /boot/kernel.bin root=sda1
}
```

Como um computador liga pelo disco:

1. a **BIOS** lê o setor 0 do disco (a MBR) e pula para o código dele (o
   começo do GRUB);
2. ele carrega o resto do GRUB, nos setores 1 a 2047;
3. o GRUB acha a partição com o `/boot/grub/grub.cfg` e carrega o
   `kernel.bin` com `root=sda1`;
4. o NovaEra monta a `sda1` em `/`, o `/bin/init` monta o `/etc/montagens`
   e mostra o login.
