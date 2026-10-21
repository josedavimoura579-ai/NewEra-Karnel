#!/bin/sh
# ===========================================================================
#  NovaEra Microkernel (x86_64) - build.sh
#
#   0. compila as ferramentas do NEFS (nefs-as, nefs-ld, nefs-ar) para o
#      computador onde o build roda
#   1. lê sistema.conf e programas.lista e gera build/sistema.h,
#      build/programas.h e build/programas.s
#   2. compila os servidores do boot (usuario/<nome>.c + crt0.s) como
#      programas ELF, que o núcleo embute
#   3. compila a libc do NovaEra (libc/), os programas de usuario/bin/ e as
#      ferramentas do NEFS, ligados com ela, e junta tudo no initrd.tar, que
#      o GRUB/QEMU entregam ao núcleo junto com o kernel.bin
#   4. compila os portes (portes/*/receita, como o QBE) com a mesma libc
#      e os põe no initrd, se as fontes já foram baixadas
#   5. compila o núcleo e liga o kernel.bin
#
#  Compiladores aceitos (variável CC):
#    CC=clang  (padrão)                    -> Linux, Termux
#    CC=cproc  (cproc + QBE, o do NEFS)    -> a ISO do NEFS
#  Montador e ligador (variável FERRAMENTAS):
#    FERRAMENTAS=nefs     (padrão) nefs-as, nefs-ld e nefs-ar, deste repositório
#    FERRAMENTAS=sistema  o as/ld/ar do binutils (ou o ld.lld do LLVM)
#  Dependências: o CC e um compilador C para as ferramentas (HOSTCC: cc,
#  gcc ou clang) ou elas já instaladas (como na ISO do NEFS). Com
#  CC=cproc, cada .c vira assembly (cproc -S) e o nefs-as o monta: o
#  caminho todo é o do NEFS.
#  Escrito em sh puro (sem bash): roda também no BusyBox da ISO do NEFS.
# ===========================================================================
set -eu
cd "$(dirname "$0")"
mkdir -p build

CC="${CC:-clang}"
FERRAMENTAS="${FERRAMENTAS:-nefs}"
case "$(basename "$CC")" in cproc*) CPROC=1 ;; *) CPROC=0 ;; esac

# --- 0. As ferramentas do NEFS -----------------------------------------------

if [ "$FERRAMENTAS" = "nefs" ]; then
    # Compiladas aqui com o compilador do computador (HOSTCC). Sem ele (na
    # ISO do NEFS), usa as que já estão instaladas.
    if [ -z "${HOSTCC:-}" ]; then
        for c in cc gcc clang; do
            if command -v "$c" >/dev/null; then HOSTCC="$c"; break; fi
        done
    fi
    if [ -n "${HOSTCC:-}" ]; then
        mkdir -p build/ferramentas
        for f in nefs-as nefs-ld nefs-ar; do
            "$HOSTCC" -O2 -o "build/ferramentas/$f" "ferramentas/$f.c"
        done
        NEFS_AS="build/ferramentas/nefs-as"
        NEFS_LD="build/ferramentas/nefs-ld"
        AR="build/ferramentas/nefs-ar"
        echo "  ferramentas/*.c -> nefs-as, nefs-ld, nefs-ar (com $HOSTCC)"
    elif command -v nefs-as >/dev/null && command -v nefs-ld >/dev/null && command -v nefs-ar >/dev/null; then
        NEFS_AS="nefs-as"; NEFS_LD="nefs-ld"; AR="nefs-ar"
    else
        echo "ERRO: falta um compilador C para as ferramentas (HOSTCC) ou o nefs-as/nefs-ld instalados"
        exit 1
    fi
else
    if [ -z "${LD:-}" ]; then
        if command -v ld.lld >/dev/null; then LD=ld.lld; else LD=ld; fi
    fi
    if [ -z "${OBJCOPY:-}" ]; then
        if command -v objcopy >/dev/null; then OBJCOPY=objcopy; else OBJCOPY=llvm-objcopy; fi
    fi
    if [ -z "${AR:-}" ]; then
        if command -v ar >/dev/null; then AR=ar; else AR=llvm-ar; fi
    fi
    # Os programas do NovaEra não têm cabeçalhos de segmento com permissões
    # separadas (o núcleo mapeia tudo); o aviso "RWX" do ld do binutils é esperado.
    LDFLAGS=""
    if "$LD" --help 2>/dev/null | grep -q -- --no-warn-rwx-segments; then
        LDFLAGS="--no-warn-rwx-segments"
    fi
fi

# Monta um arquivo .s (sintaxe AT&T).
montar() {
    if [ "$FERRAMENTAS" = "nefs" ]; then "$NEFS_AS" -o "$1" "$2"
    elif [ "$CPROC" = 1 ]; then as --64 -o "$1" "$2"
    else "$CC" --target=x86_64-none-elf -c -o "$1" "$2"
    fi
}

# Compila um .c: compilar saída.o fonte.c flags...
# Com o cproc e as ferramentas do NEFS, passa pelo assembly: cproc -S e nefs-as.
compilar() {
    saida="$1"; fonte="$2"; shift 2
    if [ "$CPROC" = 1 ] && [ "$FERRAMENTAS" = "nefs" ]; then
        "$CC" "$@" -S -o "$saida.s" "$fonte"
        "$NEFS_AS" -o "$saida" "$saida.s"
    else
        "$CC" "$@" -c -o "$saida" "$fonte"
    fi
}

# Liga um programa do usuário (servidor ou comando): um ELF que mora a
# partir de 0x40000000, que o núcleo sabe carregar.
ligar_programa() {
    saida="$1"; shift
    if [ "$FERRAMENTAS" = "nefs" ]; then
        "$NEFS_LD" -o "$saida" "$@"
    else
        # shellcheck disable=SC2086
        "$LD" -m elf_x86_64 -nostdlib $LDFLAGS -T usuario/usuario.ld -o "$saida" "$@"
    fi
}

# Liga o núcleo: memória pura a partir de 1 MiB, com o cabeçalho Multiboot
# no começo. Por que não "ld --oformat binary" direto? Porque assim o ld do
# binutils não resolve os acessos pela GOT que o QBE gera; por isso o ELF
# e depois o objcopy.
ligar_nucleo() {
    saida="$1"; shift
    if [ "$FERRAMENTAS" = "nefs" ]; then
        "$NEFS_LD" -binario -base 0x100000 -inicio .multiboot -o "$saida" "$@"
    else
        # shellcheck disable=SC2086
        "$LD" -m elf_x86_64 -nostdlib $LDFLAGS -T linker.ld -o "$saida.elf" "$@"
        "$OBJCOPY" -O binary "$saida.elf" "$saida"
    fi
}

# CFLAGS_NUCLEO: o núcleo. CFLAGS: os programas do usuário, que podem usar
# ponto flutuante (o núcleo salva a FPU/SSE de cada processo) e só enxergam
# os cabeçalhos da libc do NovaEra (-nostdinc), nunca os do Linux.
case "$(basename "$CC")" in
    cproc*)
        # cproc + QBE: o QBE só usa SSE para números de ponto flutuante (o
        # núcleo não tem nenhum) e o núcleo nunca é interrompido no meio do
        # código C (as interrupções ficam desligadas no anel 0, menos em
        # cpu_ocioso), então a "zona vermelha" não é problema.
        CFLAGS_NUCLEO="-std=c11"
        CFLAGS="-std=c11"
        SEM_AVISOS="" ;;
    *)
        COMUNS="--target=x86_64-none-elf -ffreestanding -fno-builtin -fno-stack-protector
                -fno-pic -O2 -Wall -Wextra"
        # -mno-red-zone: uma interrupção no anel 0 escreveria em cima da
        #  "zona vermelha" da pilha; -mgeneral-regs-only: o núcleo não usa SSE.
        CFLAGS_NUCLEO="$COMUNS -mno-red-zone -mgeneral-regs-only"
        CFLAGS="$COMUNS"
        SEM_AVISOS="-w" ;;
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
    echo "# Gerado por build.sh a partir de programas.lista. Não edite."
    echo ".section .rodata"
} > build/programas.s
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
    cat >> build/programas.s <<EOF
.globl prog_${nome}_ini
.globl prog_${nome}_fim
.balign 8
prog_${nome}_ini:
    .incbin "build/$nome.elf"
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
echo '.section .note.GNU-stack,"",@progbits' >> build/programas.s

# --- 2. Servidores do boot ---------------------------------------------------

montar build/crt0.o usuario/crt0.s
montar build/sys.o usuario/sys.s
for prog in $PROGRAMAS; do
    # shellcheck disable=SC2086
    compilar "build/$prog.o" "usuario/$prog.c" $CFLAGS
    ligar_programa "build/$prog.elf" build/crt0.o build/sys.o "build/$prog.o"
    echo "  usuario/$prog.c -> build/$prog.elf ($(wc -c < "build/$prog.elf") bytes)"
done

# --- 3. A libc e os programas do initrd (usuario/bin/*.c -> bin/<nome>) -----

# A libc vira uma biblioteca (libc.a): cada programa leva só as partes que
# usa. O crt0 e as chamadas de sistema (sys.o) também vão nela, para o
# nefs-ld dentro do NovaEra ligar um programa só com "-lc".
rm -rf build/libc
mkdir -p build/libc
montar build/libc/crt0.o libc/crt0.s
LIBC_OBJS="build/libc/crt0.o build/sys.o"
for fonte in libc/src/*.c; do
    obj="build/libc/$(basename "$fonte" .c).o"
    # shellcheck disable=SC2086
    compilar "$obj" "$fonte" $CFLAGS
    LIBC_OBJS="$LIBC_OBJS $obj"
done
# shellcheck disable=SC2086
"$AR" rcs build/libc/libc.a $LIBC_OBJS
LIBC="build/libc/libc.a"
echo "  libc/src/*.c -> libc.a ($(echo $LIBC_OBJS | wc -w) partes)"

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
# Os comandos (usuario/bin/) e as ferramentas do NEFS (ferramentas/), que
# também rodam dentro do NovaEra.
for fonte in usuario/bin/*.c ferramentas/*.c; do
    [ -f "$fonte" ] || continue
    nome="$(basename "$fonte" .c)"
    # shellcheck disable=SC2086
    compilar "build/bin-$nome.o" "$fonte" $CFLAGS
    ligar_programa "build/initrd/bin/$nome" build/libc/crt0.o "build/bin-$nome.o" "$LIBC"
done
# Para ligar programas dentro do NovaEra: nefs-ld -o prog prog.o -lc
mkdir -p build/initrd/usr/lib/nefs
cp build/libc/libc.a build/initrd/usr/lib/nefs/libc.a

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
            compilar "$obj" "build/portes/$PASTA/$fonte" $CFLAGS $SEM_AVISOS
            objs="$objs $obj"
        done
        # shellcheck disable=SC2086
        ligar_programa "build/initrd/bin/$NOME" build/libc/crt0.o $objs "$LIBC"
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

montar build/boot.o boot.s
montar build/cpu.o cpu.s
montar build/programas.o build/programas.s
# shellcheck disable=SC2086
compilar build/kernel.o kernel.c $CFLAGS_NUCLEO
# Binário puro: o cabeçalho Multiboot (boot.s) diz onde carregar.
ligar_nucleo build/kernel.bin build/boot.o build/cpu.o build/kernel.o build/programas.o
cp build/kernel.bin kernel.bin

echo "OK: kernel.bin gerado ($(wc -c < kernel.bin) bytes) com $CC e as ferramentas '$FERRAMENTAS'"
