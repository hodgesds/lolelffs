#!/usr/bin/env bash
#
# Integration tests for lolelffs filesystem
# Requires root privileges to load kernel module and mount filesystem
#

set -e

LOLELFFS_MOD=lolelffs.ko
IMAGE=$1
IMAGESIZE=${2:-200}
MKFS=${3:-mkfs.lolelffs}

# Test modes
D_MOD="drwxr-xr-x"
F_MOD="-rw-r--r--"
S_MOD="lrwxrwxrwx"

# Filesystem limits
MAXFILESIZE=11173888 # LOLELFFS_MAX_EXTENTS * LOLELFFS_MAX_BLOCKS_PER_EXTENT * LOLELFFS_BLOCK_SIZE
MAXFILES=20          # Limited for faster tests

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

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

    if sudo sh -c "$op" >/dev/null 2>&1; then
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

check_exist() {
    local mode=$1
    local nlink=$2
    local name=$3

    TESTS_RUN=$((TESTS_RUN + 1))
    echo -n "  Check exists: $mode $nlink $name... "

    if sudo ls -lR | grep -e "$mode $nlink".*$name >/dev/null 2>&1; then
        echo -e "${GREEN}PASS${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}FAIL${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

check_not_exist() {
    local name=$1

    TESTS_RUN=$((TESTS_RUN + 1))
    echo -n "  Check not exists: $name... "

    if ! sudo ls -la | grep -w "$name" >/dev/null 2>&1; then
        echo -e "${GREEN}PASS${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}FAIL${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

check_file_content() {
    local file=$1
    local expected=$2

    TESTS_RUN=$((TESTS_RUN + 1))
    echo -n "  Check content of $file... "

    local content=$(sudo cat "$file" 2>/dev/null)
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

check_file_size() {
    local file=$1
    local max_size=$2

    TESTS_RUN=$((TESTS_RUN + 1))
    echo -n "  Check size of $file <= $max_size... "

    local size=$(sudo stat -c %s "$file" 2>/dev/null)
    if [ "$size" -le "$max_size" ]; then
        echo -e "${GREEN}PASS${NC} (size: $size)"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}FAIL${NC} (size: $size, max: $max_size)"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Sanity checks
if [ "$EUID" -eq 0 ]; then
    echo "Don't run this script as root"
    exit 1
fi

if [ -z "$IMAGE" ]; then
    echo "Usage: $0 <image> [imagesize_mb] [mkfs_path]"
    exit 1
fi

# Setup
print_header "Setting up test environment"

mkdir -p test
sudo umount test 2>/dev/null || true
sleep 1
sudo rmmod lolelffs 2>/dev/null || true
sleep 1

# Load module
echo "Loading lolelffs module..."
if ! modinfo $LOLELFFS_MOD >/dev/null 2>&1; then
    echo -e "${RED}Error: Cannot find $LOLELFFS_MOD${NC}"
    exit 1
fi

sudo insmod $LOLELFFS_MOD

# Create and format filesystem
echo "Creating filesystem image ($IMAGESIZE MB)..."
cp "$IMAGE" fs.elf
./$MKFS fs.elf

# Mount filesystem
echo "Mounting filesystem..."
sudo mount -t lolelffs -o loop fs.elf test
pushd test >/dev/null

# ==================== TESTS ====================

print_header "Directory Operations"

test_op 'mkdir dir1' 0 "Create directory"
test_op 'mkdir dir1' 1 "Create existing directory (should fail)"
test_op 'mkdir dir1/subdir' 0 "Create subdirectory"
test_op 'mkdir -p dir2/nested/deep' 0 "Create nested directories"
test_op 'rmdir dir2/nested/deep' 0 "Remove empty directory"
test_op 'rmdir dir2/nested' 0 "Remove parent directory"

print_header "File Operations"

test_op 'touch file1' 0 "Create file"
test_op 'touch file1' 0 "Touch existing file"
test_op 'touch dir1/file2' 0 "Create file in directory"

# Multiple files
for i in $(seq 1 $MAXFILES); do
    test_op "touch num_$i.txt" 0 "Create file num_$i.txt"
done

# Count files
filecnt=$(ls -1 | wc -l)
echo "  Total files/dirs created: $filecnt"

# Cleanup numbered files
sudo find . -name 'num_*.txt' -delete

print_header "Link Operations"

test_op 'ln file1 hardlink1' 0 "Create hard link"
test_op 'ln file1 hardlink2' 0 "Create second hard link"
test_op 'ln -s file1 symlink1' 0 "Create symbolic link"
test_op 'ln -s dir1 symlink_dir' 0 "Create symbolic link to directory"
test_op 'ln -s nonexistent broken_link' 0 "Create broken symbolic link"

print_header "File I/O"

test_op 'echo "Hello World" > file1' 0 "Write to file"
check_file_content file1 "Hello World"

test_op 'echo "Line 1" > multiline' 0 "Create multiline file"
test_op 'echo "Line 2" >> multiline' 0 "Append to file"
test_op 'echo "Line 3" >> multiline' 0 "Append to file again"

# Binary data
test_op 'dd if=/dev/urandom of=random_data bs=1K count=10 status=none' 0 "Write random data"

print_header "File Size Limits"

test_op 'dd if=/dev/zero of=large_file bs=1M count=5 status=none' 0 "Create 5MB file"
test_op 'dd if=/dev/zero of=max_file bs=1M count=12 status=none' 0 "Create file at max size limit"
check_file_size max_file $MAXFILESIZE

print_header "Filename Tests"

# Long filenames
test_op 'touch abcdefghijklmnopqrstuvwxyz_123456789' 0 "Create file with long name"
test_op 'mkdir directory_with_a_very_long_name_that_tests_limits' 0 "Create dir with long name"

# Special characters (that are valid)
test_op 'touch file-with-dashes' 0 "Create file with dashes"
test_op 'touch file_with_underscores' 0 "Create file with underscores"
test_op 'touch file.with.dots' 0 "Create file with dots"

print_header "Permission Tests"

test_op 'chmod 755 file1' 0 "Change permissions"
test_op 'chmod 644 dir1/file2' 0 "Change file permissions in directory"

print_header "Rename and Move Operations"

test_op 'mv file1 file1_renamed' 0 "Rename file"
test_op 'mv file1_renamed dir1/' 0 "Move file to directory"
test_op 'mv dir1/subdir dir1/subdir_renamed' 0 "Rename directory"

print_header "Delete Operations"

test_op 'rm random_data' 0 "Delete file"
check_not_exist random_data
test_op 'rm hardlink1' 0 "Delete hard link"
test_op 'rm symlink1' 0 "Delete symbolic link"
test_op 'rm -rf dir2' 0 "Recursive delete"

print_header "Existence Checks"

check_exist $D_MOD 3 dir1
check_exist $F_MOD 1 large_file
check_exist $F_MOD 2 hardlink2
check_exist $S_MOD 1 symlink_dir

print_header "Concurrent Operations"

# Create multiple files in quick succession
for i in $(seq 1 5); do
    test_op "touch concurrent_$i" 0 "Concurrent create $i"
done &
wait

print_header "Edge Cases"

# Empty file
test_op 'touch empty_file' 0 "Create empty file"
check_file_content empty_file ""

# Overwrite file
test_op 'echo "first" > overwrite_test' 0 "Write first content"
test_op 'echo "second" > overwrite_test' 0 "Overwrite with second content"
check_file_content overwrite_test "second"

# Directory listing
test_op 'ls -la' 0 "List directory"
test_op 'ls -lR' 0 "Recursive list"

print_header "Cleanup and Verify"

# Final listing
echo "Final directory contents:"
sudo ls -la

# Filesystem stats
echo ""
echo "Filesystem statistics:"
df -h . || true

# ==================== END TESTS ====================

print_header "Test Summary"

popd >/dev/null
sleep 1
sudo umount test
sudo rmmod lolelffs
sudo rm -f fs.elf

echo ""
echo "Tests run:    $TESTS_RUN"
echo -e "Tests passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Tests failed: ${RED}$TESTS_FAILED${NC}"
echo ""

if [ "$TESTS_FAILED" -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi
