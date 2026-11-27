# 8. Reiniciar

```
desligar
```

O `desligar` grava tudo o que estava na memória dos servidores de disco e
desliga. Agora ligue **sem a ISO**: só o disco.

```
qemu-system-x86_64 -m 256M -vnc :1 \
    -drive file=disco.img,format=raw,index=0,media=disk
```

(No NovaEra compilado: `bash run.sh disco vnc`.)

Aparece a **tela de entrada**: digite o seu usuário, Tab, a senha e Enter.
A sessão abre com um terminal numa janela, na sua pasta pessoal. Ctrl+Q
sai da sessão (e a tela de entrada volta).

Em texto, no próprio terminal:

```
qemu-system-x86_64 -m 256M -display curses -vga cirrus \
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
