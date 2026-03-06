#!/bin/bash
# Requirements for Arch Linux:
# sudo pacman -S gcc nasm

# Compile the assembly wrapper (syscalls and entry point)
nasm -f elf32 start.s -o start.o

# Compile the C program
gcc -m32 -ffreestanding -fno-stack-protector -fno-pie -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -O2 -Wall -Wextra -c hello.c -o hello.o

# Link everything together into a raw binary mapped to 0x2000000
ld -m elf_i386 -T bex.ld -nostdlib --oformat binary start.o hello.o -o hello.bex

echo "Successfully built hello.bex!"