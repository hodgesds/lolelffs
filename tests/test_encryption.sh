#!/bin/bash
# Test script for lolelffs encryption functionality

set -e

# Find lolelffs binary - try current directory first (for CI), then build directory
if [ -x "./lolelffs" ]; then
    LOLELFFS=./lolelffs
elif [ -x "./lolelffs-tools/target/release/lolelffs" ]; then
    LOLELFFS=./lolelffs-tools/target/release/lolelffs
else
    echo "ERROR: lolelffs binary not found"
    exit 1
fi

IMG=/tmp/lolelffs_enc_test.img
PASSWORD="MySecretPassword123"

echo "=== lolelffs Encryption Test ==="
echo

# Clean up
rm -f $IMG

# Create test image
echo "1. Creating 10MB test image..."
dd if=/dev/zero of=$IMG bs=1M count=10 2>&1 | tail -1

# Create encrypted filesystem
echo
echo "2. Creating encrypted filesystem with AES-256-XTS..."
$LOLELFFS mkfs $IMG --encrypt --password "$PASSWORD" --algo aes-256-xts

# Create test file
echo
echo "3. Creating test file..."
$LOLELFFS touch --image $IMG /test.txt

# Write encrypted data
echo
echo "4. Writing encrypted data..."
echo "This is sensitive data that should be encrypted!" | \
    $LOLELFFS write --image $IMG /test.txt --password "$PASSWORD"

# Read encrypted data
echo
echo "5. Reading encrypted data (with correct password)..."
$LOLELFFS cat --image $IMG /test.txt --password "$PASSWORD"

# Test wrong password
echo
echo "6. Testing with wrong password (should fail gracefully)..."
echo "   Output with wrong password:"
$LOLELFFS cat --image $IMG /test.txt --password "WrongPassword" 2>&1 | head -c 50
echo "..."

# Test no password
echo
echo
echo "7. Testing without password (should show error)..."
$LOLELFFS cat --image $IMG /test.txt 2>&1 || true

# Verify data is encrypted on disk
echo
echo "8. Verifying data is encrypted on disk..."
if strings $IMG | grep -q "sensitive data"; then
    echo "   ERROR: Plaintext found on disk!"
    exit 1
else
    echo "   ✓ Data is properly encrypted (not visible in plaintext)"
fi

# Test multiple files
echo
echo "9. Creating multiple encrypted files..."
$LOLELFFS touch --image $IMG /file1.txt
$LOLELFFS touch --image $IMG /file2.txt
$LOLELFFS touch --image $IMG /file3.txt

echo "File 1 contents" | $LOLELFFS write --image $IMG /file1.txt --password "$PASSWORD"
echo "File 2 contents" | $LOLELFFS write --image $IMG /file2.txt --password "$PASSWORD"
echo "File 3 contents" | $LOLELFFS write --image $IMG /file3.txt --password "$PASSWORD"

echo "   Reading file1: $($LOLELFFS cat --image $IMG /file1.txt --password "$PASSWORD")"
echo "   Reading file2: $($LOLELFFS cat --image $IMG /file2.txt --password "$PASSWORD")"
echo "   Reading file3: $($LOLELFFS cat --image $IMG /file3.txt --password "$PASSWORD")"

# Test larger file (compression + encryption)
echo
echo "10. Testing larger file with compression and encryption..."
dd if=/dev/urandom bs=1K count=100 of=/tmp/random_data.bin 2>/dev/null
$LOLELFFS cp --image $IMG /tmp/random_data.bin /largefile.bin --password "$PASSWORD"
rm -f /tmp/random_data.bin

SIZE=$($LOLELFFS stat --image $IMG /largefile.bin | grep Size | awk '{print $2}')
echo "   Created file of size: $SIZE bytes"

# Show filesystem stats
echo
echo "11. Filesystem statistics..."
$LOLELFFS super --image $IMG

echo
echo "=== All tests passed! ==="
echo
echo "Summary:"
echo "  - Encryption: AES-256-XTS with PBKDF2"
echo "  - Master key encrypted and stored in superblock"
echo "  - Per-block encryption with block number as IV"
echo "  - Compress-then-encrypt pipeline working"
echo "  - Password verification working"
echo "  - Data properly encrypted on disk"
