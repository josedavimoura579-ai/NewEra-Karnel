# 4. Formatar e montar

Uma partição recém-criada é só espaço vazio. **Formatar** é preparar
dentro dela um sistema de arquivos.

```
formatar -t ext2 sda1
formatar sda2
```

- a `sda1` em **ext2**: o sistema de arquivos com dono e permissões (o do
  Linux);
- a `sda2` em **FAT32** (o padrão do `formatar`): abre em qualquer lugar.

Agora **monte** a partição do sistema em `/mnt`, e a outra dentro dela:

```
montar sda1 /mnt
mkdir /mnt/dados
montar sda2 /mnt/dados
montar
```

```
sda1 em /mnt (ext2)
sda2 em /mnt/dados (fat32)
```

Daqui em diante, tudo o que for gravado em `/mnt` vai para o disco novo.
