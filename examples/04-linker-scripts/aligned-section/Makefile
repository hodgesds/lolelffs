.PHONY: all clean

PROJECT_ROOT=../../..
LOLELFFS=$(PROJECT_ROOT)/lolelffs

all: example

fs.img:
	$(LOLELFFS) mkfs --size 1M fs.img
	$(LOLELFFS) write -i fs.img /aligned.txt --create -d "Page-aligned filesystem!"

fs.o: fs.img
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
	        --rename-section .data=.lolfs.super,alloc,load,readonly,data \
	        fs.img fs.o

example.o: example.c
	gcc -c example.c -o example.o

example: example.o fs.o aligned.ld
	gcc -o example example.o fs.o -T aligned.ld

clean:
	rm -f example example.o fs.o fs.img
