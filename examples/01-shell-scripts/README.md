# Shell Script Examples

This directory contains shell scripts demonstrating common lolelffs workflows using existing command-line tools.

## Scripts Overview

| Script | Description | Requires Root |
|--------|-------------|---------------|
| `create-and-populate.sh` | Use Rust CLI tools (recommended starting point) | No |
| `basic-mount.sh` | Mount/unmount with kernel module | Yes |
| `fuse-mount.sh` | Mount with FUSE driver | No |
| `encrypted-workflow.sh` | Create and use encrypted filesystems | No |
| `backup-script.sh` | Real-world backup/restore example | No |
| `dual-purpose-binary.sh` | Create executable+mountable binary | No |

## Quick Start

**Recommended first script** (no root required):
```bash
./create-and-populate.sh
```

This script demonstrates using the Rust CLI tools to create and manipulate filesystems without mounting.

## Prerequisites

- lolelffs built in parent directory (`make` and `make rust` from `../`)
- For kernel module scripts: root access
- For FUSE scripts: fusermount installed
- For encryption scripts: no additional requirements

## Usage

All scripts can be run with `-h` or `--help` for usage information:

```bash
./create-and-populate.sh --help
```

Most scripts are self-contained and will clean up after themselves.

## Script Descriptions

### create-and-populate.sh

Demonstrates Rust CLI tool usage without requiring mounting. This is the **recommended starting point** as it requires no special privileges.

**What it does:**
- Creates a filesystem image
- Creates directories
- Writes files
- Lists contents
- Reads files back
- Shows filesystem statistics

**Example:**
```bash
./create-and-populate.sh
```

### basic-mount.sh

Shows the full kernel module workflow for mounting lolelffs.

**What it does:**
- Loads kernel module with dependencies
- Creates filesystem image
- Mounts using `mount -t lolelffs`
- Performs file operations through normal Linux VFS
- Unmounts and cleans up

**Requires:** Root access

**Example:**
```bash
sudo ./basic-mount.sh
```

### fuse-mount.sh

Demonstrates FUSE-based mounting without root privileges.

**What it does:**
- Builds FUSE driver (if needed)
- Mounts filesystem using lolelffs-fuse
- Performs file operations
- Unmounts with fusermount

**Example:**
```bash
./fuse-mount.sh
```

### encrypted-workflow.sh

Shows how to create and use encrypted filesystems with passwords.

**What it does:**
- Creates encrypted filesystem (AES-256-XTS)
- Writes encrypted files with password
- Reads encrypted files
- Demonstrates wrong password handling
- Shows on-disk encryption verification

**Example:**
```bash
./encrypted-workflow.sh
```

### backup-script.sh

Real-world example of using lolelffs for backups.

**What it does:**
- Creates compressed, encrypted backup image
- Copies directory tree into filesystem
- Verifies backup integrity
- Demonstrates restoration

**Usage:**
```bash
./backup-script.sh /path/to/source output.img
./backup-script.sh --restore output.img /path/to/dest
```

### dual-purpose-binary.sh

Shows how to create binaries that are both executable and mountable.

**What it does:**
- Copies an existing binary
- Creates filesystem on the binary
- Populates filesystem
- Verifies binary still executes
- Shows how to mount the binary

**Example:**
```bash
./dual-purpose-binary.sh
```

## Common Patterns

### Error Handling

All scripts include error checking:

```bash
set -e  # Exit on error
set -u  # Exit on undefined variable

# Cleanup on exit
trap cleanup EXIT
```

### Path Resolution

Scripts handle relative paths to lolelffs tools:

```bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOLELFFS="$PROJECT_ROOT/../lolelffs"
```

### Temporary Files

Scripts use temporary directories that are automatically cleaned up:

```bash
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT
```

## Troubleshooting

### "lolelffs: command not found"

Build the Rust CLI tools first:
```bash
cd ../..
make rust
```

### "lolelffs.ko: No such file"

Build the kernel module:
```bash
cd ../..
make
```

### "Operation not permitted" (mount)

Kernel module mounting requires root:
```bash
sudo ./basic-mount.sh
```

Or use FUSE instead:
```bash
./fuse-mount.sh
```

### "Module has invalid ELF structures"

Kernel module was built for a different kernel version. Rebuild:
```bash
cd ../..
make clean
make
```

## Extending Examples

These scripts serve as templates for your own use cases. Common modifications:

1. **Different filesystem sizes:** Modify `--size` parameter
2. **Different encryption:** Try `chacha20-poly1305` instead of `aes256-xts`
3. **Different compression:** Try `lz4`, `zlib`, or `zstd`
4. **Different file structures:** Modify directory layouts and file contents

## Related Examples

- **ELF Embedding:** See `../02-elf-embedding/` for programmatic embedding
- **Linker Scripts:** See `../04-linker-scripts/` for custom ELF layouts
- **Programmatic:** See `../03-programmatic/` for API usage

## References

- Main README: `../../README.md`
- CLI tool help: `../../lolelffs --help`
- Encryption details: `../../ENCRYPTION.md`
