#!/usr/bin/env bash
# ===========================================================================
#  NEFS - nefs/iso/construir.sh
#  Monta a ISO do NEFS: um Linux mínimo que liga direto num terminal com
#  tudo o que é preciso para criar um sistema operacional sobre o NovaEra.
#
#  Dentro da ISO:
#    - kernel Linux + BusyBox (o terminal, os comandos básicos, o vi)
#    - o compilador do NEFS: cproc (C) + QBE (backend) + nefs-as, nefs-ld e
#      nefs-ar (montador, ligador e bibliotecas, deste repositório)
#    - QEMU (para testar o sistema criado) e GRUB (para gerar ISOs)
#    - o comando nefs, o código do NovaEra (modelo) e o Livro do NEFS
#
#  Precisa de: Debian ou Ubuntu x86_64, root (para o apt-get) e internet
#  para baixar o QBE (c9x.me) e o cproc (git.sr.ht) na primeira vez.
#  Os fontes ficam guardados em nefs/iso/fontes/ e podem ser colocados lá
#  à mão (qbe-*.tar.xz e cproc/), para construir sem internet.
#
#  Uso:  sudo bash nefs/iso/construir.sh
#        Resultado: nefs/iso/nefs-<versão>-x86_64.iso
#
#  Opção de desenvolvimento:
#    --sem-compilador   monta a ISO sem cproc/QBE, só para testar o resto.
#                       NÃO é a ISO do NEFS: o nome do arquivo deixa claro.
# ===========================================================================
set -euo pipefail

AQUI="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$AQUI/../.." && pwd)"
TRABALHO="$AQUI/trabalho"
FONTES="$AQUI/fontes"
RAIZ="$TRABALHO/raiz"            # o sistema de arquivos da ISO
CD="$TRABALHO/cd"                # o conteúdo do CD (kernel + initramfs + GRUB)

NEFS_VERSAO="$(sed -n 's/^NEFS_VERSAO="\(.*\)"/\1/p' "$REPO/nefs/nefs")"
# Versões testadas juntas: o NovaEra compila e roda com elas.
QBE_VERSAO="1.3"
QBE_URL="https://c9x.me/compile/release/qbe-$QBE_VERSAO.tar.xz"
CPROC_URL="https://git.sr.ht/~mcf/cproc"
CPROC_COMMIT="d1c53ddf56571573a7025324c8dd5c6d547a4d1f"   # 2026-04-27, usa QBE 1.3

SEM_COMPILADOR=0
[ "${1:-}" = "--sem-compilador" ] && SEM_COMPILADOR=1

passo() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
erro()  { printf '\033[1;31mERRO:\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(uname -m)" = "x86_64" ] || erro "construa a ISO numa máquina x86_64"
command -v apt-get >/dev/null || erro "este script precisa de Debian ou Ubuntu (apt-get)"

# --- 1. Ferramentas da máquina que constrói --------------------------------
passo "Instalando as ferramentas de construção (apt-get)"
PACOTES="busybox-static binutils gcc make git curl xz-utils cpio gzip
         qemu-system-x86 grub-pc-bin grub-efi-amd64-bin grub-common xorriso mtools"
# shellcheck disable=SC2086
if ! dpkg -s $PACOTES >/dev/null 2>&1; then
    [ "$(id -u)" = 0 ] || erro "rode como root (sudo) para instalar: $PACOTES"
    apt-get update -qq
    # shellcheck disable=SC2086
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $PACOTES >/dev/null
fi

rm -rf "$TRABALHO"
mkdir -p "$TRABALHO" "$FONTES"

# --- 2. O compilador do NEFS: QBE + cproc ----------------------------------
COMPILADOR="$TRABALHO/compilador"   # instalado em $COMPILADOR/usr/...
if [ "$SEM_COMPILADOR" = 0 ]; then
    passo "Compilando o QBE $QBE_VERSAO"
    if [ ! -f "$FONTES/qbe-$QBE_VERSAO.tar.xz" ]; then
        curl -fsSL -o "$FONTES/qbe-$QBE_VERSAO.tar.xz" "$QBE_URL" \
            || erro "não consegui baixar $QBE_URL (a rede bloqueia c9x.me?)"
    fi
    tar -xJf "$FONTES/qbe-$QBE_VERSAO.tar.xz" -C "$TRABALHO"
    make -s -C "$TRABALHO/qbe-$QBE_VERSAO"
    make -s -C "$TRABALHO/qbe-$QBE_VERSAO" install DESTDIR="$COMPILADOR" PREFIX=/usr

    passo "Compilando o cproc"
    if [ ! -d "$FONTES/cproc" ]; then
        git clone -q "$CPROC_URL" "$FONTES/cproc" \
            || erro "não consegui baixar $CPROC_URL (a rede bloqueia git.sr.ht?)"
    fi
    git -C "$FONTES/cproc" checkout -q "$CPROC_COMMIT" \
        || { git -C "$FONTES/cproc" fetch -q origin && git -C "$FONTES/cproc" checkout -q "$CPROC_COMMIT"; } \
        || erro "não achei o commit $CPROC_COMMIT do cproc"
    rm -rf "$TRABALHO/cproc"
    cp -r "$FONTES/cproc" "$TRABALHO/cproc"
    (cd "$TRABALHO/cproc" && ./configure --prefix=/usr >/dev/null && make -s)
    make -s -C "$TRABALHO/cproc" install DESTDIR="$COMPILADOR"
    echo "cproc $(git -C "$FONTES/cproc" rev-parse --short HEAD)" > "$COMPILADOR/versoes.txt"
    echo "qbe $QBE_VERSAO" >> "$COMPILADOR/versoes.txt"
else
    passo "ATENÇÃO: montando SEM o compilador (--sem-compilador)"
fi

# --- 3. A raiz do sistema da ISO --------------------------------------------
passo "Montando a raiz do sistema"
mkdir -p "$RAIZ"/{usr/bin,usr/sbin,usr/lib,usr/lib64,usr/share,etc,proc,sys,dev,tmp,root,run}
# Mesmo formato do Debian/Ubuntu atuais: /bin, /lib... apontam para /usr.
for d in bin sbin lib lib64; do ln -s "usr/$d" "$RAIZ/$d"; done
chmod 1777 "$RAIZ/tmp"

# Copia um programa e as bibliotecas de que ele depende (lidas com ldd).
copiar_com_libs() {
    local arq="$1" destino="${2:-$1}"
    mkdir -p "$RAIZ$(dirname "$destino")"
    cp -L "$arq" "$RAIZ$destino"
    ldd "$arq" 2>/dev/null | grep -o '/[^ ]*' | while read -r lib; do
        [ -e "$RAIZ$lib" ] && continue
        mkdir -p "$RAIZ$(dirname "$lib")"
        cp -L "$lib" "$RAIZ$lib"
    done
}

# BusyBox: um binário que faz o papel de sh, ls, cp, vi, less, init...
cp "$(command -v busybox)" "$RAIZ/usr/bin/busybox"
for applet in $("$RAIZ/usr/bin/busybox" --list-full); do
    [ -e "$RAIZ/$applet" ] || ln -s /usr/bin/busybox "$RAIZ/$applet"
done

# As ferramentas do NEFS: montador, ligador e bibliotecas. O build.sh do
# NovaEra as usa direto (dentro da ISO não há compilador para o Linux).
for f in nefs-as nefs-ld nefs-ar; do
    gcc -O2 -o "$TRABALHO/$f" "$REPO/ferramentas/$f.c"
    copiar_com_libs "$TRABALHO/$f" "/usr/bin/$f"
done
# O as/ld/objcopy do binutils ficam como alternativa (FERRAMENTAS=sistema).
copiar_com_libs "$(readlink -f "$(command -v as)")" /usr/bin/as
copiar_com_libs "$(readlink -f "$(command -v ld)")" /usr/bin/ld
copiar_com_libs "$(readlink -f "$(command -v objcopy)")" /usr/bin/objcopy

# O compilador do NEFS
if [ "$SEM_COMPILADOR" = 0 ]; then
    for b in "$COMPILADOR"/usr/bin/*; do
        copiar_com_libs "$b" "/usr/bin/$(basename "$b")"
    done
    [ -d "$COMPILADOR/usr/lib" ] && cp -a "$COMPILADOR/usr/lib/." "$RAIZ/usr/lib/"
    mkdir -p "$RAIZ/usr/share/nefs"
    cp "$COMPILADOR/versoes.txt" "$RAIZ/usr/share/nefs/compilador.txt"
    # O pré-processador do cproc ainda não está completo: ele usa o cpp do
    # GCC (só o pré-processador, cpp + cc1; o compilador continua o cproc).
    copiar_com_libs "$(readlink -f "$(command -v cpp)")" /usr/bin/cpp
    cc1="$(gcc -print-prog-name=cc1)"
    copiar_com_libs "$cc1" "$cc1"
    cp -a "$(dirname "$(gcc -print-libgcc-file-name)")/include" \
          "$RAIZ$(dirname "$(gcc -print-libgcc-file-name)")/" 2>/dev/null || true
fi

# QEMU, para testar o sistema criado sem sair da ISO (tela no terminal)
copiar_com_libs "$(command -v qemu-system-x86_64)" /usr/bin/qemu-system-x86_64
for m in accel-tcg-x86_64.so ui-curses.so; do
    copiar_com_libs "/usr/lib/x86_64-linux-gnu/qemu/$m"
done
cp -rL /usr/share/qemu "$RAIZ/usr/share/qemu"
cp -rL /usr/share/seabios "$RAIZ/usr/share/seabios"   # BIOS e VGA BIOS do PC virtual
# Descrições de terminal (linux, vt100, xterm...) para a tela "curses".
cp -rL /usr/share/terminfo "$RAIZ/usr/share/terminfo"
# Conversões de texto do iconv da glibc: a tela curses (CP437 da VGA), o
# mtools (CP850) e o xorriso (UTF-16) precisam destes módulos.
cp -rL /usr/lib/x86_64-linux-gnu/gconv "$RAIZ/usr/lib/x86_64-linux-gnu/gconv"

# GRUB + xorriso + mtools, para o comando "nefs iso"
for b in grub-mkrescue grub-mkimage xorriso mformat mcopy; do
    copiar_com_libs "$(command -v "$b")" "/usr/bin/$b"
done
mkdir -p "$RAIZ/usr/lib/grub"
cp -r /usr/lib/grub/i386-pc /usr/lib/grub/x86_64-efi "$RAIZ/usr/lib/grub/"
mkdir -p "$RAIZ/usr/share/grub"
cp -L /usr/share/grub/unicode.pf2 "$RAIZ/usr/share/grub/" 2>/dev/null || true

# --- 4. O NEFS: o comando, o modelo NovaEra e o livro -----------------------
passo "Instalando o NEFS $NEFS_VERSAO"
install -m 755 "$REPO/nefs/nefs" "$RAIZ/usr/bin/nefs"
MODELO="$RAIZ/usr/share/nefs/novaera"
mkdir -p "$MODELO/usuario"
for f in kernel.c boot.s cpu.s novaera.h linker.ld build.sh sistema.conf \
         programas.lista logo.txt LICENSE README.md; do
    cp "$REPO/$f" "$MODELO/"
done
cp "$REPO"/usuario/*.c "$REPO"/usuario/*.h "$REPO"/usuario/*.s "$REPO"/usuario/*.ld \
   "$MODELO/usuario/"
mkdir -p "$MODELO/usuario/bin"
cp "$REPO"/usuario/bin/*.c "$MODELO/usuario/bin/"
cp -r "$REPO/libc" "$MODELO/libc"
cp -r "$REPO/ferramentas" "$MODELO/ferramentas"
# Os portes (o QBE que roda dentro do NovaEra), já com as fontes: a ISO
# não precisa de internet para compilá-los.
mkdir -p "$REPO/portes/fontes"
cp "$FONTES/qbe-$QBE_VERSAO.tar.xz" "$REPO/portes/fontes/" 2>/dev/null || true
cp -r "$REPO/portes" "$MODELO/portes"
# O NovaEra original já compilado, para comparar com o seu sistema:
#   qemu-system-x86_64 -kernel /usr/share/nefs/novaera/kernel.bin \
#       -initrd /usr/share/nefs/novaera/initrd.tar -display curses
(cd "$REPO" && sh build.sh >/dev/null) && cp "$REPO/kernel.bin" "$REPO/initrd.tar" "$MODELO/"
cp -r "$REPO/nefs/livro" "$RAIZ/usr/share/nefs/livro"

cp "$AQUI/raiz/init" "$RAIZ/init"
chmod 755 "$RAIZ/init"
cp -r "$AQUI/raiz/etc/." "$RAIZ/etc/"
sed -i "s/@NEFS_VERSAO@/$NEFS_VERSAO/" "$RAIZ/etc/motd"
if [ "$SEM_COMPILADOR" = 1 ]; then
    printf '\n  ATENCAO: ISO de teste montada SEM o compilador (cproc + QBE).\n\n' >> "$RAIZ/etc/motd"
fi

# --- 5. Kernel Linux --------------------------------------------------------
passo "Baixando o kernel Linux (pacote linux-image-generic do apt)"
KDIR="$TRABALHO/kernel"
mkdir -p "$KDIR"
KPKG="$(apt-cache depends linux-image-generic | sed -n 's/.*Depends: \(linux-image-[0-9].*\)/\1/p' | head -n 1)"
[ -n "$KPKG" ] || erro "não achei o pacote do kernel"
(cd "$KDIR" && apt-get download -qq "$KPKG" >/dev/null && dpkg-deb -x ./*.deb x)

# --- 6. A ISO ---------------------------------------------------------------
passo "Gerando o initramfs e a ISO"
mkdir -p "$CD/boot/grub"
cp "$KDIR"/x/boot/vmlinuz-* "$CD/boot/vmlinuz"
(cd "$RAIZ" && find . | cpio -o -H newc --quiet | gzip -6) > "$CD/boot/nefs.gz"
cat > "$CD/boot/grub/grub.cfg" <<EOF
set timeout=3
set default=0
menuentry "NEFS $NEFS_VERSAO - NovaEra From Scratch" {
    linux /boot/vmlinuz quiet console=tty0 console=ttyS0
    initrd /boot/nefs.gz
}
EOF

if [ "$SEM_COMPILADOR" = 1 ]; then
    ISO="$AQUI/nefs-$NEFS_VERSAO-x86_64-SEM-COMPILADOR.iso"
else
    ISO="$AQUI/nefs-$NEFS_VERSAO-x86_64.iso"
fi
grub-mkrescue -o "$ISO" "$CD" >/dev/null 2>&1 || erro "grub-mkrescue falhou"

passo "Pronto: $ISO ($(du -h "$ISO" | cut -f1))"
echo "Teste com:  qemu-system-x86_64 -cdrom $(basename "$ISO") -m 1G"
