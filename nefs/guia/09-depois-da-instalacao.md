# 9. Depois da instalação

## Primeiros passos

```
whoami                          quem sou eu
sudo adduser beto               mais um usuário
echo oi > /dados/oi.txt         o FAT32: abre no Windows e no celular
nefs livro                      o Livro 1: como o NovaEra funciona
```

## O modo gráfico

```
term                            mais um terminal (numa janela)
relogio                         o relógio
calculadora                     a calculadora
echo texto > /etc/modo          ligar sempre em texto (como root)
echo /home/ana/foto.jpg > /etc/papel    outro papel de parede
```

(Os três primeiros abrem uma janela nova a partir de um terminal da
sessão; o terminal fica esperando a janela fechar.) Tudo sobre o modo
gráfico está no Livro 1, capítulos 16 a 23.

## Atualizar o sistema

Ligue por uma ISO (ou um NovaEra) mais nova, com o disco junto (no QEMU,
com `-boot d`, para ligar pelo CD e não pelo disco), e copie o sistema por
cima:

```
montar sda1 /mnt
instalar-sistema /mnt
desligar
```

Os programas (`/bin`, `/usr`) e o núcleo (`/boot/kernel.bin`) são
trocados. Os arquivos do `/etc` que já existiam (`passwd`, `sombra`,
`group`, `hostname`, `montagens`) ficam como estavam: os seus usuários e
senhas continuam. E as pastas pessoais (`/home`) nem são tocadas.

## Se o sistema não liga

Ligue pela ISO (`-boot d` no QEMU) e monte o disco: dá para ver e
consertar tudo.

```
montar sda1 /mnt
cat /mnt/etc/montagens          um disco que não existe mais?
entrar /mnt                     consertar senhas, usuários...
instalar-boot sda /mnt          refazer o GRUB
```

## O que você aprendeu

Você fez à mão o que todo instalador faz escondido: partições, sistemas de
arquivos, a cópia do sistema, a configuração, o carregador de boot. Daqui
para frente, nenhuma tela de instalação vai ser mistério.
