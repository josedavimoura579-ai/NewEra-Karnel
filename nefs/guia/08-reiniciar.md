# 8. Reiniciar

```
desligar
```

O `desligar` grava tudo o que estava na memória dos servidores de disco e
desliga. Agora ligue **sem a ISO**: só o disco.

```
qemu-system-x86_64 -m 256M -display curses \
    -drive file=disco.img,format=raw,index=0,media=disk
```

(No NovaEra compilado: `bash run.sh disco`.)

```
[init] montando a raiz: sda1 em /
[init] montando sda2 em /dados

NovaEra Microkernel - entre com o seu usuario.
login:
```

Entre com o seu usuário:

```
login: ana
Senha:
Bem-vindo, ana!
ana@meupc:/home/ana>
```

**Pronto: o NovaEra está instalado.** Tudo o que você criar fica guardado.
Para desligar, sempre: `sudo desligar`.
