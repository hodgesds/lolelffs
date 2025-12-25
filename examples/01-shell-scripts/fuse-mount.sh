#!/bin/bash

# fuse-mount.sh - Demonstrate FUSE mounting (no root required)
#
# This script shows how to mount lolelffs filesystems using the FUSE driver,
# which doesn't require root privileges.

set -e
set -u

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOLELFFS="$PROJECT_ROOT/lolelffs"
FUSE_DRIVER="$PROJECT_ROOT/lolelffs-tools/target/release/lolelffs-fuse"
IMAGE_FILE="fuse_test.img"
MOUNT_POINT="/tmp/lolelffs_fuse_$$"

cleanup() {
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        fusermount -u "$MOUNT_POINT" 2>/dev/null || true
    fi
    [ -d "$MOUNT_POINT" ] && rmdir "$MOUNT_POINT" 2>/dev/null || true
    [ -f "$IMAGE_FILE" ] && rm -f "$IMAGE_FILE"
}
trap cleanup EXIT

echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}FUSE Mounting Demonstration${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo ""

# Check for FUSE driver
if [ ! -f "$FUSE_DRIVER" ]; then
    echo -e "${YELLOW}FUSE driver not found, building...${NC}"
    cd "$PROJECT_ROOT"
    make fuse || {
        echo -e "${RED}ERROR: Failed to build FUSE driver${NC}"
        exit 1
    }
    cd - > /dev/null
fi

# Create filesystem
echo -e "${BLUE}Creating filesystem${NC}"
"$LOLELFFS" mkfs --size 10M "$IMAGE_FILE"
"$LOLELFFS" mkdir -i "$IMAGE_FILE" /test
"$LOLELFFS" write -i "$IMAGE_FILE" /test/file.txt --create -d "Hello from FUSE!"
echo -e "${GREEN}Filesystem created${NC}"
echo ""

# Mount with FUSE
echo -e "${BLUE}Mounting with FUSE (no root required)${NC}"
mkdir -p "$MOUNT_POINT"
"$FUSE_DRIVER" "$IMAGE_FILE" "$MOUNT_POINT" -f &
FUSE_PID=$!
sleep 1

echo -e "${GREEN}Mounted at $MOUNT_POINT${NC}"
echo ""

# Test operations
echo -e "${BLUE}Testing filesystem operations${NC}"
ls -la "$MOUNT_POINT"
cat "$MOUNT_POINT/test/file.txt"
echo ""

# Unmount
echo -e "${BLUE}Unmounting${NC}"
fusermount -u "$MOUNT_POINT"
wait $FUSE_PID 2>/dev/null || true

echo -e "${GREEN}Success! FUSE mounting works${NC}"
