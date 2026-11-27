#!/data/data/com.termux/files/usr/bin/bash
# Roda o kernel.bin num PC x86_64 virtual (QEMU).
#   ./run.sh          -> tela de texto dentro do próprio Termux (curses)
#   ./run.sh vnc      -> tela gráfica via app cliente VNC (localhost:5901)
#   ./run.sh debug    -> como o padrão, mas QEMU sai em vez de reiniciar e grava
#                        interrupções/resets em qemu-debug.log
#   ./run.sh disco    -> o NovaEra instalado no disco.img, em texto (curses)
#   ./run.sh disco vnc -> o NovaEra instalado, com a tela de entrada gráfica (VNC)
cd "$(dirname "$0")"
[ -f kernel.bin ] || { echo "kernel.bin não existe. Rode ./build.sh primeiro."; exit 1; }

QEMU="$(command -v qemu-system-x86_64 || true)"   # o NovaEra é 64 bits
[ -n "$QEMU" ] || { echo "QEMU não encontrado. No Termux: pkg install qemu-system-x86-64-headless"; exit 1; }

MODO="${1:-curses}"
APPEND=()
case "$MODO" in grafico|vnc) APPEND=(-append grafico) ;; esac   # a tela gráfica (1024x768)
if [ "$MODO" = "disco" ]; then
    # O NovaEra instalado no disco.img (instalar-sistema + instalar-boot):
    # a BIOS lê o disco e o GRUB dele dá o boot. Nada de -kernel/-initrd.
    BASE=(-m 64M -nic none)
    if [ "${2:-}" = "vnc" ]; then
        MODO="vnc"
    else
        # O curses só mostra texto: com uma placa sem o modo gráfico (cirrus),
        # o NovaEra liga direto no login de texto, mesmo pela opção gráfica do GRUB.
        BASE+=(-vga cirrus)
        MODO="curses"
    fi
else
    BASE=(-kernel kernel.bin -m 64M -nic none "${APPEND[@]}")
    [ -f initrd.tar ] && BASE+=(-initrd initrd.tar)   # os programas que o shell executa
fi

# O disco do NovaEra (/disco): um arquivo de 64 MiB, criado no primeiro uso.
# Dentro do NovaEra, rode "formatar" uma vez. O que você guardar lá fica.
if [ ! -f disco.img ]; then
    echo "Criando disco.img (64 MiB). Dentro do NovaEra, rode: formatar"
    dd if=/dev/zero of=disco.img bs=1M count=64 2>/dev/null || truncate -s 64M disco.img
fi
BASE+=(-drive file=disco.img,format=raw,index=0,media=disk)

if [ "$MODO" != "vnc" ] && ! "$QEMU" -display help 2>&1 | grep -qw curses; then
    echo "Este QEMU não tem modo curses; usando VNC."
    MODO="vnc"
fi

aviso_tamanho() {
    local c l
    c="$(tput cols 2>/dev/null || echo 0)"; l="$(tput lines 2>/dev/null || echo 0)"
    if [ "$c" -lt 80 ] || [ "$l" -lt 25 ]; then
        echo "Seu terminal tem ${c}x${l}; o PC virtual precisa de 80x25."
        echo "Gire o celular para horizontal e/ou diminua a fonte (gesto de pinça)."
        echo "Iniciando em 4 segundos..."; sleep 4
    fi
}

case "$MODO" in
    curses)
        aviso_tamanho
        exec "$QEMU" "${BASE[@]}" -display curses ;;
    debug)
        aviso_tamanho
        exec "$QEMU" "${BASE[@]}" -no-reboot -display curses -d int,cpu_reset -D qemu-debug.log ;;
    vnc)
        echo "Modo gráfico (1024x768) por VNC."
        echo "Abra um app cliente VNC (bVNC, RealVNC...) e conecte em: localhost:5901"
        echo "Para encerrar: Ctrl+C aqui no Termux."
        exec "$QEMU" "${BASE[@]}" -vga std -vnc :1 ;;
    grafico)
        # numa janela (no computador; no Termux, use: bash run.sh vnc)
        exec "$QEMU" "${BASE[@]}" -vga std ;;
    *)
        echo "Uso: ./run.sh [curses|vnc|grafico|debug|disco]"; exit 1 ;;
esac
