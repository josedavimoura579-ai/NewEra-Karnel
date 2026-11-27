# 6. Configurar o sistema novo

```
entrar /mnt
```

Agora você está **dentro** do sistema novo: o `/` é o `/mnt`. O que você
mudar aqui vale para ele, não para a ISO. (É o `arch-chroot` do Arch.)

## O nome da máquina

```
echo meupc > /etc/hostname
```

O prompt vai mostrar `meupc` no lugar de `NovaEra` (a partir do próximo
shell).

## A senha do root

```
passwd
```

Escolha uma senha (até 31 letras). Ela não aparece enquanto você digita.

## O seu usuário

```
adduser ana
adduser ana sudo
```

O segundo comando põe a `ana` no grupo `sudo`: ela vai poder usar o `sudo`
para tarefas de administrador, com a senha dela.

## Conferir

```
cat /etc/passwd
ls -l /home
sair
```

O `sair` volta para a ISO.
