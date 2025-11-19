obj-m += lolelffs.o
lolelffs-objs := fs.o super.o inode.o file.o dir.o extent.o

KDIR ?= /lib/modules/$(shell uname -r)/build

MKFS = mkfs.lolelffs
FSCK = fsck.lolelffs
TEST_MKFS = test_mkfs
TEST_UNIT = test_unit

CC ?= gcc
CFLAGS = -std=gnu99 -Wall -Wextra -g

all: $(MKFS) $(FSCK)
	make -C $(KDIR) M=$(PWD) modules

$(MKFS): mkfs.c lolelffs.h
	$(CC) $(CFLAGS) -o $@ $< -lelf

$(FSCK): fsck.lolelffs.c lolelffs.h
	$(CC) $(CFLAGS) -o $@ $<

# Unit tests for mkfs functionality
$(TEST_MKFS): test/test_mkfs.c lolelffs.h
	$(CC) $(CFLAGS) -o $@ $< -lelf

# Unit tests for filesystem structures
$(TEST_UNIT): test/test_unit.c lolelffs.h
	$(CC) $(CFLAGS) -o $@ $<

# Run all tests
test: $(TEST_MKFS) $(TEST_UNIT)
	@echo "=== Running Unit Tests ==="
	./$(TEST_UNIT)
	@echo ""
	@echo "=== Running mkfs Tests ==="
	./$(TEST_MKFS)
	@echo ""
	@echo "=== All tests passed! ==="

# Run integration tests (requires root)
test-integration: all
	@echo "=== Running Integration Tests ==="
	@echo "Note: This requires root privileges and will load/unload kernel modules"
	cd test && ./test.sh ../test.img 200 ../$(MKFS)

# Create a test image
test-image: $(MKFS)
	dd if=/dev/zero of=test.img bs=1M count=200
	./$(MKFS) test.img

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f *~ $(PWD)/*.ur-safe
	rm -f $(MKFS) $(FSCK) $(TEST_MKFS) $(TEST_UNIT)
	rm -f test.img test/*.img

# Check filesystem image
check: $(FSCK) test-image
	./$(FSCK) -v test.img

.PHONY: all clean test test-integration test-image check
