# lolelffs

**lolelffs** (LOL ELF FileSystem) is a Linux kernel filesystem module that enables creating fully functional filesystems within ELF binary files. Based on simplefs and compliant with the ELF specification, it allows a single binary to serve as both an executable program and a mountable filesystem.

## Table of Contents

- [Overview](#overview)
- [Use Cases](#use-cases)
- [Features](#features)
- [Architecture](#architecture)
- [Building](#building)
- [Usage](#usage)
- [Rust CLI Tools](#rust-cli-tools)
- [Testing and Benchmarks](#testing-and-benchmarks)
- [Technical Details](#technical-details)
- [Performance Considerations](#performance-considerations)
- [Limitations](#limitations)
- [Contributing](#contributing)
- [License](#license)
- [References](#references)

## Overview

lolelffs embeds a complete filesystem within a special ELF section (`.lolfs.super`), allowing you to:

- Create filesystems on regular image files or ELF binaries
- Mount and use them like any standard Linux filesystem
- Distribute self-contained binaries with embedded data
- Use modern Rust CLI tools for filesystem manipulation without kernel module overhead

The magic number `0x101E1FF5` encodes "lolelffs" in hexspeak, reflecting the project's playful origins while providing serious filesystem functionality.

## Use Cases

### 1. Self-Contained Application Distribution

Package an application with all its configuration files, assets, and data in a single executable:

```bash
# Create your application binary
gcc -o myapp main.c

# Embed a filesystem with configuration and assets
truncate -s 10M myapp
./mkfs.lolelffs myapp

# Mount and add files
sudo mount -t lolelffs -o loop myapp /mnt/lolelffs
sudo cp -r config/ assets/ /mnt/lolelffs/
sudo umount /mnt/lolelffs

# Distribute the single binary containing everything
```

**Benefits**: Single-file deployment, no external dependencies, versioned assets bundled with code.

### 2. Embedded Systems and IoT

Ideal for resource-constrained environments where a full filesystem stack is too heavy:

- **Single-binary firmware**: Embed configuration, certificates, and initial data
- **Read-only data storage**: Ship devices with pre-configured filesystems
- **Reduced attack surface**: No need for complex filesystem drivers
- **Predictable memory footprint**: Fixed block sizes and extent-based allocation

### 3. Container and Microservice Initialization

Bootstrap containers with embedded initialization data:

```bash
# Create an init binary with embedded filesystem
cp /bin/busybox init
truncate -s 5M init
./mkfs.lolelffs init

# Add initialization scripts and config
lolelffs write -i init /init.sh -c '#!/bin/sh
echo "Initializing container..."
exec /app'
```

### 4. Educational and Research Purposes

Learn filesystem internals with a simple, well-documented implementation:

- **Filesystem design**: Study extent-based allocation, inode management, bitmap tracking
- **Kernel module development**: Understand VFS integration and kernel APIs
- **ELF format exploration**: See how custom sections can extend binary functionality
- **Systems programming**: Bridge userspace tools (Rust) with kernel modules (C)

### 5. Secure Data Bundling

Embed sensitive configuration or secrets within executables:

- Certificates and keys for TLS applications
- License files for commercial software
- Encrypted configuration data
- API tokens and credentials (for development/testing)

### 6. Portable Development Environments

Create self-contained development tools with embedded resources:

```bash
# Bundle a compiler with its standard library
cp /usr/bin/gcc my-gcc
truncate -s 50M my-gcc
./mkfs.lolelffs my-gcc
# Add headers, libraries, etc.
```

### 7. CTF Challenges and Security Research

Create interesting reverse engineering challenges:

- Hide flags or data within filesystem structures
- Create multi-layer challenges (execute + mount + explore)
- Study filesystem forensics techniques

### 8. Backup and Archival

Create portable filesystem snapshots:

```bash
# Create a backup archive
dd if=/dev/zero of=backup.img bs=1M count=500
./mkfs.lolelffs backup.img

# Use Rust tools to add files without mounting
lolelffs cp -i backup.img /path/to/important/files /backup/
```

### 9. Plugin and Extension Systems

Build applications that load functionality from embedded filesystems:

- Game engines with embedded asset filesystems
- IDEs with bundled plugins and themes
- Media players with embedded codec libraries

### 10. Testing and CI/CD Pipelines

Create reproducible test environments:

```bash
# Create test fixtures embedded in test binary
./mkfs.lolelffs test_runner
lolelffs mkdir -i test_runner /fixtures -p
lolelffs write -i test_runner /fixtures/test_data.json -c '{"test": true}'
```

## Features

### Core Capabilities

| Feature | Description | Limits |
|---------|-------------|--------|
| **ELF-compliant** | Filesystems can exist within valid ELF binaries | N/A |
| **Extent-based allocation** | Efficient storage with contiguous block ranges | Up to 340 extents per file |
| **POSIX operations** | Files, directories, hard links, symbolic links | Full support |
| **Permissions** | Standard Unix mode, uid, gid | rwx for user/group/other |
| **Timestamps** | atime, mtime, ctime | Unix seconds |

### Filesystem Limits

| Metric | Value |
|--------|-------|
| Block size | 4 KB (fixed) |
| Maximum file size | ~11 MB |
| Maximum filename | 255 characters |
| Maximum files per directory | 40,920 |
| Inodes per block | 56 |
| Blocks per extent | 8 |

### Tooling

- **Kernel module**: Full VFS integration for mounting
- **mkfs utility**: Create filesystems with ELF detection
- **fsck utility**: Filesystem consistency checking
- **Rust CLI tools**: Complete userspace filesystem manipulation

## Architecture

### Project Structure

```
lolelffs/
├── Kernel Module (C)
│   ├── fs.c              # Module init/mount/unmount
│   ├── super.c           # Superblock operations
│   ├── inode.c           # Inode caching & VFS integration
│   ├── file.c            # File read/write operations
│   ├── dir.c             # Directory operations
│   ├── extent.c          # Extent search (binary search optimized)
│   ├── bitmap.h          # Bitmap manipulation
│   └── lolelffs.h        # Core data structures
│
├── Utilities (C)
│   ├── mkfs.c            # Create filesystems
│   └── fsck.lolelffs.c   # Filesystem checker
│
├── Rust CLI Tools
│   └── lolelffs-tools/
│       └── src/
│           ├── main.rs   # 15+ CLI commands
│           ├── fs.rs     # Core filesystem operations
│           ├── types.rs  # Data structures
│           ├── dir.rs    # Directory operations
│           ├── file.rs   # File operations
│           ├── bitmap.rs # Allocation management
│           └── lib.rs    # Library exports
│
└── Tests
    └── test/
        ├── test_unit.c       # Structure validation
        ├── test_mkfs.c       # Creation tests
        ├── test_benchmark.c  # Performance tests
        ├── test_stress.c     # Edge case testing
        └── test.sh           # Integration tests
```

### Filesystem Layout

```
+---------------+
|  superblock   |  1 block (4 KB)
+---------------+
|  inode store  |  sb->nr_istore_blocks
+---------------+
| ifree bitmap  |  sb->nr_ifree_blocks
+---------------+
| bfree bitmap  |  sb->nr_bfree_blocks
+---------------+
|    data       |
|    blocks     |  remaining blocks
+---------------+
```

## Building

### Prerequisites

- Linux kernel headers
- libelf development library
- GCC
- Rust toolchain (for CLI tools)

**Debian/Ubuntu:**
```bash
sudo apt-get install build-essential linux-headers-$(uname -r) libelf-dev
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

**Fedora/RHEL:**
```bash
sudo dnf install kernel-devel elfutils-libelf-devel gcc make
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

**Arch Linux:**
```bash
sudo pacman -S linux-headers libelf base-devel rust
```

### Compile

```bash
# Build everything (kernel module, mkfs, Rust tools)
make

# Build only the kernel module and mkfs
make all

# Build only the Rust CLI tools
make rust-tools

# Build Rust tools in debug mode
make rust-tools-debug

# Clean build artifacts
make clean
```

Build outputs:
- `lolelffs.ko` - Kernel module
- `mkfs.lolelffs` - Filesystem creation utility
- `fsck.lolelffs` - Filesystem checker
- `lolelffs-tools/target/release/lolelffs` - Rust CLI

## Usage

### Creating a Filesystem

**On a regular file:**
```bash
# Create a 100MB filesystem image
dd if=/dev/zero of=myfs.img bs=1M count=100
./mkfs.lolelffs myfs.img
```

**On an ELF binary:**
```bash
# Copy an existing binary
cp /bin/ls myelf.bin

# Pad to minimum size (must be at least 400KB)
truncate -s 1M myelf.bin

# Create filesystem in the binary
./mkfs.lolelffs myelf.bin
```

### Mounting the Filesystem

```bash
# Load the kernel module
sudo insmod lolelffs.ko

# Create mount point and mount
mkdir -p /mnt/lolelffs
sudo mount -t lolelffs -o loop myfs.img /mnt/lolelffs

# Use the filesystem
sudo cp file.txt /mnt/lolelffs/
ls /mnt/lolelffs/

# Unmount when done
sudo umount /mnt/lolelffs
sudo rmmod lolelffs
```

### Checking Filesystem Integrity

```bash
./fsck.lolelffs myfs.img
```

## Rust CLI Tools

The Rust CLI provides complete filesystem manipulation without requiring the kernel module. This is ideal for development, scripting, and environments where kernel modules cannot be loaded.

### Installation

```bash
# Build and install to PATH
make rust-tools
sudo cp lolelffs-tools/target/release/lolelffs /usr/local/bin/
```

### Commands

#### Filesystem Information

```bash
# Show superblock information
lolelffs super -i image.img

# Show filesystem usage (like df)
lolelffs df -i image.img
lolelffs df -i image.img -H    # Human-readable sizes
```

#### File Operations

```bash
# List directory contents
lolelffs ls -i image.img /
lolelffs ls -i image.img / -l    # Long format
lolelffs ls -i image.img / -a    # Show all (including . and ..)

# Read file contents
lolelffs cat -i image.img /path/to/file.txt

# Write content to a file
lolelffs write -i image.img /file.txt -c "Hello, World!"

# Copy file from host to filesystem
lolelffs cp -i image.img /host/path/file.txt /fs/path/file.txt

# Extract file from filesystem to host
lolelffs extract -i image.img /fs/path/file.txt /host/destination/

# Get file/directory information
lolelffs stat -i image.img /path/to/file
```

#### Directory Operations

```bash
# Create directory
lolelffs mkdir -i image.img /newdir

# Create nested directories
lolelffs mkdir -i image.img /path/to/nested/dir -p

# Remove file or empty directory
lolelffs rm -i image.img /path/to/file
```

#### Link Operations

```bash
# Create hard link
lolelffs ln -i image.img /target /link

# Create symbolic link
lolelffs ln -i image.img /target /link -s

# Read symbolic link target
lolelffs readlink -i image.img /symlink
```

#### Filesystem Creation

```bash
# Create a new filesystem
lolelffs mkfs --size 100M output.img

# Create with specific block count
lolelffs mkfs --blocks 25600 output.img
```

#### Debugging

```bash
# Dump raw inode data
lolelffs debug-inode -i image.img 1
```

### Example Workflow

```bash
# Create a new filesystem
lolelffs mkfs --size 50M myfs.img

# Create directory structure
lolelffs mkdir -i myfs.img /config -p
lolelffs mkdir -i myfs.img /data -p
lolelffs mkdir -i myfs.img /logs -p

# Add configuration file
lolelffs write -i myfs.img /config/app.conf -c "
server_port=8080
log_level=info
data_dir=/data
"

# Copy data files from host
lolelffs cp -i myfs.img ~/project/data/initial.json /data/initial.json

# Verify contents
lolelffs ls -i myfs.img / -l
lolelffs cat -i myfs.img /config/app.conf
lolelffs df -i myfs.img -H
```

## Testing and Benchmarks

### Unit Tests

Run tests that don't require root:

```bash
make test
```

Tests included:
- `test_unit` - Validates constants, structures, and calculations
- `test_mkfs` - Tests filesystem creation utility

### Performance Benchmarks

```bash
make benchmark
```

Measures:
- Extent search performance with binary search
- Bitmap operations throughput
- Directory entry calculations
- Memory layout efficiency

### Stress Tests

```bash
make stress
```

Tests edge cases:
- Large file extent patterns
- Maximum directory capacity
- Pathological access patterns
- Full extent tree traversal

### Integration Tests

Requires root for kernel module operations:

```bash
make test-integration
```

### Run All Tests

```bash
make test-all
```

### Create Test Image

```bash
make test-image
```

Creates a 200MB test filesystem at `test.img`.

## Technical Details

### Data Structures

#### Superblock (32 bytes)

```c
struct lolelffs_sb_info {
    uint32_t magic;             // 0x101E1FF5
    uint32_t nr_blocks;         // Total blocks
    uint32_t nr_inodes;         // Total inodes
    uint32_t nr_istore_blocks;  // Inode store blocks
    uint32_t nr_ifree_blocks;   // Inode bitmap blocks
    uint32_t nr_bfree_blocks;   // Block bitmap blocks
    uint32_t nr_free_inodes;    // Available inodes
    uint32_t nr_free_blocks;    // Available blocks
};
```

#### Inode (72 bytes)

```c
struct lolelffs_inode {
    uint32_t i_mode;    // File type + permissions
    uint32_t i_uid;     // Owner user ID
    uint32_t i_gid;     // Owner group ID
    uint32_t i_size;    // Size in bytes
    uint32_t i_ctime;   // Change time
    uint32_t i_atime;   // Access time
    uint32_t i_mtime;   // Modification time
    uint32_t i_blocks;  // Block count
    uint32_t i_nlink;   // Hard link count
    uint32_t ei_block;  // Extent index block
    char i_data[32];    // Symlink target or reserved
};
```

#### Extent (12 bytes)

```c
struct lolelffs_extent {
    uint32_t ee_block;  // First logical block
    uint32_t ee_len;    // Number of blocks
    uint32_t ee_start;  // First physical block
};
```

#### Directory Entry (259 bytes)

```c
struct lolelffs_file {
    uint32_t inode;         // Inode number
    char filename[255];     // Null-terminated filename
};
```

### Key Calculations

```
BLOCK_SIZE = 4096 bytes
INODES_PER_BLOCK = 4096 / 72 = 56
MAX_EXTENTS = (4096 - 4) / 12 = 340
BLOCKS_PER_EXTENT = 8
FILES_PER_BLOCK = 4096 / 259 = 15
MAX_FILESIZE = 8 × 4096 × 340 = 11,010,048 bytes (~10.5 MB)
MAX_FILES_PER_DIR = 15 × 340 × 8 = 40,920
```

### ELF Integration

When creating a filesystem on an ELF binary, mkfs:

1. Validates the ELF magic number
2. Parses the ELF header to find the end of program data
3. Places the filesystem after the ELF content
4. Creates a `.lolfs.super` section reference
5. The binary remains executable while also being mountable

## Performance Considerations

### Extent Search Optimization

The extent search uses binary search with locality hints:

- O(log n) lookup for random access
- O(1) for sequential access with hint caching
- Optimized for both large files and many small extents

### Block Allocation

- Greedy allocation attempts to extend existing extents
- Bitmap scanning for free block discovery
- Consecutive block preference for new extents

### Memory Usage

- Kernel module uses slab caching for inodes
- Rust tools use memory-mapped I/O where beneficial
- Fixed-size structures enable predictable memory footprint

### Recommended Use Cases by Size

| Image Size | Recommended Use |
|------------|-----------------|
| < 1 MB | Configuration files, small data |
| 1-10 MB | Application assets, embedded data |
| 10-100 MB | Development environments, test fixtures |
| 100+ MB | Archives, large datasets |

## Limitations

- **Block size**: Fixed at 4 KB (cannot be changed)
- **Maximum file size**: ~11 MB per file
- **No extended attributes**: xattr not supported
- **No journaling**: Not crash-safe
- **No encryption**: Data stored in plaintext
- **No compression**: Files stored uncompressed
- **Single-threaded mkfs**: Large images take time to create
- **No resize**: Cannot grow or shrink existing filesystems

## Contributing

Contributions are welcome! Areas of interest:

### Feature Ideas

- Add journaling for crash recovery
- Implement file compression (zlib, lz4)
- Add encryption support
- Support extended attributes
- Implement filesystem resize
- Add FUSE support for non-root mounting

### Development Setup

```bash
# Clone the repository
git clone https://github.com/hodgesds/lolelffs.git
cd lolelffs

# Build in debug mode
make rust-tools-debug

# Run tests
make test-all

# Create test filesystem for experimentation
make test-image
```

### Code Style

- C code follows Linux kernel style
- Rust code uses standard rustfmt formatting
- All new features should include tests

### Testing Requirements

- Unit tests for new functionality
- Integration tests for kernel module changes
- Benchmark tests for performance-sensitive code

## License

Dual BSD/GPL

## Author

Daniel Hodges

## References

- Based on [simplefs](https://github.com/sysprog21/simplefs)
- [Linux VFS documentation](https://www.kernel.org/doc/html/latest/filesystems/vfs.html)
- [ELF Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf)
- [Extent-based filesystems](https://en.wikipedia.org/wiki/Extent_(file_systems))
