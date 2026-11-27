# 5. Instalar o sistema

```
instalar-sistema /mnt
```

Ele copia o NovaEra que está rodando (da ISO) para o disco: `/bin` (os
programas), `/etc` (a configuração), `/usr` (o compilador, as bibliotecas,
o código-fonte e os livros) e `/boot` (o `kernel.bin`). E cria as pastas
que todo sistema tem: `/home`, `/root`, `/tmp`, `/mnt`, `/disco`.

É o `pacstrap` do Arch. Demora um pouco: são uns 9 MB.

## As montagens

O sistema novo precisa saber que a `sda2` vai em `/dados` a cada boot:

```
gerar-montagens /mnt
cat /mnt/etc/montagens
```

```
sda2 /dados
```

É o `genfstab` do Arch: ele olha o que está montado dentro de `/mnt` agora
e escreve no `/mnt/etc/montagens`. Confira sempre o resultado.
