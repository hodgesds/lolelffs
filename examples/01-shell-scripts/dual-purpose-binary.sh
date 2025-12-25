#!/bin/bash

# dual-purpose-binary.sh - Create executable + mountable binaries
#
# This script demonstrates lolelffs's key feature: creating binaries that are
# both executable AND mountable as filesystems. The same file can be:
# 1. Executed as a normal program
# 2. Mounted as a filesystem (kernel module or userspace)
# 3. Accessed via CLI tools
#
# REQUIRES: No root for creation, root for mounting (or use FUSE)

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
OUTPUT_BINARY="hello-with-fs"
FS_IMAGE="temp_fs.img"

cleanup() {
    [ -f "$FS_IMAGE" ] && rm -f "$FS_IMAGE"
    [ -f "$OUTPUT_BINARY" ] && rm -f "$OUTPUT_BINARY"
    [ -f "hello.c" ] && rm -f "hello.c"
    [ -f "hello.o" ] && rm -f "hello.o"
}
trap cleanup EXIT

if [ ! -f "$LOLELFFS" ]; then
    echo -e "${RED}ERROR: lolelffs not found${NC}"
    exit 1
fi

echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Creating Dual-Purpose Binary${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo ""

# Step 1: Create a simple C program
echo -e "${BLUE}Step 1: Creating source program${NC}"
cat > hello.c <<'EOF'
#include <stdio.h>

int main() {
    printf("Hello from dual-purpose binary!\n");
    printf("This binary is both executable AND mountable.\n");
    printf("Try mounting me: sudo mount -t lolelffs -o loop %s /mnt/point\n", __FILE__);
    return 0;
}
EOF

echo -e "${GREEN}Source file created${NC}"
cat hello.c
echo ""

# Step 2: Compile the program
echo -e "${BLUE}Step 2: Compiling program${NC}"
gcc -o "$OUTPUT_BINARY" hello.c
echo -e "${GREEN}Binary compiled${NC}"
ls -lh "$OUTPUT_BINARY"
echo ""

# Step 3: Test execution BEFORE embedding
echo -e "${BLUE}Step 3: Testing binary before embedding filesystem${NC}"
./"$OUTPUT_BINARY"
echo ""

# Step 4: Pad binary to make room for filesystem
echo -e "${BLUE}Step 4: Padding binary${NC}"
ORIGINAL_SIZE=$(stat -c%s "$OUTPUT_BINARY")
echo "Original size: $ORIGINAL_SIZE bytes"

# Pad to 5MB to have room for filesystem
truncate -s 5M "$OUTPUT_BINARY"
NEW_SIZE=$(stat -c%s "$OUTPUT_BINARY")
echo "Padded size: $NEW_SIZE bytes"
echo -e "${GREEN}Binary padded${NC}"
echo ""

# Step 5: Create filesystem image
echo -e "${BLUE}Step 5: Creating filesystem image${NC}"
"$LOLELFFS" mkfs --size 4M "$FS_IMAGE"
echo -e "${GREEN}Filesystem created${NC}"
echo ""

# Step 6: Populate filesystem
echo -e "${BLUE}Step 6: Populating filesystem${NC}"
"$LOLELFFS" mkdir -i "$FS_IMAGE" /config
"$LOLELFFS" mkdir -i "$FS_IMAGE" /data

"$LOLELFFS" write -i "$FS_IMAGE" /config/app.conf --create -d "# Application Configuration
application=hello-dual-purpose
version=1.0
embedded_fs=true"

"$LOLELFFS" write -i "$FS_IMAGE" /data/info.txt --create -d "This file is embedded inside an executable binary!
The binary can both run as a program AND be mounted as a filesystem.
This demonstrates lolelffs's unique capability."

"$LOLELFFS" write -i "$FS_IMAGE" /README.txt --create -d "Welcome to the embedded filesystem!

This filesystem is contained within an ELF executable binary.
You can mount this binary to access these files:

  sudo mount -t lolelffs -o loop $OUTPUT_BINARY /mnt/point

Or access via CLI tools:

  $LOLELFFS ls -i $OUTPUT_BINARY /
  $LOLELFFS cat -i $OUTPUT_BINARY /README.txt
"

echo -e "${GREEN}Filesystem populated${NC}"
"$LOLELFFS" ls -i "$FS_IMAGE" / -l
echo ""

# Step 7: Embed filesystem in binary using objcopy
echo -e "${BLUE}Step 7: Embedding filesystem in binary${NC}"
objcopy --add-section .lolfs.super="$FS_IMAGE" \
        --set-section-flags .lolfs.super=alloc,load,readonly,data \
        "$OUTPUT_BINARY" "$OUTPUT_BINARY.tmp"
mv "$OUTPUT_BINARY.tmp" "$OUTPUT_BINARY"
chmod +x "$OUTPUT_BINARY"

echo -e "${GREEN}Filesystem embedded in binary${NC}"
echo ""

# Step 8: Verify ELF structure
echo -e "${BLUE}Step 8: Verifying ELF structure${NC}"
echo "Checking for .lolfs.super section..."
if readelf -S "$OUTPUT_BINARY" | grep -q ".lolfs.super"; then
    echo -e "${GREEN}✓ .lolfs.super section found${NC}"
    readelf -S "$OUTPUT_BINARY" | grep ".lolfs.super"
else
    echo -e "${RED}✗ .lolfs.super section NOT found${NC}"
    exit 1
fi
echo ""

# Step 9: Test execution AFTER embedding
echo -e "${BLUE}Step 9: Testing binary after embedding filesystem${NC}"
echo "Binary should still execute normally..."
./"$OUTPUT_BINARY"
echo -e "${GREEN}✓ Binary still executes correctly${NC}"
echo ""

# Step 10: Access embedded filesystem via CLI
echo -e "${BLUE}Step 10: Accessing embedded filesystem via CLI${NC}"
echo ""

echo -e "${YELLOW}Files in embedded filesystem:${NC}"
"$LOLELFFS" ls -i "$OUTPUT_BINARY" / -l
echo ""
echo "Files in /config:"
"$LOLELFFS" ls -i "$OUTPUT_BINARY" /config -l
echo ""
echo "Files in /data:"
"$LOLELFFS" ls -i "$OUTPUT_BINARY" /data -l
echo ""

echo -e "${YELLOW}Reading /README.txt:${NC}"
"$LOLELFFS" cat -i "$OUTPUT_BINARY" /README.txt
echo ""

echo -e "${YELLOW}Reading /config/app.conf:${NC}"
"$LOLELFFS" cat -i "$OUTPUT_BINARY" /config/app.conf
echo ""

echo -e "${YELLOW}Reading /data/info.txt:${NC}"
"$LOLELFFS" cat -i "$OUTPUT_BINARY" /data/info.txt
echo ""

# Step 11: Show filesystem info
echo -e "${BLUE}Step 11: Filesystem information${NC}"
"$LOLELFFS" super -i "$OUTPUT_BINARY"
echo ""

# Step 12: Final verification
echo -e "${BLUE}Step 12: Final verification${NC}"
echo "File type:"
file "$OUTPUT_BINARY"
echo ""
echo "File size:"
ls -lh "$OUTPUT_BINARY"
echo ""

# Modify cleanup to keep the binary
cleanup() {
    [ -f "$FS_IMAGE" ] && rm -f "$FS_IMAGE"
    [ -f "hello.c" ] && rm -f "hello.c"
    [ -f "hello.o" ] && rm -f "hello.o"
    echo -e "${GREEN}Dual-purpose binary preserved: $OUTPUT_BINARY${NC}"
}
trap cleanup EXIT

# Success
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo -e "${GREEN}SUCCESS: Dual-purpose binary created${NC}"
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo ""
echo "Created: $OUTPUT_BINARY"
echo ""
echo "This binary can be used in three ways:"
echo ""
echo "1. Execute as a normal program:"
echo "   ./$OUTPUT_BINARY"
echo ""
echo "2. Access via CLI tools:"
echo "   $LOLELFFS ls -i $OUTPUT_BINARY /"
echo "   $LOLELFFS cat -i $OUTPUT_BINARY /README.txt"
echo ""
echo "3. Mount as filesystem (requires root):"
echo "   sudo mkdir -p /mnt/dual"
echo "   sudo mount -t lolelffs -o loop $OUTPUT_BINARY /mnt/dual"
echo "   ls /mnt/dual"
echo "   sudo umount /mnt/dual"
echo ""
echo "Key technique used:"
echo "  objcopy --add-section .lolfs.super=fs.img binary binary"
echo ""
