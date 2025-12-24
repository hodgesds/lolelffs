obj-m += lolelffs.o
lolelffs-objs := src/fs.o src/super.o src/inode.o src/file.o src/dir.o src/extent.o src/xattr.o src/compress.o src/encrypt.o

KDIR ?= /lib/modules/$(shell uname -r)/build
EXTRA_CFLAGS += -I$(src)/src

MKFS = mkfs.lolelffs
FSCK = fsck.lolelffs
UNLOCK = unlock_lolelffs
TEST_MKFS = test_mkfs
TEST_UNIT = test_unit
TEST_BENCHMARK = test_benchmark
TEST_STRESS = test_stress
RUST_TOOLS_DIR = lolelffs-tools

CC ?= gcc
CFLAGS = -std=gnu99 -Wall -Wextra -g -O2

all: $(MKFS) $(FSCK) $(UNLOCK) rust-tools
	make -C $(KDIR) M=$(PWD) LLVM=1 modules

# Build Rust CLI tools
rust-tools:
	cd $(RUST_TOOLS_DIR) && cargo build --release
	cp $(RUST_TOOLS_DIR)/target/release/lolelffs .

rust-tools-debug:
	cd $(RUST_TOOLS_DIR) && cargo build
	cp $(RUST_TOOLS_DIR)/target/debug/lolelffs .

# Build FUSE driver
fuse: rust-tools
	@echo "Building FUSE driver (release)..."
	cd $(RUST_TOOLS_DIR) && cargo build --release -p lolelffs-fuse
	cp $(RUST_TOOLS_DIR)/target/release/lolelffs-fuse .

fuse-debug: rust-tools-debug
	@echo "Building FUSE driver (debug)..."
	cd $(RUST_TOOLS_DIR) && cargo build -p lolelffs-fuse
	cp $(RUST_TOOLS_DIR)/target/debug/lolelffs-fuse .

# Install FUSE driver
install-fuse: fuse
	@echo "Installing lolelffs-fuse to /usr/local/bin/"
	install -m 755 lolelffs-fuse /usr/local/bin/

$(MKFS): src/mkfs.c src/lolelffs.h
	$(CC) $(CFLAGS) -iquote src -o $@ $< -lelf

$(FSCK): src/fsck.lolelffs.c src/lolelffs.h
	$(CC) $(CFLAGS) -iquote src -o $@ $<

$(UNLOCK): src/unlock_lolelffs.c src/lolelffs.h
	$(CC) $(CFLAGS) -iquote src -o $@ $<

# Unit tests for mkfs functionality
$(TEST_MKFS): tests/test_mkfs.c src/lolelffs.h
	$(CC) $(CFLAGS) -iquote src -o $@ $< -lelf

# Unit tests for filesystem structures
$(TEST_UNIT): tests/test_unit.c src/lolelffs.h
	$(CC) $(CFLAGS) -iquote src -o $@ $<

# Performance benchmarks
$(TEST_BENCHMARK): tests/test_benchmark.c src/lolelffs.h
	$(CC) $(CFLAGS) -iquote src -o $@ $<

# Stress tests
$(TEST_STRESS): tests/test_stress.c src/lolelffs.h
	$(CC) $(CFLAGS) -iquote src -o $@ $<

# Run all tests
test: $(TEST_MKFS) $(TEST_UNIT)
	@echo "=== Running Unit Tests ==="
	./$(TEST_UNIT)
	@echo ""
	@echo "=== Running mkfs Tests ==="
	./$(TEST_MKFS)
	@echo ""
	@echo "=== All tests passed! ==="

# Run performance benchmarks
benchmark: $(TEST_BENCHMARK)
	@echo "=== Running Performance Benchmarks ==="
	./$(TEST_BENCHMARK)

# Run stress tests
stress: $(TEST_STRESS)
	@echo "=== Running Stress Tests ==="
	./$(TEST_STRESS)

# Run all tests including benchmarks and stress tests
test-all: test benchmark stress
	@echo ""
	@echo "=== All tests, benchmarks, and stress tests completed! ==="

# Run integration tests (requires root)
test-integration: all
	@echo "=== Running Integration Tests ==="
	@echo "Note: This requires root privileges and will load/unload kernel modules"
	cd tests && ./test.sh ../test.img 200 ../$(MKFS)

# Create a test image
test-image: $(MKFS)
	dd if=/dev/zero of=test.img bs=1M count=200
	./$(MKFS) test.img

clean:
	make -C $(KDIR) M=$(PWD) LLVM=1 clean
	rm -f *~ $(PWD)/*.ur-safe
	rm -f $(MKFS) $(FSCK) $(UNLOCK) $(TEST_MKFS) $(TEST_UNIT) $(TEST_BENCHMARK) $(TEST_STRESS)
	rm -f test.img tests/*.img
	rm -f lolelffs lolelffs-fuse
	cd $(RUST_TOOLS_DIR) && cargo clean

# Check filesystem image
check: $(FSCK) test-image
	./$(FSCK) -v test.img

.PHONY: all clean test test-integration test-image check benchmark stress test-all rust-tools rust-tools-debug fuse fuse-debug install-fuse
