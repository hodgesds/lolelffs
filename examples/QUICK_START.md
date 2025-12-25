# lolelffs Examples - Quick Start Guide

## ✅ All Examples Pass (6/6)

Run the test suite:
```bash
./test-all-no-root.sh
```

## 📚 Examples Overview

### 1. Shell Scripts (No Root Required) ⭐ **Start Here**
```bash
cd 01-shell-scripts

# Comprehensive CLI demonstration
./create-and-populate.sh

# Encryption with AES-256-XTS
./encrypted-workflow.sh

# Create dual-purpose binaries
./dual-purpose-binary.sh
```

**What these demonstrate:**
- All 13+ lolelffs CLI commands
- Filesystem creation, population, and operations
- Encryption/unlocking workflow
- Creating executable + mountable binaries

### 2. ELF Embedding
```bash
cd 02-elf-embedding/hello-world
make
./hello-with-fs
```

**What this demonstrates:**
- Embedding filesystem in ELF using `objcopy --add-section`
- Creating dual-purpose binaries (executable + mountable)
- Accessing embedded data

### 3. Linker Scripts ⭐ **Your Key Request**
```bash
cd 04-linker-scripts

# Basic embedding example
cd basic-section
make
./example

# Page-aligned sections (for mmap)
cd ../aligned-section
make
./example
```

**What these demonstrate:**
- Embedding filesystems in `.lolfs.super` ELF section
- Page alignment (4096 bytes) for efficient memory mapping
- ELF section manipulation with objcopy

### 4. Programmatic Access
```bash
cd 03-programmatic/c-examples
make test
./read-superblock ../../test.img
```

**What this demonstrates:**
- Parsing superblock structure (magic: 0x101E1FF5)
- Reading filesystem metadata
- Understanding block layout
- Compression/encryption status

## 🔑 Key Concepts Demonstrated

### Accessing Embedded Filesystems

**Method 1: Extract section** (works with CLI tools)
```bash
objcopy --dump-section .lolfs.super=fs.img example
lolelffs ls -i fs.img /
lolelffs cat -i fs.img /info.txt
```

**Method 2: Mount with kernel module** (requires root)
```bash
sudo insmod lolelffs.ko
sudo mount -t lolelffs -o loop example /mnt/point
ls /mnt/point
sudo umount /mnt/point
```

**Method 3: CLI tools on raw images** (no root)
```bash
lolelffs ls -i image.img /
lolelffs cat -i image.img /file.txt
lolelffs write -i image.img /new.txt -d "data"
```

### Linker Scripts & Dynamic Paths

The examples show how to:
- ✅ Define `.lolfs.super` sections in ELF binaries
- ✅ Control section placement and alignment
- ✅ Embed filesystems using `objcopy --add-section`
- ✅ Access filesystem data programmatically
- ✅ Create self-contained executables with embedded data

**Page alignment example:**
```ld
. = ALIGN(4096);
.lolfs_data : ALIGN(4096) {
    PROVIDE(__lolfs_start = .);
    *(.lolfs.super)
    PROVIDE(__lolfs_end = .);
}
```

### Encryption Workflow

```bash
# Create encrypted filesystem
lolelffs mkfs --size 10M --encrypt --algo aes-256-xts --password "mypass" fs.img

# Unlock it
lolelffs unlock -i fs.img --password "mypass"

# Use with password
lolelffs write -i fs.img -P "mypass" /secret.txt -d "encrypted data"
lolelffs cat -i fs.img -P "mypass" /secret.txt
```

## 📖 Important Notes

### CLI Tools vs Kernel Module

**Rust CLI tools** (`lolelffs ls/cat/write/etc`):
- ✅ Work on raw `.img` filesystem files
- ✅ No root required
- ✅ Cross-platform
- ❌ Don't parse ELF sections directly (extract first)

**Kernel module** (`mount -t lolelffs`):
- ✅ Parses ELF `.lolfs.super` sections
- ✅ Mounts binaries directly
- ✅ Full VFS integration
- ❌ Requires root access
- ❌ Linux-only

### ELF Embedding Limitations

When using `objcopy --add-section`:
- The section gets added but may show warning: "allocated section not in segment"
- The binary still executes correctly
- Kernel module can mount it directly
- CLI tools need extraction first: `objcopy --dump-section`

## 🚀 Next Steps

1. **Start with**: `01-shell-scripts/create-and-populate.sh` (no root)
2. **Try encryption**: `01-shell-scripts/encrypted-workflow.sh`
3. **Learn ELF embedding**: `02-elf-embedding/hello-world/`
4. **Understand linker scripts**: `04-linker-scripts/basic-section/`
5. **See alignment**: `04-linker-scripts/aligned-section/`
6. **Parse structures**: `03-programmatic/c-examples/`

## 📝 Documentation

- `README.md` - Comprehensive overview
- Each example has its own `README.md`
- Main project: `../README.md`
- ELF mounting: `../README_ELF_MOUNT.md`
- Encryption: `../ENCRYPTION.md`

## 🧪 Testing

Run all non-root examples:
```bash
./test-all-no-root.sh
```

Individual tests:
```bash
cd 01-shell-scripts && ./create-and-populate.sh
cd 02-elf-embedding/hello-world && make test
cd 03-programmatic/c-examples && make test
```

---

**All examples are fully functional and tested!** ✅
