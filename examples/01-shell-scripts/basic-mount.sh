#!/bin/bash

# basic-mount.sh - Demonstrate kernel module mounting of lolelffs
#
# This script shows the complete workflow for mounting a lolelffs filesystem
# using the Linux kernel module. It demonstrates:
# - Loading the kernel module with dependencies
# - Creating a filesystem image
# - Mounting the filesystem
# - Performing file operations through the VFS
# - Unmounting and cleanup
#
# REQUIRES: Root access (sudo)

set -e  # Exit on error
set -u  # Exit on undefined variable

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MKFS="$PROJECT_ROOT/mkfs.lolelffs"
MODULE="$PROJECT_ROOT/lolelffs.ko"
IMAGE_FILE="basic_mount_test.img"
MOUNT_POINT="/tmp/lolelffs_basic_mount_$$"
IMAGE_SIZE_MB=10

# Cleanup function
cleanup() {
    local exit_code=$?
    echo -e "${BLUE}Cleaning up...${NC}"

    # Unmount if mounted
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        echo "Unmounting $MOUNT_POINT"
        umount "$MOUNT_POINT" 2>/dev/null || true
    fi

    # Remove mount point
    if [ -d "$MOUNT_POINT" ]; then
        rmdir "$MOUNT_POINT" 2>/dev/null || true
    fi

    # Remove image file
    if [ -f "$IMAGE_FILE" ]; then
        rm -f "$IMAGE_FILE"
    fi

    # Unload module (don't fail if already unloaded or in use)
    if lsmod | grep -q "^lolelffs "; then
        rmmod lolelffs 2>/dev/null || true
    fi

    if [ $exit_code -eq 0 ]; then
        echo -e "${GREEN}Cleanup complete${NC}"
    else
        echo -e "${RED}Script failed with exit code $exit_code${NC}"
    fi
}

trap cleanup EXIT

# Show usage
usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Demonstrate kernel module mounting of lolelffs filesystem.

Options:
    -h, --help          Show this help message
    -s, --size SIZE     Image size in MB (default: 10)
    -m, --mount-point   Mount point (default: /tmp/lolelffs_basic_mount_PID)

Requirements:
    - Root access (run with sudo)
    - lolelffs.ko kernel module built
    - mkfs.lolelffs utility built

Example:
    sudo $0
    sudo $0 --size 50

EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -s|--size)
            IMAGE_SIZE_MB="$2"
            shift 2
            ;;
        -m|--mount-point)
            MOUNT_POINT="$2"
            shift 2
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            usage
            exit 1
            ;;
    esac
done

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}ERROR: This script must be run as root${NC}"
    echo "Please run with: sudo $0"
    exit 1
fi

# Check dependencies
echo -e "${BLUE}Checking dependencies...${NC}"

if [ ! -f "$MKFS" ]; then
    echo -e "${RED}ERROR: mkfs.lolelffs not found at $MKFS${NC}"
    echo "Build it with: cd $PROJECT_ROOT && make"
    exit 1
fi

if [ ! -f "$MODULE" ]; then
    echo -e "${RED}ERROR: lolelffs.ko not found at $MODULE${NC}"
    echo "Build it with: cd $PROJECT_ROOT && make"
    exit 1
fi

echo -e "${GREEN}Dependencies OK${NC}"

# Step 1: Load kernel module dependencies
echo -e "${BLUE}Step 1: Loading kernel module dependencies...${NC}"

# Load compression modules (may already be loaded or built-in)
modprobe zlib_deflate 2>/dev/null || echo "  zlib_deflate already loaded or built-in"
modprobe lz4_compress 2>/dev/null || echo "  lz4_compress already loaded or built-in"

echo -e "${GREEN}Compression modules ready${NC}"

# Step 2: Load lolelffs kernel module
echo -e "${BLUE}Step 2: Loading lolelffs kernel module...${NC}"

if lsmod | grep -q "^lolelffs "; then
    echo "  lolelffs module already loaded, unloading first..."
    rmmod lolelffs || {
        echo -e "${RED}ERROR: Failed to unload existing lolelffs module${NC}"
        echo "Check if any filesystems are still mounted: mount | grep lolelffs"
        exit 1
    }
fi

insmod "$MODULE" || {
    echo -e "${RED}ERROR: Failed to load lolelffs module${NC}"
    echo "Check dmesg for errors: dmesg | tail -20"
    exit 1
}

echo -e "${GREEN}lolelffs module loaded${NC}"
lsmod | grep "^lolelffs "

# Step 3: Create filesystem image
echo -e "${BLUE}Step 3: Creating filesystem image (${IMAGE_SIZE_MB}MB)...${NC}"

"$MKFS" --size ${IMAGE_SIZE_MB}M "$IMAGE_FILE" || {
    echo -e "${RED}ERROR: Failed to create filesystem${NC}"
    exit 1
}

echo -e "${GREEN}Filesystem created: $IMAGE_FILE${NC}"

# Show filesystem information
echo ""
echo "Filesystem details:"
ls -lh "$IMAGE_FILE"
echo ""

# Step 4: Create mount point
echo -e "${BLUE}Step 4: Creating mount point...${NC}"

mkdir -p "$MOUNT_POINT" || {
    echo -e "${RED}ERROR: Failed to create mount point${NC}"
    exit 1
}

echo -e "${GREEN}Mount point created: $MOUNT_POINT${NC}"

# Step 5: Mount the filesystem
echo -e "${BLUE}Step 5: Mounting filesystem...${NC}"

mount -t lolelffs -o loop "$IMAGE_FILE" "$MOUNT_POINT" || {
    echo -e "${RED}ERROR: Failed to mount filesystem${NC}"
    echo "Check dmesg for kernel errors: dmesg | tail -20"
    exit 1
}

echo -e "${GREEN}Filesystem mounted successfully${NC}"
mount | grep "$MOUNT_POINT"
echo ""

# Step 6: Perform file operations
echo -e "${BLUE}Step 6: Performing file operations...${NC}"
echo ""

# List root directory (should be empty)
echo "Initial contents:"
ls -la "$MOUNT_POINT"
echo ""

# Create directories
echo "Creating directories..."
mkdir "$MOUNT_POINT/config"
mkdir "$MOUNT_POINT/data"
mkdir -p "$MOUNT_POINT/logs/archive"
echo -e "${GREEN}Directories created${NC}"
echo ""

# Write files
echo "Writing files..."
echo "port=8080" > "$MOUNT_POINT/config/app.conf"
echo "hostname=localhost" >> "$MOUNT_POINT/config/app.conf"
echo "This is a test file" > "$MOUNT_POINT/data/test.txt"
echo "Log entry 1" > "$MOUNT_POINT/logs/app.log"
echo "Archived log" > "$MOUNT_POINT/logs/archive/old.log"
echo -e "${GREEN}Files written${NC}"
echo ""

# Create symbolic link
echo "Creating symbolic link..."
ln -s ../data/test.txt "$MOUNT_POINT/config/test_link"
echo -e "${GREEN}Symlink created${NC}"
echo ""

# Create hard link
echo "Creating hard link..."
ln "$MOUNT_POINT/data/test.txt" "$MOUNT_POINT/data/test_hardlink.txt"
echo -e "${GREEN}Hard link created${NC}"
echo ""

# List all files
echo "Final directory tree:"
find "$MOUNT_POINT" -ls
echo ""

# Read files back
echo "Reading files back:"
echo -e "${YELLOW}Contents of /config/app.conf:${NC}"
cat "$MOUNT_POINT/config/app.conf"
echo ""
echo -e "${YELLOW}Contents of /data/test.txt:${NC}"
cat "$MOUNT_POINT/data/test.txt"
echo ""
echo -e "${YELLOW}Following symlink /config/test_link:${NC}"
cat "$MOUNT_POINT/config/test_link"
echo ""

# Show disk usage
echo "Filesystem usage:"
df -h "$MOUNT_POINT"
echo ""

# Verify hard link
echo "Verifying hard link (inode numbers should match):"
ls -li "$MOUNT_POINT/data/test.txt" "$MOUNT_POINT/data/test_hardlink.txt"
echo ""

# Step 7: Sync and unmount
echo -e "${BLUE}Step 7: Syncing and unmounting...${NC}"

sync
umount "$MOUNT_POINT" || {
    echo -e "${RED}ERROR: Failed to unmount filesystem${NC}"
    echo "Check for open files: lsof | grep $MOUNT_POINT"
    exit 1
}

echo -e "${GREEN}Filesystem unmounted${NC}"

# Step 8: Verify persistence
echo -e "${BLUE}Step 8: Verifying data persistence...${NC}"

# Remount to verify data was written
mount -t lolelffs -o loop "$IMAGE_FILE" "$MOUNT_POINT"
echo "Remounted filesystem"
echo ""

echo "Verifying files still exist:"
if [ -f "$MOUNT_POINT/config/app.conf" ]; then
    echo -e "${GREEN}✓${NC} /config/app.conf exists"
    cat "$MOUNT_POINT/config/app.conf"
else
    echo -e "${RED}✗${NC} /config/app.conf missing!"
    exit 1
fi
echo ""

if [ -f "$MOUNT_POINT/data/test.txt" ]; then
    echo -e "${GREEN}✓${NC} /data/test.txt exists"
    cat "$MOUNT_POINT/data/test.txt"
else
    echo -e "${RED}✗${NC} /data/test.txt missing!"
    exit 1
fi
echo ""

if [ -L "$MOUNT_POINT/config/test_link" ]; then
    echo -e "${GREEN}✓${NC} Symlink /config/test_link exists"
    ls -l "$MOUNT_POINT/config/test_link"
else
    echo -e "${RED}✗${NC} Symlink missing!"
    exit 1
fi
echo ""

# Unmount again
umount "$MOUNT_POINT"

# Success!
echo ""
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo -e "${GREEN}SUCCESS: Kernel module mount test complete${NC}"
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo ""
echo "What this demonstrated:"
echo "  ✓ Loading kernel module with dependencies"
echo "  ✓ Creating filesystem image with mkfs.lolelffs"
echo "  ✓ Mounting with mount -t lolelffs"
echo "  ✓ Creating directories and files"
echo "  ✓ Symbolic and hard links"
echo "  ✓ Reading files through VFS"
echo "  ✓ Data persistence across unmount/remount"
echo ""
echo "Next steps:"
echo "  - Try ./fuse-mount.sh for mounting without root"
echo "  - Try ./create-and-populate.sh for CLI-only operations"
echo "  - Try ./encrypted-workflow.sh for encryption"
echo ""
