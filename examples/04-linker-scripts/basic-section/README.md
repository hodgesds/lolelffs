# Basic Section - Embedding Filesystem in ELF

This example demonstrates embedding a lolelffs filesystem in an ELF binary using `objcopy`.

## What It Does

Creates an executable that contains an embedded lolelffs filesystem in the `.lolfs.super` ELF section. The binary can be:
- Executed as a normal program
- Mounted as a filesystem (kernel module, requires root)
- Inspected with ELF tools

## Building

```bash
./build.sh
```

or:

```bash
make
```

## Running

```bash
# Execute the program
./example

# Check ELF structure
readelf -S example | grep lolfs

# Mount it (requires root and kernel module loaded)
sudo mount -t lolelffs -o loop example /mnt/point
ls /mnt/point
sudo umount /mnt/point
```

## How It Works

1. **Compile** the C program to create a base executable
2. **Create** a lolelffs filesystem image
3. **Populate** the filesystem with files
4. **Embed** using `objcopy --add-section`:
   ```bash
   objcopy --add-section .lolfs.super=fs.img \
           --set-section-flags .lolfs.super=alloc,load,readonly,data \
           example-base example
   ```

This adds the entire filesystem as a new ELF section.

## Key Points

### What Works
✅ Embedding filesystem in ELF binary
✅ Running the executable
✅ Mounting with kernel module (requires root)
✅ Inspecting with `readelf`, `objdump`

### Current Limitations
❌ Rust CLI tools (`lolelffs ls/cat/etc`) don't parse ELF sections yet
   (They work on raw filesystem images, not ELF-embedded ones)

### Accessing the Embedded Filesystem

**Method 1: Mount with kernel module** (recommended)
```bash
sudo insmod ../../../lolelffs.ko
sudo mount -t lolelffs -o loop example /mnt/point
ls /mnt/point
cat /mnt/point/info.txt
sudo umount /mnt/point
```

**Method 2: Extract the section first**
```bash
objcopy --dump-section .lolfs.super=extracted.img example
../../../lolelffs ls -i extracted.img /
../../../lolelffs cat -i extracted.img /info.txt
```

## Files

- `example.c` - Simple C program
- `build.sh` - Build script using objcopy
- `Makefile` - Make build file
- `README.md` - This file
- `lolfs.ld` - Linker script (for reference, shows alternative approaches)

## ELF Section Details

The `.lolfs.super` section contains the complete filesystem:
- Flags: `alloc,load,readonly,data`
- Type: `PROGBITS`
- Contains: Full lolelffs filesystem starting with superblock

View with:
```bash
readelf -S example
readelf -x .lolfs.super example  # Hex dump of section
```

## Advantages of This Approach

- **Self-contained**: One file is both program and data
- **Distribution**: Easy to distribute single executable
- **Security**: Filesystem can be encrypted
- **Version control**: Binary and data versioned together

## Next Steps

- See `../aligned-section/` for page-aligned sections (important for mmap)
- See `../../02-elf-embedding/hello-world/` for similar approach
- See main README for kernel module usage
