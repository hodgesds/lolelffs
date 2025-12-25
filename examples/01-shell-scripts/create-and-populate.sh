#!/bin/bash

# create-and-populate.sh - Demonstrate lolelffs CLI tools
#
# This script demonstrates using the Rust CLI tools to create and manipulate
# lolelffs filesystems without requiring mounting or root access.
#
# This is the recommended starting point for learning lolelffs as it:
# - Requires no root privileges
# - Works entirely in userspace
# - Demonstrates all major CLI commands
# - Safe to experiment with
#
# REQUIRES: No root access needed!

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
LOLELFFS="$PROJECT_ROOT/lolelffs"
IMAGE_FILE="cli_demo.img"
IMAGE_SIZE_MB=10

# Cleanup function
cleanup() {
    local exit_code=$?

    if [ -f "$IMAGE_FILE" ]; then
        rm -f "$IMAGE_FILE"
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

Demonstrate lolelffs Rust CLI tools (no root required).

Options:
    -h, --help          Show this help message
    -s, --size SIZE     Image size in MB (default: 10)
    -k, --keep          Keep the image file after script completes

Requirements:
    - lolelffs CLI tool built (make rust)

Example:
    $0
    $0 --size 50 --keep

EOF
}

# Parse arguments
KEEP_IMAGE=false
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
        -k|--keep)
            KEEP_IMAGE=true
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            usage
            exit 1
            ;;
    esac
done

# Modify cleanup if keeping image
if [ "$KEEP_IMAGE" = true ]; then
    cleanup() {
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}Image file preserved: $IMAGE_FILE${NC}"
        fi
    }
    trap cleanup EXIT
fi

# Check dependencies
echo -e "${BLUE}Checking dependencies...${NC}"

if [ ! -f "$LOLELFFS" ]; then
    echo -e "${RED}ERROR: lolelffs CLI not found at $LOLELFFS${NC}"
    echo "Build it with: cd $PROJECT_ROOT && make rust"
    exit 1
fi

echo -e "${GREEN}Dependencies OK${NC}"
echo ""

# Show version
echo -e "${BLUE}lolelffs CLI version:${NC}"
"$LOLELFFS" --version || echo "Version command not available"
echo ""

# Step 1: Create filesystem
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 1: Creating filesystem (${IMAGE_SIZE_MB}MB)${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

"$LOLELFFS" mkfs --size ${IMAGE_SIZE_MB}M "$IMAGE_FILE"

echo -e "${GREEN}Filesystem created: $IMAGE_FILE${NC}"
ls -lh "$IMAGE_FILE"
echo ""

# Step 2: Show superblock
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 2: Examining superblock${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

"$LOLELFFS" super -i "$IMAGE_FILE"
echo ""

# Step 3: Create directories
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 3: Creating directory structure${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

echo "Creating directories..."
"$LOLELFFS" mkdir -i "$IMAGE_FILE" /config
"$LOLELFFS" mkdir -i "$IMAGE_FILE" /data
"$LOLELFFS" mkdir -i "$IMAGE_FILE" /logs
"$LOLELFFS" mkdir -i "$IMAGE_FILE" /logs/archive -p

echo -e "${GREEN}Directories created${NC}"
echo ""

# Step 4: List root directory
echo "Root directory contents:"
"$LOLELFFS" ls -i "$IMAGE_FILE" / -l
echo ""

# Step 5: Write files
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 4: Writing files${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

echo "Writing configuration file..."
"$LOLELFFS" write -i "$IMAGE_FILE" /config/app.conf --create -d "port=8080
hostname=localhost
debug=true"

echo "Writing data files..."
"$LOLELFFS" write -i "$IMAGE_FILE" /data/test.txt --create -d "This is a test file
It has multiple lines
And demonstrates file writing"

"$LOLELFFS" write -i "$IMAGE_FILE" /data/info.txt --create -d "lolelffs filesystem
Created with CLI tools
No mounting required!"

echo "Writing log files..."
"$LOLELFFS" write -i "$IMAGE_FILE" /logs/app.log --create -d "[INFO] Application started
[DEBUG] Loading configuration
[INFO] Server listening on port 8080"

"$LOLELFFS" write -i "$IMAGE_FILE" /logs/archive/old.log --create -d "[INFO] Archived log entry
[WARN] This is an old log file"

echo -e "${GREEN}Files written${NC}"
echo ""

# Step 6: Create symbolic link
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 5: Creating symbolic link${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

"$LOLELFFS" ln -i "$IMAGE_FILE" --symbolic /data/test.txt /config/test_link
echo -e "${GREEN}Symbolic link created: /config/test_link -> /data/test.txt${NC}"
echo ""

# Step 7: Create hard link
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 6: Creating hard link${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

"$LOLELFFS" ln -i "$IMAGE_FILE" /data/test.txt /data/test_hardlink.txt
echo -e "${GREEN}Hard link created: /data/test_hardlink.txt${NC}"
echo ""

# Step 8: List all contents
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 7: Listing all contents${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

echo "Root directory:"
"$LOLELFFS" ls -i "$IMAGE_FILE" / -l
echo ""

echo "Directory /config:"
"$LOLELFFS" ls -i "$IMAGE_FILE" /config -l
echo ""

echo "Directory /data:"
"$LOLELFFS" ls -i "$IMAGE_FILE" /data -l
echo ""

echo "Directory /logs:"
"$LOLELFFS" ls -i "$IMAGE_FILE" /logs -l
echo ""

echo "Directory /logs/archive:"
"$LOLELFFS" ls -i "$IMAGE_FILE" /logs/archive -l
echo ""

# Step 9: Read files
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 8: Reading files${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

echo -e "${YELLOW}Contents of /config/app.conf:${NC}"
"$LOLELFFS" cat -i "$IMAGE_FILE" /config/app.conf
echo ""

echo -e "${YELLOW}Contents of /data/test.txt:${NC}"
"$LOLELFFS" cat -i "$IMAGE_FILE" /data/test.txt
echo ""

echo -e "${YELLOW}Contents of /logs/app.log:${NC}"
"$LOLELFFS" cat -i "$IMAGE_FILE" /logs/app.log
echo ""

# Step 10: Follow symbolic link
echo -e "${YELLOW}Symbolic link /config/test_link:${NC}"
"$LOLELFFS" stat -i "$IMAGE_FILE" /config/test_link
echo ""
echo "Reading through symlink:"
"$LOLELFFS" cat -i "$IMAGE_FILE" /config/test_link
echo ""

# Step 11: Show file stats
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 9: File statistics${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

echo "Statistics for /data/test.txt:"
"$LOLELFFS" stat -i "$IMAGE_FILE" /data/test.txt
echo ""

echo "Statistics for /config/app.conf:"
"$LOLELFFS" stat -i "$IMAGE_FILE" /config/app.conf
echo ""

# Step 12: Set extended attributes
# NOTE: Extended attributes currently have a bug in lolelffs, so this section is commented out
# Uncomment when the xattr implementation is fixed
#echo -e "${BLUE}═══════════════════════════════════════════${NC}"
#echo -e "${BLUE}Step 10: Extended attributes${NC}"
#echo -e "${BLUE}═══════════════════════════════════════════${NC}"
#
#echo "Setting extended attributes..."
#"$LOLELFFS" setfattr -i "$IMAGE_FILE" /config/app.conf --name user.comment --value "Main configuration file"
#"$LOLELFFS" setfattr -i "$IMAGE_FILE" /data/test.txt --name user.author --value "lolelffs CLI"
#
#echo -e "${GREEN}Extended attributes set${NC}"
#echo ""
#
#echo "Reading extended attributes:"
#"$LOLELFFS" getfattr -i "$IMAGE_FILE" /config/app.conf user.comment
#echo ""
#"$LOLELFFS" getfattr -i "$IMAGE_FILE" /data/test.txt user.author
#echo ""
#
#echo "Listing all extended attributes:"
#"$LOLELFFS" listxattr -i "$IMAGE_FILE" /config/app.conf
#"$LOLELFFS" listxattr -i "$IMAGE_FILE" /data/test.txt
#echo ""

# Step 13: Copy file from host
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 10: Copying file from host${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

# Create a temp file to copy
echo "test data from host" > /tmp/host_file_$$.txt
"$LOLELFFS" cp -i "$IMAGE_FILE" /tmp/host_file_$$.txt /config/from_host.txt
rm -f /tmp/host_file_$$.txt
echo -e "${GREEN}File copied from host: /tmp/host_file_$$.txt -> /config/from_host.txt${NC}"
echo ""

echo "Verifying copy:"
"$LOLELFFS" ls -i "$IMAGE_FILE" /config -l
echo ""
"$LOLELFFS" cat -i "$IMAGE_FILE" /config/from_host.txt
echo ""

# Step 14: Show filesystem usage
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 12: Filesystem usage${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

"$LOLELFFS" df -i "$IMAGE_FILE"
echo ""

# Step 15: Touch file (create empty file)
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 13: Creating empty file${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

"$LOLELFFS" touch -i "$IMAGE_FILE" /data/empty.txt
echo -e "${GREEN}Empty file created: /data/empty.txt${NC}"
echo ""

echo "Verifying empty file:"
"$LOLELFFS" stat -i "$IMAGE_FILE" /data/empty.txt
echo ""

# Step 16: Filesystem check
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 14: Filesystem integrity check${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

"$LOLELFFS" fsck "$IMAGE_FILE"
echo ""

# Step 17: Extract files
echo -e "${BLUE}═══════════════════════════════════════════${NC}"
echo -e "${BLUE}Step 15: Extracting files${NC}"
echo -e "${BLUE}═══════════════════════════════════════════${NC}"

EXTRACT_DIR="extracted_$$"
mkdir -p "$EXTRACT_DIR"

# Extract individual files
"$LOLELFFS" extract -i "$IMAGE_FILE" /config/app.conf "$EXTRACT_DIR/app.conf"
"$LOLELFFS" extract -i "$IMAGE_FILE" /data/test.txt "$EXTRACT_DIR/test.txt"
echo -e "${GREEN}Files extracted to $EXTRACT_DIR/${NC}"
echo ""

echo "Extracted files:"
ls -l "$EXTRACT_DIR"
echo ""

echo "Comparing extracted file:"
echo -e "${YELLOW}Extracted /config/app.conf:${NC}"
cat "$EXTRACT_DIR/app.conf"
echo ""

# Cleanup extracted files
rm -rf "$EXTRACT_DIR"

# Success summary
echo ""
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo -e "${GREEN}SUCCESS: CLI demonstration complete${NC}"
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo ""
echo "Commands demonstrated:"
echo "  ✓ mkfs      - Create filesystem"
echo "  ✓ super     - Show superblock"
echo "  ✓ mkdir     - Create directories"
echo "  ✓ write     - Write files"
echo "  ✓ cat       - Read files"
echo "  ✓ ls        - List contents"
echo "  ✓ ln        - Create symbolic and hard links"
echo "  ✓ stat      - File statistics (including symlinks)"
echo "  ✓ cp        - Copy files"
echo "  ✓ touch     - Create empty files"
echo "  ✓ df        - Filesystem usage"
echo "  ✓ fsck      - Filesystem check"
echo "  ✓ extract   - Extract files"
echo ""
echo "Note: Extended attributes (setfattr/getfattr/listxattr) are available"
echo "      but currently have bugs, so they're not demonstrated here."
echo ""
echo "Key advantages of CLI tools:"
echo "  • No root access required"
echo "  • No mounting necessary"
echo "  • Works on any lolelffs image"
echo "  • Safe for scripting and automation"
echo ""
echo "Next steps:"
echo "  - Try ./encrypted-workflow.sh for encryption"
echo "  - Try ./basic-mount.sh for kernel mounting (requires root)"
echo "  - Try ./backup-script.sh for real-world backup example"
echo ""

if [ "$KEEP_IMAGE" = true ]; then
    echo "Image file preserved:"
    ls -lh "$IMAGE_FILE"
    echo ""
    echo "Explore it with:"
    echo "  $LOLELFFS ls -i $IMAGE_FILE / -l"
    echo "  $LOLELFFS cat -i $IMAGE_FILE /config/app.conf"
    echo ""
fi
