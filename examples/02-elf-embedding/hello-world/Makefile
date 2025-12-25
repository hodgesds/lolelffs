.PHONY: all clean test

PROJECT_ROOT=../../..
LOLELFFS=$(PROJECT_ROOT)/lolelffs

all: hello-with-fs

hello: hello.c
	gcc -o hello hello.c

fs.img: hello
	$(LOLELFFS) mkfs --size 2M fs.img
	$(LOLELFFS) mkdir -i fs.img /data
	$(LOLELFFS) write -i fs.img /config.txt --create -d "# Application Configuration\napp_name=hello-embedded\nversion=1.0\nembedded=true\nfilesystem=lolelffs"
	$(LOLELFFS) write -i fs.img /message.txt --create -d "Welcome to the embedded filesystem!\n\nThis file is stored inside an ELF executable binary.\nThe same binary that can run as a program can also be mounted as a filesystem.\n\nThis is the magic of lolelffs!"
	$(LOLELFFS) write -i fs.img /data/info.txt --create -d "Additional information file\nLocated in /data/ directory"

hello-with-fs: hello fs.img
	objcopy --add-section .lolfs.super=fs.img \
	        --set-section-flags .lolfs.super=alloc,load,readonly,data \
	        hello hello-with-fs
	chmod +x hello-with-fs
	@echo ""
	@echo "Build complete: hello-with-fs"
	@readelf -S hello-with-fs | grep ".lolfs.super" || true

test: hello-with-fs
	@echo "Running tests..."
	@./hello-with-fs
	@echo ""
	@echo "Verifying embedded filesystem..."
	@$(LOLELFFS) ls -i hello-with-fs /
	@$(LOLELFFS) cat -i hello-with-fs /config.txt
	@echo ""
	@echo "Tests passed!"

clean:
	rm -f hello hello-with-fs fs.img
