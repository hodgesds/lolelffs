#!/bin/bash
# Test script for mounting ELF binaries with embedded lolelffs filesystems

set -e

echo "=== lolelffs ELF Binary Mount Test ==="
echo

# Check if module is loaded
if ! lsmod | grep -q lolelffs; then
    echo "Loading kernel module..."
    sudo insmod lolelffs.ko
    echo "Module loaded."
else
    echo "Kernel module already loaded."
fi

# Create mount point
sudo mkdir -p /mnt/debug

echo
echo "=== Testing Mount of ELF Binary with Embedded Filesystem ==="
echo "ELF Binary: lolelffs_with_fs"
echo "Contains: .lolfs.super section with 10MB filesystem"
echo

# Try to mount the ELF binary
echo "Mounting lolelffs_with_fs..."
if sudo mount -t lolelffs -o loop lolelffs_with_fs /mnt/debug/; then
    echo "✓ Mount successful!"
    echo

    # Check dmesg for ELF detection
    echo "Kernel messages:"
    dmesg | tail -5 | grep -i lolelffs || true
    echo

    # Test filesystem operations
    echo "Testing filesystem operations..."
    sudo sh -c 'echo "Hello from ELF-embedded filesystem!" > /mnt/debug/test.txt'
    cat /mnt/debug/test.txt
    echo

    sudo mkdir /mnt/debug/mydir
    ls -la /mnt/debug/
    echo

    # Unmount
    echo "Unmounting..."
    sudo umount /mnt/debug/
    echo "✓ Test completed successfully!"
else
    echo "✗ Mount failed. Check dmesg for details:"
    dmesg | tail -10
fi

echo
echo "=== Test Alternative: Raw Filesystem Image ==="
if [ -f test.img ]; then
    echo "Mounting test.img (raw filesystem)..."
    if sudo mount -t lolelffs -o loop test.img /mnt/debug/; then
        echo "✓ Mount successful!"
        echo "Kernel messages:"
        dmesg | tail -3 | grep -i lolelffs || true
        ls -la /mnt/debug/ | head -10
        sudo umount /mnt/debug/
        echo "✓ Raw mount test completed successfully!"
    fi
fi
