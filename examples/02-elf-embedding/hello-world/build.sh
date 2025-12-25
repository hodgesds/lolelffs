#!/bin/bash

set -e

PROJECT_ROOT="../../.."
LOLELFFS="$PROJECT_ROOT/lolelffs"

echo "Building hello-world ELF embedding example..."
echo ""

# Step 1: Compile the program
echo "[1/5] Compiling hello.c..."
gcc -o hello hello.c
echo "  ✓ Compiled"

# Step 2: Create filesystem
echo "[2/5] Creating filesystem (2MB)..."
"$LOLELFFS" mkfs --size 2M fs.img
echo "  ✓ Filesystem created"

# Step 3: Populate filesystem
echo "[3/5] Populating filesystem..."
"$LOLELFFS" mkdir -i fs.img /data
"$LOLELFFS" write -i fs.img /config.txt --create -d "# Application Configuration
app_name=hello-embedded
version=1.0
embedded=true
filesystem=lolelffs"

"$LOLELFFS" write -i fs.img /message.txt --create -d "Welcome to the embedded filesystem!

This file is stored inside an ELF executable binary.
The same binary that can run as a program can also be mounted as a filesystem.

This is the magic of lolelffs!"

"$LOLELFFS" write -i fs.img /data/info.txt --create -d "Additional information file
Located in /data/ directory"

echo "  ✓ Files written"

# Step 4: Embed filesystem in binary
echo "[4/5] Embedding filesystem in binary..."
objcopy --add-section .lolfs.super=fs.img \
        --set-section-flags .lolfs.super=alloc,load,readonly,data \
        hello hello-with-fs
chmod +x hello-with-fs
echo "  ✓ Filesystem embedded"

# Step 5: Verify
echo "[5/5] Verifying..."
if readelf -S hello-with-fs | grep -q ".lolfs.super"; then
    echo "  ✓ .lolfs.super section present"
else
    echo "  ✗ ERROR: .lolfs.super section not found!"
    exit 1
fi

# Show result
echo ""
echo "Success! Created: hello-with-fs"
echo ""
echo "File size:"
ls -lh hello-with-fs
echo ""
echo "Try it:"
echo "  ./hello-with-fs"
echo "  $LOLELFFS ls -i hello-with-fs /"
echo "  $LOLELFFS cat -i hello-with-fs /config.txt"
