.PHONY: all clean test

PROJECT_ROOT=../../..
LOLELFFS=$(PROJECT_ROOT)/lolelffs

all: example

example-base: example.c
	gcc -o example-base example.c

fs.img:
	$(LOLELFFS) mkfs --size 1M fs.img
	$(LOLELFFS) write -i fs.img /info.txt --create -d "This filesystem is embedded in an ELF binary!\n\nThe .lolfs.super section contains the complete filesystem.\nYou can access it using the lolelffs CLI tools."
	$(LOLELFFS) mkdir -i fs.img /data
	$(LOLELFFS) write -i fs.img /data/test.txt --create -d "Test file in /data/"

example: example-base fs.img
	objcopy --add-section .lolfs.super=fs.img \
	        --set-section-flags .lolfs.super=alloc,load,readonly,data \
	        example-base example
	chmod +x example
	@echo ""
	@echo "Build complete: example"
	@readelf -S example | grep ".lolfs.super" || true

test: example
	@echo "Running example..."
	@./example
	@echo ""
	@echo "Testing CLI access..."
	@$(LOLELFFS) ls -i example /
	@echo ""
	@echo "Tests passed!"

clean:
	rm -f example example-base fs.img
