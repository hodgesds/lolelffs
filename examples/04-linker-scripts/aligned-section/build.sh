#!/bin/bash
set -e

PROJECT_ROOT="../../.."
LOLELFFS="$PROJECT_ROOT/lolelffs"

echo "Building page-aligned linker script example..."

# Create filesystem
"$LOLELFFS" mkfs --size 1M fs.img
"$LOLELFFS" write -i fs.img /aligned.txt --create -d "Page-aligned filesystem!"

# Convert to object file
objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
        --rename-section .data=.lolfs.super,alloc,load,readonly,data \
        fs.img fs.o

# Compile and link
gcc -c example.c -o example.o
gcc -o example example.o fs.o -T aligned.ld

echo "✓ Build complete"
./example
