#!/bin/sh
# ===========================================================================
#  NovaEra Microkernel (x86_64) - build.sh
#
#   1. lê sistema.conf e programas.lista e gera build/sistema.h,
#      build/programas.h e build/programas.asm
#   2. compila os servidores do boot (usuario/<nome>.c + crt0.asm) como
#      programas ELF, que o núcleo embute
#   3. compila a libc do NovaEra (libc/) e os programas de usuario/bin/,
#      ligados com ela, e junta tudo no initrd.tar, que o GRUB/QEMU
#      entregam ao núcleo junto com o kernel.bin
#   4. compila os portes (portes/*/receita, como o QBE) com a mesma libc
#      e os põe no initrd, se as fontes já foram baixadas
#   5. compila o núcleo e liga o kernel.bin
#
#  Compiladores aceitos (variável CC):
#    CC=clang  (padrão)                    -> Linux, Termux
#    CC=cproc  (cproc + QBE, o do NEFS)    -> a ISO do NEFS
#  Dependências: nasm, um ligador (ld.lld ou o ld do binutils), objcopy
#  (do binutils ou llvm-objcopy) e o CC.
#  Escrito em sh puro (sem bash): roda também no BusyBox da ISO do NEFS.
# ===========================================================================
set -eu
cd "$(dirname "$0")"
mkdir -p build

CC="${CC:-clang}"
if [ -z "${LD:-}" ]; then
    if command -v ld.lld >/dev/null; then LD=ld.lld; else LD=ld; fi
fi
if [ -z "${OBJCOPY:-}" ]; then
    if command -v objcopy >/dev/null; then OBJCOPY=objcopy; else OBJCOPY=llvm-objcopy; fi
fi

# Os programas do NovaEra não têm cabeçalhos de segmento com permissões
# separadas (o núcleo mapeia tudo); o aviso "RWX" do ld do binutils é esperado.
LDFLAGS=""
if "$LD" --help 2>/dev/null | grep -q -- --no-warn-rwx-segments; then
    LDFLAGS="--no-warn-rwx-segments"
fi

# Liga um programa do usuário: um ELF, que o núcleo sabe carregar.
# Liga um servidor (só a ulib) ou um programa da libc: um ELF, que o
# núcleo sabe carregar. O 1º argumento é o começo do programa (o _start).
ligar_programa() {
    saida="$1"; shift
    # shellcheck disable=SC2086
    "$LD" -m elf_x86_64 -nostdlib $LDFLAGS -T usuario/usuario.ld -o "$saida" "$@"
}

# Liga um ELF e o converte em binário puro (o kernel.bin). Por que não
# "ld --oformat binary" direto? Porque assim o ld do binutils não resolve
# os acessos pela GOT que o QBE gera, e o código acabaria lendo zeros.
ligar() {
    script="$1"; saida="$2"; shift 2
    # shellcheck disable=SC2086
    "$LD" -m elf_x86_64 -nostdlib $LDFLAGS -T "$script" -o "$saida.elf" "$@"
    "$OBJCOPY" -O binary "$saida.elf" "$saida"
}

# CFLAGS_NUCLEO: o núcleo. CFLAGS: os programas do usuário, que podem usar
# ponto flutuante (o núcleo salva a FPU/SSE de cada processo) e só enxergam
# os cabeçalhos da libc do NovaEra (-nostdinc), nunca os do Linux.
case "$(basename "$CC")" in
    clang*)
        COMUNS="--target=x86_64-none-elf -ffreestanding -fno-builtin -fno-stack-protector
                -fno-pic -O2 -Wall -Wextra"
        # -mno-red-zone: uma interrupção no anel 0 escreveria em cima da
        #  "zona vermelha" da pilha; -mgeneral-regs-only: o núcleo não usa SSE.
        CFLAGS_NUCLEO="$COMUNS -mno-red-zone -mgeneral-regs-only"
        CFLAGS="$COMUNS"
        SEM_AVISOS="-w" ;;
    *)
        # cproc + QBE: o QBE só usa SSE para números de ponto flutuante (o
        # núcleo não tem nenhum) e o núcleo nunca é interrompido no meio do
        # código C (as interrupções ficam desligadas no anel 0, menos em
        # cpu_ocioso), então a "zona vermelha" não é problema.
        CFLAGS_NUCLEO="-std=c11"
        CFLAGS="-std=c11"
        SEM_AVISOS="" ;;
esac
CFLAGS_NUCLEO="$CFLAGS_NUCLEO -I. -Ibuild"
CFLAGS="$CFLAGS -nostdinc -Ilibc/include -I. -Ibuild -Iusuario"

# --- 1. Arquivos gerados -----------------------------------------------------

# Texto UTF-8 -> literal C em CP437 (a tabela de caracteres da VGA).
cp437() {
    printf '%s' "$1" | sed -e 's/"/\\"/g' \
        -e 's/ç/\\207/g; s/Ç/\\200/g; s/á/\\240/g; s/é/\\202/g; s/í/\\241/g' \
        -e 's/ó/\\242/g; s/ú/\\243/g; s/â/\\203/g; s/ê/\\210/g; s/ô/\\223/g' \
        -e 's/à/\\205/g; s/ü/\\201/g; s/É/\\220/g; s/ã/a/g; s/õ/o/g; s/Ã/A/g; s/Õ/O/g' \
        -e 's/Á/A/g; s/Í/I/g; s/Ó/O/g; s/Ú/U/g; s/Â/A/g; s/Ê/E/g; s/Ô/O/g'
}

NOME="NovaEra"; VERSAO="?"; LEMA=""; PROMPT="NovaEra"; PURO="nao"
# shellcheck disable=SC1091
. ./sistema.conf

cat > build/sistema.h <<EOF
/* Gerado por build.sh a partir de sistema.conf. Não edite. */
#define SISTEMA_NOME   "$(cp437 "$NOME")"
#define SISTEMA_VERSAO "$(cp437 "$VERSAO")"
#define SISTEMA_LEMA   "$(cp437 "$LEMA")"
#define SISTEMA_PROMPT "$(cp437 "$PROMPT")"
#define SISTEMA_PURO   $([ "$PURO" = "sim" ] && echo 1 || echo 0)
EOF

PROGRAMAS=""
NUM_PROGRAMAS=0
{
    echo "; Gerado por build.sh a partir de programas.lista. Não edite."
    echo "section .rodata"
} > build/programas.asm
{
    echo "/* Gerado por build.sh a partir de programas.lista. Não edite. */"
} > build/programas.h
TABELA=""

while read -r nome permissoes; do
    case "$nome" in ''|'#'*) continue ;; esac
    [ -f "usuario/$nome.c" ] || { echo "ERRO: programas.lista cita '$nome', mas usuario/$nome.c não existe"; exit 1; }
    flags="0"
    for p in $permissoes; do
        case "$p" in
            driver) flags="$flags | T_DRIVER" ;;
            vga)    flags="$flags | T_VGA" ;;
            initrd) flags="$flags | T_INITRD" ;;
            -)      ;;
            *)      echo "ERRO: permissão desconhecida '$p' para '$nome'"; exit 1 ;;
        esac
    done
    PROGRAMAS="$PROGRAMAS $nome"
    NUM_PROGRAMAS=$((NUM_PROGRAMAS + 1))
    cat >> build/programas.asm <<EOF
global prog_${nome}_ini
global prog_${nome}_fim
align 8
prog_${nome}_ini:
    incbin "build/$nome.elf"
prog_${nome}_fim:
EOF
    echo "extern const u8 prog_${nome}_ini[], prog_${nome}_fim[];" >> build/programas.h
    TABELA="$TABELA    { \"$nome\", prog_${nome}_ini, prog_${nome}_fim, $flags },
"
done < programas.lista

{
    echo "#define NUM_PROGRAMAS $NUM_PROGRAMAS"
    echo "static const programa_info_t PROGRAMAS[NUM_PROGRAMAS] = {"
    printf '%s' "$TABELA"
    echo "};"
} >> build/programas.h
echo "section .note.GNU-stack noalloc noexec nowrite progbits" >> build/programas.asm

# --- 2. Servidores do boot ---------------------------------------------------

nasm -f elf64 usuario/crt0.asm -o build/crt0.o
nasm -f elf64 usuario/sys.asm -o build/sys.o
for prog in $PROGRAMAS; do
    # shellcheck disable=SC2086
    "$CC" $CFLAGS -c "usuario/$prog.c" -o "build/$prog.o"
    ligar_programa "build/$prog.elf" build/crt0.o build/sys.o "build/$prog.o"
    echo "  usuario/$prog.c -> build/$prog.elf ($(wc -c < "build/$prog.elf") bytes)"
done

# --- 3. A libc e os programas do initrd (usuario/bin/*.c -> bin/<nome>) -----

mkdir -p build/libc
nasm -f elf64 libc/crt0.asm -o build/libc/crt0.o
LIBC=""
for fonte in libc/src/*.c; do
    obj="build/libc/$(basename "$fonte" .c).o"
    # shellcheck disable=SC2086
    "$CC" $CFLAGS -c "$fonte" -o "$obj"
    LIBC="$LIBC $obj"
done
echo "  libc/src/*.c -> libc ($(echo $LIBC | wc -w) partes)"

rm -rf build/initrd
mkdir -p build/initrd/bin build/initrd/etc

# /etc/sistema: a identidade do sistema (o "os-release"), lida pelo novafetch.
{
    echo "NOME=$NOME"
    echo "VERSAO=$VERSAO"
    echo "LEMA=$LEMA"
    echo "PURO=$PURO"
} > build/initrd/etc/sistema
[ -f logo.txt ] && cp logo.txt build/initrd/etc/logo.txt
for fonte in usuario/bin/*.c; do
    [ -f "$fonte" ] || continue
    nome="$(basename "$fonte" .c)"
    # shellcheck disable=SC2086
    "$CC" $CFLAGS -c "$fonte" -o "build/bin-$nome.o"
    # shellcheck disable=SC2086
    ligar_programa "build/initrd/bin/$nome" build/libc/crt0.o build/sys.o "build/bin-$nome.o" $LIBC
done

# --- 4. Os portes: programas de fora (portes/<nome>/receita) ----------------
# Cada porte é compilado do código original, sem mudanças, com a libc do
# NovaEra. Só entra se as fontes já estiverem em portes/fontes/ (veja
# portes/baixar.sh); sem elas, o sistema é construído sem o porte.
for receita in portes/*/receita; do
    [ -f "$receita" ] || continue
    dir="$(dirname "$receita")"
    (
        # shellcheck disable=SC1090
        . "./$receita"
        arquivo="portes/fontes/$(basename "$URL")"
        if [ ! -f "$arquivo" ]; then
            echo "  portes: $NOME $VERSAO sem fontes (rode portes/baixar.sh), fica de fora"
            exit 0
        fi
        rm -rf "build/portes/$PASTA"
        mkdir -p build/portes
        case "$arquivo" in
            *.xz) xz -dc "$arquivo" | tar -C build/portes -xf - ;;
            *.gz) gzip -dc "$arquivo" | tar -C build/portes -xf - ;;
            *)    tar -C build/portes -xf "$arquivo" ;;
        esac
        # Arquivos do NovaEra que o porte precisa (como o config.h do QBE).
        for extra in "$dir"/*; do
            case "$extra" in */receita|*/exemplos) continue ;; esac
            cp "$extra" "build/portes/$PASTA/"
        done
        objs=""
        for fonte in $FONTES; do
            obj="build/portes/$PASTA/$(echo "$fonte" | tr / _).o"
            # Sem avisos: o código é de outro projeto; eles são assunto de lá.
            # shellcheck disable=SC2086
            "$CC" $CFLAGS $SEM_AVISOS -c "build/portes/$PASTA/$fonte" -o "$obj"
            objs="$objs $obj"
        done
        # shellcheck disable=SC2086
        ligar_programa "build/initrd/bin/$NOME" build/libc/crt0.o build/sys.o $objs $LIBC
        if [ -n "${EXEMPLOS:-}" ] && [ -d "$dir/exemplos" ]; then
            mkdir -p "build/initrd$EXEMPLOS"
            cp "$dir"/exemplos/* "build/initrd$EXEMPLOS/"
        fi
        echo "  portes: $NOME $VERSAO -> bin/$NOME ($(wc -c < "build/initrd/bin/$NOME") bytes)"
    )
done

# Formato ustar: o que o núcleo sabe ler. Data, dono e ordem fixos: o mesmo
# código gera sempre o mesmo initrd.tar. O tar do BusyBox já usa ustar e
# não conhece essas opções.
if ! tar --format=ustar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
         -C build/initrd -cf build/initrd.tar $(ls build/initrd) 2>/dev/null; then
    tar -C build/initrd -cf build/initrd.tar $(ls build/initrd)
fi
cp build/initrd.tar initrd.tar
echo "  usuario/bin/*.c + etc/ -> initrd.tar ($(ls build/initrd/bin | wc -l) programas, $(wc -c < initrd.tar) bytes)"

# --- 5. Núcleo ---------------------------------------------------------------

nasm -f elf64 boot.asm -o build/boot.o
nasm -f elf64 cpu.asm -o build/cpu.o
nasm -f elf64 build/programas.asm -o build/programas.o
# shellcheck disable=SC2086
"$CC" $CFLAGS_NUCLEO -c kernel.c -o build/kernel.o
# Binário puro: o cabeçalho Multiboot (boot.asm) diz onde carregar.
ligar linker.ld build/kernel.bin build/boot.o build/cpu.o build/kernel.o build/programas.o
cp build/kernel.bin kernel.bin

echo "OK: kernel.bin gerado ($(wc -c < kernel.bin) bytes) com $CC"
