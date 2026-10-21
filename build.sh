#!/usr/bin/env bash
# NovaEra Microkernel (x86_64) - build.sh
#   1. compila cada programa do usuário (usuario/*.c) como binário puro
#   2. monta boot.asm, que embute esses binários no kernel.bin
#   3. compila o núcleo e liga tudo
# Dependências: nasm, clang, lld   (Termux: pkg install nasm clang lld)
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

# -mno-red-zone: uma interrupção no anel 0 escreveria em cima da "zona
#  vermelha" da pilha; -mgeneral-regs-only: o núcleo não salva SSE/FPU.
CFLAGS=(--target=x86_64-none-elf -ffreestanding -fno-builtin -fno-stack-protector
        -fno-pic -mno-red-zone -mgeneral-regs-only -O2 -Wall -Wextra -I.)

for prog in video teclado shell; do
    clang "${CFLAGS[@]}" -c "usuario/$prog.c" -o "build/$prog.o"
    ld.lld -m elf_x86_64 -nostdlib -T usuario/usuario.ld --oformat binary \
           -o "build/$prog.bin" "build/$prog.o"
    echo "  usuario/$prog.c -> build/$prog.bin ($(wc -c < "build/$prog.bin") bytes)"
done

nasm -f elf64 boot.asm -o build/boot.o
clang "${CFLAGS[@]}" -c kernel.c -o build/kernel.o
# Binário puro: o cabeçalho Multiboot (boot.asm) diz onde carregar.
ld.lld -m elf_x86_64 -nostdlib -T linker.ld --oformat binary \
       -o kernel.bin build/boot.o build/kernel.o

echo "OK: kernel.bin gerado ($(wc -c < kernel.bin) bytes)"
