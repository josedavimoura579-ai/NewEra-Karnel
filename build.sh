#!/usr/bin/env bash
# NovaEra Microkernel - build.sh
#   1. compila cada programa do usuário (usuario/*.c) como binário puro
#   2. monta boot.asm, que embute esses binários no kernel.bin
#   3. compila o núcleo e liga tudo
# Dependências: nasm, clang, lld   (Termux: pkg install nasm clang lld)
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

CFLAGS=(--target=i386-none-elf -ffreestanding -fno-builtin -fno-stack-protector
        -fno-pic -mno-sse -mno-mmx -O2 -Wall -Wextra -I.)

for prog in video teclado shell; do
    clang "${CFLAGS[@]}" -c "usuario/$prog.c" -o "build/$prog.o"
    ld.lld -m elf_i386 -nostdlib -T usuario/usuario.ld --oformat binary \
           -o "build/$prog.bin" "build/$prog.o"
    echo "  usuario/$prog.c -> build/$prog.bin ($(wc -c < "build/$prog.bin") bytes)"
done

nasm -f elf32 boot.asm -o build/boot.o
clang "${CFLAGS[@]}" -c kernel.c -o build/kernel.o
ld.lld -m elf_i386 -nostdlib -T linker.ld -o kernel.bin build/boot.o build/kernel.o

echo "OK: kernel.bin gerado ($(wc -c < kernel.bin) bytes)"
