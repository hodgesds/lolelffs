#!/usr/bin/env bash
#
# Functional tests for lolelffs FUSE driver
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

print_header() {
    echo ""
    echo "============================================"
    echo "$1"
    echo "============================================"
}

test_op() {
    local op=$1
    local expected_fail=${2:-0}
    local name=${3:-$op}

    TESTS_RUN=$((TESTS_RUN + 1))
    echo -n "  Testing: $name... "

    if sh -c "$op" >/dev/null 2>&1; then
        if [ "$expected_fail" -eq 0 ]; then
            echo -e "${GREEN}PASS${NC}"
            TESTS_PASSED=$((TESTS_PASSED + 1))
            return 0
        else
            echo -e "${RED}FAIL${NC} (expected failure)"
            TESTS_FAILED=$((TESTS_FAILED + 1))
            return 1
        fi
    else
        if [ "$expected_fail" -eq 1 ]; then
            echo -e "${GREEN}PASS${NC} (failed as expected)"
            TESTS_PASSED=$((TESTS_PASSED + 1))
            return 0
        else
            echo -e "${RED}FAIL${NC}"
            TESTS_FAILED=$((TESTS_FAILED + 1))
            return 1
        fi
    fi
}

check_file_content() {
    local file=$1
    local expected=$2

    TESTS_RUN=$((TESTS_RUN + 1))
    echo -n "  Check content of $file... "

    local content=$(cat "$file" 2>/dev/null)
    if [ "$content" = "$expected" ]; then
        echo -e "${GREEN}PASS${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}FAIL${NC} (got: '$content', expected: '$expected')"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Setup
IMAGE=test.img
MOUNTPOINT=fuse_test_mount
FUSE_BIN=./lolelffs-fuse

print_header "Setting up FUSE test environment"

if [ ! -f "$IMAGE" ]; then
    echo "Error: Test image $IMAGE not found"
    exit 1
fi

if [ ! -f "$FUSE_BIN" ]; then
    echo "Error: FUSE binary $FUSE_BIN not found"
    exit 1
fi

# Create mount point
mkdir -p "$MOUNTPOINT"

# Cleanup any previous mounts
fusermount -u "$MOUNTPOINT" 2>/dev/null || true
fusermount3 -u "$MOUNTPOINT" 2>/dev/null || true

echo "Mounting filesystem with FUSE driver..."
echo "Command: $FUSE_BIN $IMAGE $MOUNTPOINT --foreground --debug"

# Mount in background with debug output
$FUSE_BIN "$IMAGE" "$MOUNTPOINT" --foreground --debug > fuse.log 2>&1 &
FUSE_PID=$!

# Give it time to mount
sleep 2

# Check if FUSE process is still running
if ! kill -0 $FUSE_PID 2>/dev/null; then
    echo -e "${RED}Error: FUSE driver failed to start${NC}"
    cat fuse.log
    exit 1
fi

# Verify mount
if ! mountpoint -q "$MOUNTPOINT" && ! grep -q "$MOUNTPOINT" /proc/mounts; then
    echo -e "${YELLOW}Warning: mountpoint command doesn't show mounted, but will try tests anyway${NC}"
fi

cd "$MOUNTPOINT"

# ==================== TESTS ====================

print_header "Directory Operations"

test_op 'mkdir dir1' 0 "Create directory"
test_op 'mkdir dir1/subdir' 0 "Create subdirectory"
test_op 'ls -la dir1' 0 "List directory contents"

print_header "File Operations"

test_op 'touch file1.txt' 0 "Create file"
test_op 'touch dir1/file2.txt' 0 "Create file in directory"
test_op 'ls -la file1.txt' 0 "Stat file"

print_header "File I/O"

test_op 'echo "Hello FUSE World" > file1.txt' 0 "Write to file"
check_file_content file1.txt "Hello FUSE World"

test_op 'echo "Line 1" > multiline.txt' 0 "Create multiline file"
test_op 'echo "Line 2" >> multiline.txt' 0 "Append to file"

# Read first line
TESTS_RUN=$((TESTS_RUN + 1))
echo -n "  Check first line of multiline.txt... "
FIRST_LINE=$(head -1 multiline.txt)
if [ "$FIRST_LINE" = "Line 1" ]; then
    echo -e "${GREEN}PASS${NC}"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}FAIL${NC} (got: '$FIRST_LINE')"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

print_header "Directory Listing"

test_op 'ls -la' 0 "List current directory"
test_op 'ls -R' 0 "Recursive list"

print_header "File Information"

TESTS_RUN=$((TESTS_RUN + 1))
echo -n "  Check file size of file1.txt... "
FILE_SIZE=$(stat -c %s file1.txt 2>/dev/null || stat -f %z file1.txt 2>/dev/null)
if [ "$FILE_SIZE" -gt 0 ]; then
    echo -e "${GREEN}PASS${NC} (size: $FILE_SIZE bytes)"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}FAIL${NC} (size: $FILE_SIZE)"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

print_header "Binary Data"

test_op 'dd if=/dev/urandom of=random.dat bs=1K count=10 2>/dev/null' 0 "Write random binary data"
test_op 'test -f random.dat' 0 "Verify binary file exists"

TESTS_RUN=$((TESTS_RUN + 1))
echo -n "  Check size of random.dat... "
RANDOM_SIZE=$(stat -c %s random.dat 2>/dev/null || stat -f %z random.dat 2>/dev/null)
EXPECTED_SIZE=$((10 * 1024))
if [ "$RANDOM_SIZE" -eq "$EXPECTED_SIZE" ]; then
    echo -e "${GREEN}PASS${NC} (size: $RANDOM_SIZE bytes)"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}FAIL${NC} (size: $RANDOM_SIZE, expected: $EXPECTED_SIZE)"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

print_header "Delete Operations"

test_op 'rm random.dat' 0 "Delete file"
test_op 'test ! -f random.dat' 0 "Verify file deleted"

print_header "Final State"

echo "Final directory contents:"
ls -la

# ==================== END TESTS ====================

print_header "Cleanup"

cd ..
sleep 1

# Unmount
echo "Unmounting filesystem..."
# Kill the FUSE process gracefully
kill $FUSE_PID 2>/dev/null || true
sleep 1

# If FUSE utilities are available, use them as backup
if command -v fusermount3 >/dev/null 2>&1; then
    fusermount3 -u "$MOUNTPOINT" 2>/dev/null || true
elif command -v fusermount >/dev/null 2>&1; then
    fusermount -u "$MOUNTPOINT" 2>/dev/null || true
fi

sleep 1

# Clean up
rmdir "$MOUNTPOINT" 2>/dev/null || true

print_header "Test Summary"

echo ""
echo "Tests run:    $TESTS_RUN"
echo -e "Tests passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Tests failed: ${RED}$TESTS_FAILED${NC}"
echo ""

if [ "$TESTS_FAILED" -eq 0 ]; then
    echo -e "${GREEN}All FUSE tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some FUSE tests failed!${NC}"
    echo ""
    echo "FUSE log output:"
    cat fuse.log
    exit 1
fi
