#!/bin/bash

# backup-script.sh - Real-world backup example with lolelffs
#
# Usage: ./backup-script.sh [source_dir] [output.img]
#        ./backup-script.sh --restore [input.img] [dest_dir]

set -e
set -u

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOLELFFS="$PROJECT_ROOT/lolelffs"
PASSWORD="backup_password_2024"

usage() {
    echo "Usage:"
    echo "  Backup:  $0 <source_dir> <output.img>"
    echo "  Restore: $0 --restore <input.img> <dest_dir>"
    exit 1
}

backup() {
    local src="$1"
    local img="$2"

    echo -e "${BLUE}Creating encrypted backup${NC}"
    "$LOLELFFS" mkfs --size 50M --encrypt --algo aes-256-xts --password "$PASSWORD" "$img"

    echo -e "${BLUE}Backing up $src${NC}"
    find "$src" -type f | while read -r file; do
        rel_path="${file#$src}"
        dir=$(dirname "$rel_path")
        [ "$dir" != "." ] && "$LOLELFFS" mkdir -i "$img" --password "$PASSWORD" "$dir" -p 2>/dev/null || true
        "$LOLELFFS" write -i "$img" --password "$PASSWORD" "$rel_path" -f "$file" --create
        echo "  Backed up: $rel_path"
    done

    echo -e "${GREEN}Backup complete: $img${NC}"
    "$LOLELFFS" df -i "$img" --password "$PASSWORD"
}

restore() {
    local img="$1"
    local dest="$2"

    echo -e "${BLUE}Restoring from $img to $dest${NC}"
    mkdir -p "$dest"
    "$LOLELFFS" extract -i "$img" --password "$PASSWORD" / "$dest/"
    echo -e "${GREEN}Restore complete${NC}"
}

[ $# -lt 2 ] && usage

if [ "$1" = "--restore" ]; then
    [ $# -ne 3 ] && usage
    restore "$2" "$3"
else
    [ $# -ne 2 ] && usage
    backup "$1" "$2"
fi
