# Hello World - ELF Embedding Example

This example demonstrates the simplest case of embedding a lolelffs filesystem within an ELF executable.

## What It Does

Creates an executable that:
1. Runs as a normal program
2. Contains an embedded lolelffs filesystem in the `.lolfs.super` section
3. Can be mounted or accessed via CLI tools

## Building

```bash
make
```

or:

```bash
./build.sh
```

## Running

```bash
# Execute as normal program
./hello-with-fs

# Access embedded filesystem via CLI
../../../lolelffs ls -i hello-with-fs /
../../../lolelffs cat -i hello-with-fs /config.txt

# Mount (requires root)
sudo mkdir -p /mnt/hello
sudo mount -t lolelffs -o loop hello-with-fs /mnt/hello
cat /mnt/hello/config.txt
sudo umount /mnt/hello
```

## How It Works

1. **Compile** the C program to create normal executable
2. **Create** a lolelffs filesystem image
3. **Populate** the filesystem with config files
4. **Embed** filesystem using `objcopy --add-section`

The key command:
```bash
objcopy --add-section .lolfs.super=fs.img \
        --set-section-flags .lolfs.super=alloc,load,readonly,data \
        hello hello-with-fs
```

This adds the `.lolfs.super` section containing the entire filesystem to the ELF binary.

## Files

- `hello.c` - Simple C program
- `config.txt` - Configuration file to embed
- `build.sh` - Build script
- `Makefile` - Make build file
- `README.md` - This file

## Verifying

Check that the section was added:
```bash
readelf -S hello-with-fs | grep lolfs
```

You should see:
```
[Nr] Name              Type             Address           Offset
     Size              EntSize          Flags  Link  Info  Align
[XX] .lolfs.super      PROGBITS         0000000000000000  XXXXXXXX
     XXXXXXXXXXXX      0000000000000000  A       0     0     1
```

## Next Steps

- See `../self-extracting/` for self-extracting archives
- See `../../04-linker-scripts/basic-section/` for linker script approach
