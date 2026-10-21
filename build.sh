#!/bin/sh
# ===========================================================================
#  NovaEra Microkernel (x86_64) - build.sh
#
#   1. lê sistema.conf e programas.lista e gera build/sistema.h,
#      build/programas.h e build/programas.asm
#   2. compila os servidores do boot (usuario/<nome>.c + crt0.asm) como
#      programas ELF, que o núcleo embute
#   3. compila os programas de usuario/bin/ e junta todos no initrd.tar,
#      que o GRUB/QEMU entregam ao núcleo junto com o kernel.bin
#   4. compila o núcleo e liga o kernel.bin
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
ligar_programa() {
    saida="$1"; shift
    # shellcheck disable=SC2086
    "$LD" -m elf_x86_64 -nostdlib $LDFLAGS -T usuario/usuario.ld -o "$saida" build/crt0.o "$@"
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

case "$(basename "$CC")" in
    clang*)
        # -mno-red-zone: uma interrupção no anel 0 escreveria em cima da
        #  "zona vermelha" da pilha; -mgeneral-regs-only: o núcleo não salva
        #  os registradores SSE/FPU na troca de processo.
        CFLAGS="--target=x86_64-none-elf -ffreestanding -fno-builtin -fno-stack-protector
                -fno-pic -mno-red-zone -mgeneral-regs-only -O2 -Wall -Wextra" ;;
    *)
        # cproc + QBE: o QBE só usa SSE para números de ponto flutuante (não
        # usamos nenhum) e o núcleo nunca é interrompido no meio do código C
        # (as interrupções ficam desligadas no anel 0, menos em cpu_ocioso).
        CFLAGS="-std=c11" ;;
esac
CFLAGS="$CFLAGS -I. -Ibuild"

# --- 1. Arquivos gerados -----------------------------------------------------

# Texto UTF-8 -> literal C em CP437 (a tabela de caracteres da VGA).
cp437() {
    printf '%s' "$1" | sed -e 's/"/\\"/g' \
        -e 's/ç/\\207/g; s/Ç/\\200/g; s/á/\\240/g; s/é/\\202/g; s/í/\\241/g' \
        -e 's/ó/\\242/g; s/ú/\\243/g; s/â/\\203/g; s/ê/\\210/g; s/ô/\\223/g' \
        -e 's/à/\\205/g; s/ü/\\201/g; s/É/\\220/g; s/ã/a/g; s/õ/o/g; s/Ã/A/g; s/Õ/O/g' \
        -e 's/Á/A/g; s/Í/I/g; s/Ó/O/g; s/Ú/U/g; s/Â/A/g; s/Ê/E/g; s/Ô/O/g'
}

NOME="NovaEra"; VERSAO="?"; LEMA=""; PROMPT="NovaEra"
# shellcheck disable=SC1091
. ./sistema.conf

cat > build/sistema.h <<EOF
/* Gerado por build.sh a partir de sistema.conf. Não edite. */
#define SISTEMA_NOME   "$(cp437 "$NOME")"
#define SISTEMA_VERSAO "$(cp437 "$VERSAO")"
#define SISTEMA_LEMA   "$(cp437 "$LEMA")"
#define SISTEMA_PROMPT "$(cp437 "$PROMPT")"
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
for prog in $PROGRAMAS; do
    # shellcheck disable=SC2086
    "$CC" $CFLAGS -c "usuario/$prog.c" -o "build/$prog.o"
    ligar_programa "build/$prog.elf" "build/$prog.o"
    echo "  usuario/$prog.c -> build/$prog.elf ($(wc -c < "build/$prog.elf") bytes)"
done

# --- 3. Programas do initrd (usuario/bin/*.c -> bin/<nome>) -----------------

rm -rf build/initrd
mkdir -p build/initrd/bin
for fonte in usuario/bin/*.c; do
    [ -f "$fonte" ] || continue
    nome="$(basename "$fonte" .c)"
    # shellcheck disable=SC2086
    "$CC" $CFLAGS -Iusuario -c "$fonte" -o "build/bin-$nome.o"
    ligar_programa "build/initrd/bin/$nome" "build/bin-$nome.o"
done
# Formato ustar: o que o núcleo sabe ler. O tar do BusyBox já usa ustar e
# não conhece a opção --format.
if ! tar --format=ustar -C build/initrd -cf build/initrd.tar bin 2>/dev/null; then
    tar -C build/initrd -cf build/initrd.tar bin
fi
cp build/initrd.tar initrd.tar
echo "  usuario/bin/*.c -> initrd.tar ($(ls build/initrd/bin | wc -l) programas, $(wc -c < initrd.tar) bytes)"

# --- 4. Núcleo ---------------------------------------------------------------

nasm -f elf64 boot.asm -o build/boot.o
nasm -f elf64 cpu.asm -o build/cpu.o
nasm -f elf64 build/programas.asm -o build/programas.o
# shellcheck disable=SC2086
"$CC" $CFLAGS -c kernel.c -o build/kernel.o
# Binário puro: o cabeçalho Multiboot (boot.asm) diz onde carregar.
ligar linker.ld build/kernel.bin build/boot.o build/cpu.o build/kernel.o build/programas.o
cp build/kernel.bin kernel.bin

echo "OK: kernel.bin gerado ($(wc -c < kernel.bin) bytes) com $CC"
