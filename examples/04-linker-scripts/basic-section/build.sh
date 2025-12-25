#!/bin/bash

set -e

PROJECT_ROOT="../../.."
LOLELFFS="$PROJECT_ROOT/lolelffs"

echo "Building embedded filesystem example..."
echo ""

# Step 1: Compile the program
echo "[1/4] Compiling example.c..."
gcc -o example-base example.c
echo "  ✓ Compiled"

# Step 2: Create filesystem
echo "[2/4] Creating filesystem (1MB)..."
"$LOLELFFS" mkfs --size 1M fs.img
echo "  ✓ Created fs.img"

# Step 3: Populate filesystem
echo "[3/4] Populating filesystem..."
"$LOLELFFS" write -i fs.img /info.txt --create -d "This filesystem is embedded in an ELF binary!

The .lolfs.super section contains the complete filesystem.
You can access it using the lolelffs CLI tools."

"$LOLELFFS" mkdir -i fs.img /data
"$LOLELFFS" write -i fs.img /data/test.txt --create -d "Test file in /data/"
echo "  ✓ Files written"

# Step 4: Embed filesystem using objcopy
echo "[4/4] Embedding filesystem in binary..."
objcopy --add-section .lolfs.super=fs.img \
        --set-section-flags .lolfs.super=alloc,load,readonly,data \
        example-base example
chmod +x example
echo "  ✓ Filesystem embedded"

# Verify
echo ""
echo "Verifying..."
if readelf -S example | grep -q ".lolfs.super"; then
    echo "  ✓ .lolfs.super section present"
    readelf -S example | grep ".lolfs.super"
else
    echo "  ✗ ERROR: .lolfs.super section not found!"
    exit 1
fi

echo ""
echo "Success! Created: example"
echo ""
echo "Try it:"
echo "  ./example"
echo "  $LOLELFFS ls -i example /"
echo "  $LOLELFFS cat -i example /info.txt"
