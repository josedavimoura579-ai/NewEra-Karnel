#!/usr/bin/env bash
# NovaEra Kernel - build.sh (reconstruído): boot.asm + kernel.c -> kernel.bin
# Dependências: nasm, clang, lld   (Termux: pkg install nasm clang lld)
set -euo pipefail
cd "$(dirname "$0")"

nasm -f elf32 boot.asm -o boot.o
clang --target=i386-none-elf -ffreestanding -fno-builtin -fno-stack-protector \
      -fno-pic -O2 -Wall -Wextra -c kernel.c -o kernel.o
ld.lld -m elf_i386 -nostdlib -T linker.ld -o kernel.bin boot.o kernel.o

echo "OK: kernel.bin gerado ($(wc -c < kernel.bin) bytes)"
