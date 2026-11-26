# 10. O ext2: o disco com dono e permissões

O FAT32 é ótimo para trocar arquivos com o Windows e o celular, mas não
guarda **de quem** é cada arquivo, nem **quem pode** ler ou mudar. Um
sistema com vários usuários precisa disso. O **ext2** guarda: é o sistema
de arquivos clássico do Linux, e é onde o NovaEra vai morar quando
aprender a dar boot pelo disco.

## Na prática

```
particionar sdb criar resto linux     uma partição do tipo Linux (83)
formatar -t ext2 sdb2                 o ext2 dentro dela
montar sdb2 /mnt
echo ola > /mnt/ola.txt
mkdir /mnt/projetos
montar                                sdb2 em /mnt (ext2)
desmontar /mnt
```

O `montar` descobre sozinho se o disco é FAT32 ou ext2. Um disco
formatado no Linux com `mkfs.ext2` também é montado, e o que o NovaEra
grava passa no `e2fsck` do Linux sem nenhum erro.

## Como o ext2 guarda as coisas

Pense numa biblioteca:

- O disco é dividido em **blocos** (1 ou 4 KiB): as prateleiras.
- Cada arquivo tem uma **ficha**, o **inode**: tipo (arquivo ou pasta),
  **dono**, **grupo**, **permissões**, tamanho e a lista das prateleiras
  onde está o conteúdo. A ficha **não tem o nome** do arquivo.
- Uma **pasta** é um arquivo especial: uma lista de "nome → número da
  ficha". Por isso o mesmo arquivo pode ter dois nomes (um *link*).
- Para achar prateleiras e fichas livres, há **mapas de bits**: um bit por
  bloco e um por inode (1 = ocupado).
- O **superbloco**, no começo do disco, diz o tamanho de tudo. Ele tem
  cópias espalhadas pelo disco, para o caso de o original estragar.
- O disco é dividido em **grupos** (pedaços de 8 MiB ou 128 MiB), cada um
  com os seus mapas e as suas fichas, para o conteúdo de um arquivo ficar
  perto da ficha dele.

Um inode guarda 12 números de blocos diretos. Para arquivos maiores, o 13º
aponta para um bloco **cheio de números** de blocos; o 14º, para um bloco
de blocos de números; e o 15º, mais um nível. Com blocos de 4 KiB, um
arquivo pode chegar a 4 GiB.

As permissões são o famoso `rwxr-xr-x` do Linux (no NovaEra, por
enquanto, todo arquivo novo é do root, com `644` para arquivos e `755`
para pastas). Quem confere as permissões, e quem é "você", vem na próxima
etapa: usuários, `login` e `sudo`.

## Como funciona no NovaEra

O ext2 é mais um servidor no anel 3, o **PID 9**, ao lado do FAT32 (PID 8).
Quando você monta um disco, o `montar` pergunta primeiro ao FAT32 e depois
ao ext2 se eles reconhecem o disco; quem reconhece fica com ele.

O NovaEra não conhece o **diário** do ext3/ext4 nem os seus recursos novos:
um disco ext4 é recusado, e um ext2 com recursos desconhecidos é montado
só para leitura, para nunca estragar o que o Linux gravou.
