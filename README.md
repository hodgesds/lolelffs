# lolelffs

`lolelffs` is a Linux kernel filesystem module based on simplefs that is compliant with the ELF specification. This means that a `lolelffs` enabled binary can also work as a filesystem with the `lolelffs` kernel module loaded.

## Features

- **ELF-compliant filesystem**: Create filesystems within ELF binary files
- **Extent-based allocation**: Efficient storage management with up to 48 extents per file
- **Standard POSIX operations**: Support for files, directories, hard links, and symbolic links
- **Maximum file size**: ~11 MB per file (8 blocks × 4KB × 340+ extents)
- **Maximum filename length**: 255 characters
- **Maximum files per directory**: 40,920

## Building

### Prerequisites

- Linux kernel headers
- libelf development library
- GCC

On Debian/Ubuntu:
```bash
sudo apt-get install build-essential linux-headers-$(uname -r) libelf-dev
```

On Fedora/RHEL:
```bash
sudo dnf install kernel-devel elfutils-libelf-devel gcc make
```

### Compile

```bash
make
```

This will build:
- `lolelffs.ko` - The kernel module
- `mkfs.lolelffs` - Filesystem creation utility

## Usage

### Creating a Filesystem

1. Create a file to use as the filesystem image:
```bash
dd if=/dev/zero of=myfs.img bs=1M count=100
```

2. Format it with lolelffs:
```bash
./mkfs.lolelffs myfs.img
```

### Mounting the Filesystem

1. Load the kernel module:
```bash
sudo insmod lolelffs.ko
```

2. Mount the filesystem:
```bash
mkdir -p /mnt/lolelffs
sudo mount -t lolelffs -o loop myfs.img /mnt/lolelffs
```

### Using with ELF Files

You can also use an ELF binary as the filesystem image:
```bash
cp /bin/ls myelf.bin
# Pad to minimum size (must be at least 400KB)
truncate -s 1M myelf.bin
./mkfs.lolelffs myelf.bin
sudo mount -t lolelffs -o loop myelf.bin /mnt/lolelffs
```

### Unmounting

```bash
sudo umount /mnt/lolelffs
sudo rmmod lolelffs
```

## Testing

### Unit Tests

Run the unit tests (no root required):
```bash
make test
```

This runs:
- `test_unit` - Tests for filesystem constants, structures, and calculations
- `test_mkfs` - Tests for the mkfs.lolelffs utility

### Integration Tests

Run the full integration tests (requires root for kernel module operations):
```bash
make test-integration
```

### Create a Test Image

```bash
make test-image
```

## Filesystem Layout

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
|    blocks     |  rest of the blocks
+---------------+
```

## Data Structures

### Superblock

Contains filesystem metadata:
- Magic number: `0x101E1FF5` ("lolelffs" in hexspeak)
- Total blocks/inodes
- Free bitmap locations
- Free block/inode counts

### Inode (64 bytes)

```c
struct lolelffs_inode {
    uint32_t i_mode;    // File mode
    uint32_t i_uid;     // Owner id
    uint32_t i_gid;     // Group id
    uint32_t i_size;    // Size in bytes
    uint32_t i_ctime;   // Inode change time
    uint32_t i_atime;   // Access time
    uint32_t i_mtime;   // Modification time
    uint32_t i_blocks;  // Block count
    uint32_t i_nlink;   // Hard links count
    uint32_t ei_block;  // Block with extents
    char i_data[32];    // Symlink content
};
```

### Extent

```c
struct lolelffs_extent {
    uint32_t ee_block;  // First logical block
    uint32_t ee_len;    // Number of blocks
    uint32_t ee_start;  // First physical block
};
```

## Limitations

- Block size: 4 KB (fixed)
- Maximum file size: ~11 MB
- Maximum extents per file: 48
- Blocks per extent: 8
- No extended attributes
- No journaling

## Source Files

| File | Description |
|------|-------------|
| `lolelffs.h` | Header with data structures and constants |
| `bitmap.h` | Bitmap manipulation helpers |
| `fs.c` | Filesystem registration |
| `super.c` | Superblock operations |
| `inode.c` | Inode operations |
| `file.c` | File read/write operations |
| `dir.c` | Directory operations |
| `extent.c` | Extent searching |
| `mkfs.c` | Filesystem creation utility |

## License

Dual BSD/GPL

## Author

Daniel Hodges

## References

- Based on [simplefs](https://github.com/sysprog21/simplefs)
- [Linux VFS documentation](https://www.kernel.org/doc/html/latest/filesystems/vfs.html)
