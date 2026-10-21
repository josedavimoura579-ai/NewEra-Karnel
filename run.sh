#!/data/data/com.termux/files/usr/bin/bash
# Roda o kernel.bin num PC x86_64 virtual (QEMU).
#   ./run.sh          -> tela de texto dentro do próprio Termux (curses)
#   ./run.sh vnc      -> tela gráfica via app cliente VNC (localhost:5901)
#   ./run.sh debug    -> como o padrão, mas QEMU sai em vez de reiniciar e grava
#                        interrupções/resets em qemu-debug.log
cd "$(dirname "$0")"
[ -f kernel.bin ] || { echo "kernel.bin não existe. Rode ./build.sh primeiro."; exit 1; }

QEMU="$(command -v qemu-system-x86_64 || true)"   # o NovaEra é 64 bits
[ -n "$QEMU" ] || { echo "QEMU não encontrado. Rode: pkg install qemu-system-x86-64"; exit 1; }

MODO="${1:-curses}"
BASE=(-kernel kernel.bin -m 64M -nic none)

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
        echo "Abra um app cliente VNC (bVNC, RealVNC...) e conecte em: localhost:5901"
        echo "Para encerrar: Ctrl+C aqui no Termux."
        exec "$QEMU" "${BASE[@]}" -vnc :1 ;;
    *)
        echo "Uso: ./run.sh [curses|vnc|debug]"; exit 1 ;;
esac
