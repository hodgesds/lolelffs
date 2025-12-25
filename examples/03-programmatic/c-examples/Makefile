.PHONY: all clean test

PROJECT_ROOT=../../..
LOLELFFS=$(PROJECT_ROOT)/lolelffs

CFLAGS=-Wall -Wextra -std=c99
PROGRAMS=read-superblock

all: $(PROGRAMS)

read-superblock: read-superblock.c
	gcc $(CFLAGS) -o read-superblock read-superblock.c

test: all
	@echo "Creating test filesystem..."
	@$(LOLELFFS) mkfs --size 10M test.img
	@$(LOLELFFS) mkdir -i test.img /test
	@$(LOLELFFS) write -i test.img /test/file.txt --create -d "Test file"
	@echo ""
	@echo "Running read-superblock..."
	@./read-superblock test.img
	@echo ""
	@echo "Tests passed!"
	@rm -f test.img

clean:
	rm -f $(PROGRAMS) *.o test.img
