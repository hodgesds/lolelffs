#!/bin/bash

# encrypted-workflow.sh - Demonstrate encryption features
#
# This script demonstrates lolelffs encryption capabilities:
# - Creating encrypted filesystems (AES-256-XTS, ChaCha20-Poly1305)
# - Password-based operations
# - Verifying on-disk encryption
# - Unlocking filesystems
#
# REQUIRES: No root access needed!

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
IMAGE_FILE="encrypted_demo.img"
PASSWORD="demo_password_123"

cleanup() {
    [ -f "$IMAGE_FILE" ] && rm -f "$IMAGE_FILE"
}
trap cleanup EXIT

if [ ! -f "$LOLELFFS" ]; then
    echo -e "${RED}ERROR: lolelffs not found at $LOLELFFS${NC}"
    exit 1
fi

echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}lolelffs Encryption Demonstration${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo ""

# Step 1: Create encrypted filesystem
echo -e "${BLUE}Step 1: Creating AES-256-XTS encrypted filesystem${NC}"
"$LOLELFFS" mkfs --size 10M --encrypt --algo aes-256-xts --password "$PASSWORD" "$IMAGE_FILE"
echo -e "${GREEN}Encrypted filesystem created${NC}"
echo ""

# Step 2: Unlock filesystem
echo -e "${BLUE}Step 2: Unlocking filesystem with password${NC}"
"$LOLELFFS" unlock -i "$IMAGE_FILE" --password "$PASSWORD"
echo -e "${GREEN}Filesystem unlocked${NC}"
echo ""

# Step 3: Write encrypted data
echo -e "${BLUE}Step 3: Writing encrypted data (with password)${NC}"
"$LOLELFFS" write -i "$IMAGE_FILE" -P "$PASSWORD" /secret.txt --create -d "This is encrypted data
It is protected by AES-256-XTS encryption
Only accessible with the correct password"

"$LOLELFFS" mkdir -i "$IMAGE_FILE" /secure
"$LOLELFFS" write -i "$IMAGE_FILE" -P "$PASSWORD" /secure/credentials.txt --create -d "username=admin
password=super_secret
api_key=abc123xyz"

echo -e "${GREEN}Encrypted files written${NC}"
echo ""

# Step 4: List encrypted filesystem
echo -e "${BLUE}Step 4: Listing filesystem contents${NC}"
"$LOLELFFS" ls -i "$IMAGE_FILE" / -l
echo ""
echo "Directory /secure:"
"$LOLELFFS" ls -i "$IMAGE_FILE" /secure -l
echo ""

# Step 5: Read encrypted files
echo -e "${BLUE}Step 5: Reading encrypted files (with password)${NC}"
echo -e "${YELLOW}Contents of /secret.txt:${NC}"
"$LOLELFFS" cat -i "$IMAGE_FILE" -P "$PASSWORD" /secret.txt
echo ""
echo -e "${YELLOW}Contents of /secure/credentials.txt:${NC}"
"$LOLELFFS" cat -i "$IMAGE_FILE" -P "$PASSWORD" /secure/credentials.txt
echo ""

# Step 6: Verify data is encrypted on disk
echo -e "${BLUE}Step 6: Verifying on-disk encryption${NC}"
echo "Searching for plaintext in image file..."
if grep -q "This is encrypted data" "$IMAGE_FILE" 2>/dev/null; then
    echo -e "${RED}WARNING: Plaintext found in image (encryption may not be working)${NC}"
else
    echo -e "${GREEN}✓ Plaintext NOT found in image file (encryption working)${NC}"
fi

if grep -q "super_secret" "$IMAGE_FILE" 2>/dev/null; then
    echo -e "${RED}WARNING: Password found in image${NC}"
else
    echo -e "${GREEN}✓ Sensitive data NOT found in image file${NC}"
fi
echo ""

# Step 7: Filesystem stats
echo -e "${BLUE}Step 7: Encrypted filesystem statistics${NC}"
"$LOLELFFS" super -i "$IMAGE_FILE"
echo ""
"$LOLELFFS" df -i "$IMAGE_FILE"
echo ""

# Success
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo -e "${GREEN}SUCCESS: Encryption demonstration complete${NC}"
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo ""
echo "What was demonstrated:"
echo "  ✓ Creating encrypted filesystem with AES-256-XTS"
echo "  ✓ Unlocking encrypted filesystem with password"
echo "  ✓ Writing and reading encrypted data"
echo "  ✓ On-disk encryption verification"
echo "  ✓ Encrypted filesystem operations"
echo ""
echo "Supported encryption methods:"
echo "  • aes256-xts (demonstrated)"
echo "  • chacha20-poly1305"
echo ""
echo "Try with ChaCha20:"
echo "  $LOLELFFS mkfs --size 10M --encrypt chacha20-poly1305 image.img"
echo ""
