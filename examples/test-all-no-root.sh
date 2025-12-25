#!/bin/bash

# test-all-no-root.sh - Test all examples that don't require root access
#
# This script runs through all examples that can be executed without root privileges

set -u

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

FAILED=0
PASSED=0

test_example() {
    local name="$1"
    local command="$2"

    echo ""
    echo -e "${BLUE}╔═══════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║  Testing: $name${NC}"
    echo -e "${BLUE}╚═══════════════════════════════════════════════════════╝${NC}"
    echo ""

    if eval "$command"; then
        echo -e "${GREEN}✓ $name PASSED${NC}"
        ((PASSED++))
    else
        echo -e "${RED}✗ $name FAILED${NC}"
        ((FAILED++))
    fi
}

echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}Testing lolelffs Examples (No Root Required)${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"

# Test 1: Shell Scripts - CLI Tools
test_example \
    "Shell Script: create-and-populate.sh" \
    "cd 01-shell-scripts && ./create-and-populate.sh >/dev/null 2>&1 && cd .."

# Test 2: Shell Scripts - Encryption
test_example \
    "Shell Script: encrypted-workflow.sh" \
    "cd 01-shell-scripts && ./encrypted-workflow.sh >/dev/null 2>&1 && cd .."

# Test 3: ELF Embedding - Hello World
test_example \
    "ELF Embedding: hello-world" \
    "cd 02-elf-embedding/hello-world && make clean >/dev/null 2>&1 && make >/dev/null 2>&1 && ./hello-with-fs >/dev/null && cd ../.."

# Test 4: Linker Scripts - Basic Section
test_example \
    "Linker Script: basic-section" \
    "cd 04-linker-scripts/basic-section && make clean >/dev/null 2>&1 && make >/dev/null 2>&1 && cd ../.."

# Test 5: Linker Scripts - Aligned Section
test_example \
    "Linker Script: aligned-section" \
    "cd 04-linker-scripts/aligned-section && make clean >/dev/null 2>&1 && make >/dev/null 2>&1 && ./example >/dev/null && cd ../.."

# Test 6: C Programmatic Examples
test_example \
    "C Programmatic: read-superblock" \
    "cd 03-programmatic/c-examples && make clean >/dev/null 2>&1 && make test >/dev/null 2>&1 && cd ../.."

# Summary
echo ""
echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}Test Summary${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"
echo ""
echo -e "  ${GREEN}Passed: $PASSED${NC}"
echo -e "  ${RED}Failed: $FAILED${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
