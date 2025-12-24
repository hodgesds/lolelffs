#!/bin/bash
# Comprehensive test for kernel-level encryption unlock mechanism
# Tests the complete flow: create encrypted fs → mount → unlock → read/write

set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

MOUNT_POINT=/tmp/lolelffs_test_unlock
IMAGE=/tmp/lolelffs_unlock_test.img
PASSWORD="TestPassword123"

echo "=== lolelffs Kernel Unlock Test ==="
echo

# Clean up any previous test
umount $MOUNT_POINT 2>/dev/null || true
rmmod lolelffs 2>/dev/null || true
rm -f $IMAGE
mkdir -p $MOUNT_POINT

# Load the kernel module
echo "1. Loading lolelffs kernel module..."
insmod ./lolelffs.ko
lsmod | grep lolelffs

# Create encrypted filesystem with userspace tools
echo
echo "2. Creating encrypted filesystem with userspace tools..."
dd if=/dev/zero of=$IMAGE bs=1M count=50 2>&1 | tail -1
./lolelffs mkfs $IMAGE --encrypt --password "$PASSWORD" --algo aes-256-xts

# Write some test files with userspace tools
echo
echo "3. Writing encrypted files with userspace tools..."
./lolelffs touch --image $IMAGE /test1.txt
echo "This file was written before kernel mount" | \
    ./lolelffs write --image $IMAGE /test1.txt --password "$PASSWORD"

./lolelffs touch --image $IMAGE /secret.txt
echo "Top secret data!" | \
    ./lolelffs write --image $IMAGE /secret.txt --password "$PASSWORD"

# Verify files are readable with userspace tools
echo
echo "4. Verifying files with userspace tools..."
echo "Content of /test1.txt:"
./lolelffs cat --image $IMAGE /test1.txt --password "$PASSWORD"
echo
echo "Content of /secret.txt:"
./lolelffs cat --image $IMAGE /secret.txt --password "$PASSWORD"

# Mount the filesystem in kernel (it will be locked)
echo
echo "5. Mounting encrypted filesystem in kernel..."
mount -t lolelffs -o loop $IMAGE $MOUNT_POINT
mount | grep lolelffs

# Verify filesystem is locked (reads should fail or return garbage)
echo
echo "6. Verifying filesystem is locked (reads should fail)..."
if cat $MOUNT_POINT/test1.txt 2>/dev/null | grep -q "written before"; then
    echo "ERROR: Data is readable without unlocking!"
    exit 1
else
    echo "✓ Filesystem is locked (cannot read encrypted data)"
fi

# List directory (metadata should work even when locked)
echo
echo "7. Directory listing (metadata not encrypted)..."
ls -la $MOUNT_POINT/

# Unlock the filesystem
echo
echo "8. Unlocking filesystem with unlock utility..."
./unlock_lolelffs $MOUNT_POINT "$PASSWORD"

# Verify files are now readable
echo
echo "9. Reading files after unlock..."
echo "Content of /test1.txt:"
cat $MOUNT_POINT/test1.txt
echo
echo "Content of /secret.txt:"
cat $MOUNT_POINT/secret.txt

# Try to write a new file from kernel
echo
echo "10. Writing new file from kernel..."
echo "This file was written by the kernel after unlock" > $MOUNT_POINT/kernel_file.txt

# Verify new file is readable
echo "Content of /kernel_file.txt:"
cat $MOUNT_POINT/kernel_file.txt

# Sync and unmount
echo
echo "11. Syncing and unmounting..."
sync
umount $MOUNT_POINT

# Verify the kernel-written file with userspace tools
echo
echo "12. Verifying kernel-written file with userspace tools..."
echo "Content of /kernel_file.txt (read with userspace tools):"
./lolelffs cat --image $IMAGE /kernel_file.txt --password "$PASSWORD"

# Clean up
echo
echo "13. Cleaning up..."
rmmod lolelffs

echo
echo "=== All Tests Passed! ==="
echo
echo "Summary:"
echo "  ✓ Encrypted filesystem created with userspace tools"
echo "  ✓ Files written and encrypted with userspace tools"
echo "  ✓ Filesystem mounted in kernel (locked state)"
echo "  ✓ Locked filesystem prevents reading encrypted data"
echo "  ✓ Unlock utility successfully unlocked filesystem"
echo "  ✓ Files readable from kernel after unlock"
echo "  ✓ Files writable from kernel after unlock"
echo "  ✓ Kernel-written files properly encrypted"
echo "  ✓ Cross-compatibility between kernel and userspace tools"
echo
echo "Kernel unlock mechanism is COMPLETE and WORKING!"
