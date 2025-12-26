# lolelffs Examples

This directory contains comprehensive examples demonstrating all aspects of **lolelffs** (LOL ELF FileSystem) usage. lolelffs is a Linux filesystem that can be embedded within ELF binary files, creating dual-purpose files that are both executable and mountable as filesystems.

## Quick Start

```bash
# Build all examples
make

# Build specific category
make shell-scripts
make elf-embedding
make programmatic
make linker-scripts

# Clean everything
make clean
```

## Prerequisites

### Required
- **Linux kernel headers** - For kernel module examples
- **GCC/Clang** - C compiler
- **GNU binutils** - objcopy, readelf, ld
- **Rust toolchain** - For Rust examples and CLI tools
- **Make** - Build automation

### Optional
- **Root access** - For kernel module mounting examples
- **FUSE** - For userspace mounting (fusermount)
- **Docker** - For container integration examples
- **systemd** - For service integration examples

### Building lolelffs

Before running examples, build the main lolelffs project:

```bash
cd ..
make              # Build kernel module and C utilities
make rust         # Build Rust CLI tools
make fuse         # (Optional) Build FUSE driver
```

## Examples Overview

### 01-shell-scripts/ - Shell Script Examples

Shell scripts demonstrating common lolelffs workflows using existing tools.

| Example | Description |
|---------|-------------|
| `basic-mount.sh` | Mount/unmount with kernel module |
| `fuse-mount.sh` | Mount with FUSE driver (no root) |
| `create-and-populate.sh` | Use Rust CLI tools (mkfs, write, ls, etc.) |
| `encrypted-workflow.sh` | Create and use encrypted filesystems |
| `backup-script.sh` | Real-world backup/restore example |
| `dual-purpose-binary.sh` | Create executable+mountable binary |

**Quick start:**
```bash
cd 01-shell-scripts
./create-and-populate.sh
```

### 02-elf-embedding/ - ELF Binaries with Embedded Filesystems

Examples of executables that contain lolelffs filesystems within them.

| Example | Description |
|---------|-------------|
| `hello-world/` | Simple program with embedded config file |
| `self-extracting/` | Self-extracting archive binary |
| `game-with-assets/` | Game with embedded sprite/sound files |
| `config-launcher/` | Application reading its own embedded config |

**Quick start:**
```bash
cd 02-elf-embedding/hello-world
make
./hello-with-fs
```

### 03-programmatic/ - C/Rust API Usage

Examples showing programmatic interaction with lolelffs structures and APIs.

**C Examples:**
- `read-superblock.c` - Parse superblock structure
- `direct-inode-access.c` - Direct inode manipulation
- `extent-walker.c` - Walk extent trees
- `bitmap-operations.c` - Bitmap manipulation

**Rust Examples:**
- `fs-builder.rs` - Build filesystem programmatically
- `fs-analyzer.rs` - Analyze filesystem structure
- `encrypted-fs.rs` - Encryption API usage
- `compression-demo.rs` - Compression examples

**Quick start:**
```bash
cd 03-programmatic/c-examples
make
./read-superblock ../../test.img

cd ../rust-examples
cargo run --bin fs-builder
```

### 04-linker-scripts/ - Custom Linker Scripts

Custom LD linker scripts for fine-grained control of ELF section placement.

| Example | Description |
|---------|-------------|
| `basic-section/` | Minimal `.lolfs.super` section placement |
| `multi-section/` | Multiple filesystem sections in one binary |
| `aligned-section/` | Page-aligned section for efficient mmap |
| `custom-placement/` | Fine-grained segment control |

**Quick start:**
```bash
cd 04-linker-scripts/basic-section
./build.sh
readelf -S example | grep lolfs
```

### 05-advanced-use-cases/ - Real-World Scenarios

Production-ready examples demonstrating real-world use cases.

| Example | Description |
|---------|-------------|
| `container-init/` | PID 1 init with embedded startup scripts |
| `plugin-system/` | Dynamic plugin loading from embedded FS |
| `encrypted-backup/` | Secure backup/restore system |
| `portable-dev-env/` | Self-contained development tools |
| `ctf-challenge/` | Security challenge with hidden flag |

**Quick start:**
```bash
cd 05-advanced-use-cases/encrypted-backup
./backup.sh /path/to/source backup.img
```

### 06-integration/ - Integration Examples

Integration with modern development and deployment tools.

| Example | Description |
|---------|-------------|
| `docker/` | Docker container with embedded filesystem init |
| `systemd/` | systemd service/timer units for mounting |
| `ci-cd/` | GitHub Actions and GitLab CI examples |

**Quick start:**
```bash
cd 06-integration/docker
docker build -t lolelffs-demo .
docker run lolelffs-demo
```

## Key Concepts

### ELF Embedding

lolelffs can be embedded in ELF binaries using the `.lolfs.super` section:

```bash
# Method 1: Using objcopy
objcopy --add-section .lolfs.super=fs.img binary binary

# Method 2: Using custom linker script
gcc -T custom.ld source.c -o binary
```

### Filesystem Operations

Three ways to interact with lolelffs:

1. **Kernel Module** (requires root):
   ```bash
   sudo insmod lolelffs.ko
   sudo mount -t lolelffs -o loop image.img /mnt/lolelffs
   ```

2. **FUSE Driver** (no root required):
   ```bash
   lolelffs-fuse image.img /mnt/lolelffs
   ```

3. **Userspace CLI** (no mount needed):
   ```bash
   lolelffs ls -i image.img /
   lolelffs cat -i image.img /file.txt
   lolelffs write -i image.img /new.txt -d "content"
   ```

### Encryption

lolelffs supports AES-256-XTS and ChaCha20-Poly1305 encryption:

```bash
# Create encrypted filesystem
lolelffs mkfs --size 10M --encrypt aes256-xts encrypted.img

# Password-based operations
lolelffs unlock --password mypassword encrypted.img
lolelffs write -i encrypted.img --password mypassword /secret.txt -d "data"
```

### Compression

Per-extent compression with LZ4, zlib, or zstd:

```bash
# Create filesystem with compression
lolelffs mkfs --size 10M --compress lz4 compressed.img
lolelffs write -i compressed.img /large.txt -f source.txt
```

## Filesystem Structure

```
[Superblock 4KB] → [Inode store] → [Inode bitmap] → [Block bitmap] → [Data blocks]
```

- **Magic number**: `0x101E1FF5` ("lolelffs" in hexspeak)
- **Block size**: 4096 bytes
- **Max file size**: ~347 GB (uncompressed), ~1.33 GB (mixed compression)
- **Max filename**: 255 characters
- **Extent-based allocation**: Up to 170 extents per file

## Common Commands Reference

### Filesystem Creation
```bash
# Create basic filesystem
lolelffs mkfs --size 50M fs.img

# With encryption
lolelffs mkfs --size 50M --encrypt aes256-xts encrypted.img

# With compression
lolelffs mkfs --size 50M --compress lz4 compressed.img
```

### File Operations
```bash
# List files
lolelffs ls -i fs.img / -l

# Create directory
lolelffs mkdir -i fs.img /config -p

# Write file
lolelffs write -i fs.img /config/app.conf -d "port=8080"

# Read file
lolelffs cat -i fs.img /config/app.conf

# Copy file
lolelffs cp -i fs.img /source.txt /dest.txt

# Extract files
lolelffs extract -i fs.img / output/
```

### Extended Attributes
```bash
# Set extended attribute
lolelffs setfattr -i fs.img /file.txt --name user.comment --value "note"

# Get extended attribute
lolelffs getfattr -i fs.img /file.txt user.comment

# List all xattrs
lolelffs listxattr -i fs.img /file.txt
```

### Filesystem Information
```bash
# Show superblock
lolelffs super -i fs.img

# Check filesystem
lolelffs fsck -i fs.img

# Show usage statistics
lolelffs df -i fs.img

# Get file stats
lolelffs stat -i fs.img /file.txt
```

## Troubleshooting

### Kernel Module Issues

**Module won't load:**
```bash
# Check for missing dependencies
lsmod | grep lz4
lsmod | grep zlib

# Load dependencies first
sudo modprobe zlib_deflate
sudo modprobe lz4_compress

# Then load lolelffs
sudo insmod lolelffs.ko
```

**Mount fails:**
```bash
# Check dmesg for kernel errors
dmesg | tail -20

# Verify filesystem integrity
./lolelffs fsck -i image.img

# Check magic number
xxd -l 16 image.img
# Should show: 0x101E1FF5 at offset 0
```

### ELF Embedding Issues

**Binary not executable after embedding:**
```bash
# Verify ELF header not corrupted
file binary
readelf -h binary

# Check section was added
readelf -S binary | grep lolfs

# Ensure execute bit set
chmod +x binary
```

**Can't mount embedded filesystem:**
```bash
# Extract section for testing
objcopy --dump-section .lolfs.super=extracted.img binary

# Verify extracted filesystem
./lolelffs super -i extracted.img
```

### FUSE Issues

**Permission denied:**
```bash
# Ensure user has fuse access
ls -l /dev/fuse

# Add user to fuse group (if exists)
sudo usermod -a -G fuse $USER

# Or use allow_other mount option
lolelffs-fuse -o allow_other image.img /mnt/point
```

## Learning Path

1. **Start here**: `01-shell-scripts/create-and-populate.sh`
   - Learn basic CLI operations
   - No root required
   - Safe to experiment

2. **Next**: `01-shell-scripts/basic-mount.sh`
   - Understand kernel module mounting
   - Requires root access
   - See filesystem in action

3. **Then**: `02-elf-embedding/hello-world/`
   - Learn ELF embedding basics
   - Understand dual-purpose binaries
   - Foundation for advanced examples

4. **Advanced**: `04-linker-scripts/basic-section/`
   - Fine-grained control
   - Custom ELF layout
   - Linker script fundamentals

5. **Real-world**: `05-advanced-use-cases/`
   - Apply concepts to real scenarios
   - Production-ready patterns
   - Integration examples

## Documentation

- **Main README**: `../README.md` - Comprehensive project documentation
- **ELF Mounting**: `../README_ELF_MOUNT.md` - Detailed ELF embedding guide
- **Encryption**: `../ENCRYPTION.md` - Encryption implementation details
- **Source code**: `../src/lolelffs.h` - Filesystem structure definitions

## Contributing Examples

When adding new examples, follow these guidelines:

1. **Include a README.md** with:
   - Overview of what it demonstrates
   - Prerequisites
   - Build instructions
   - Usage instructions
   - Expected output
   - Explanation of how it works

2. **Make it self-contained**:
   - Include all necessary files
   - Add Makefile or build script
   - Document dependencies

3. **Follow naming conventions**:
   - Use descriptive names
   - Use hyphens for multi-word names
   - Keep directory structure flat

4. **Test thoroughly**:
   - Verify build process
   - Test on clean system
   - Handle errors gracefully

## License

All examples are provided under the same license as the main lolelffs project.

## Support

- **Issues**: Report bugs at https://github.com/hodgesds/lolelffs/issues
- **Documentation**: See main README.md
- **Questions**: Check existing examples first

---

**Tip**: All shell scripts accept `-h` or `--help` for usage information. All C examples include detailed comments explaining the code.
