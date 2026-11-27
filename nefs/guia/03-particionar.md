# 3. Particionar o disco

Uma **partição** é um pedaço do disco que funciona como um disco separado.
Vamos fazer duas:

| Partição | Tamanho | Tipo | Para quê |
|---|---|---|---|
| `sda1` | 200 MiB | Linux (ext2) | o sistema: `/bin`, `/etc`, `/home`... |
| `sda2` | o resto | FAT32 | trocar arquivos com o Windows e o celular |

(Se preferir, faça só a `sda1`, com `criar resto linux`.)

```
particionar sda limpar
particionar sda criar 200M linux
particionar sda criar resto fat32
particionar sda boot 1
particionar sda
```

- `limpar` apaga a tabela de partições (ele pergunta antes: digite `sim`);
- `criar` acrescenta uma partição depois da última, alinhada em 1 MiB;
- `boot 1` marca a `sda1` como a de dar boot.

```
Dispositivo  Boot  Inicio     Fim        Tamanho    Tipo
sda1         *     2048       411647     200 MiB    83 Linux (ext2)
sda2               411648     524287     55 MiB     0c FAT32
```

O primeiro MiB (os setores 0 a 2047) fica livre de propósito: é onde o
GRUB vai morar.
