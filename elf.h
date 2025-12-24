#ifndef LOLELFFS_ELF_H
#define LOLELFFS_ELF_H

#include <linux/elf.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>

/* ELF section name for lolelffs filesystem */
#define LOLELFFS_SECTION ".lolfs.super"

/*
 * Helper to read data from block device that may span multiple blocks.
 * Uses 512-byte sector size for reliable reading across the entire device.
 * Returns 0 on success, -1 on failure.
 */
static inline int read_from_bdev(struct super_block *sb, void *buf,
                                 loff_t offset, size_t size)
{
    size_t bytes_read = 0;
    const size_t sector_size = 512; /* Use 512-byte sectors for ELF parsing */
    int iterations = 0;

    while (bytes_read < size) {
        sector_t sector = offset / sector_size;
        size_t sector_offset = offset % sector_size;
        size_t to_read = min(size - bytes_read,
                             sector_size - sector_offset);
        struct buffer_head *bh;

        /* Safety check to prevent infinite loops */
        if (++iterations > 10000) {
            pr_err("lolelffs: read_from_bdev loop limit exceeded\n");
            return -1;
        }

        /* Use __bread_gfp with non-blocking flag to avoid hangs */
        bh = __bread_gfp(sb->s_bdev, sector, sector_size, GFP_NOWAIT);
        if (!bh) {
            pr_info("lolelffs: Failed to read sector %llu (offset=0x%llx, bytes_read=%zu/%zu)\n",
                    (u64)sector, offset, bytes_read, size);
            return -1;
        }

        memcpy((char *)buf + bytes_read, bh->b_data + sector_offset, to_read);
        brelse(bh);

        bytes_read += to_read;
        offset += to_read;
    }

    return 0;
}

/*
 * Find the .lolfs.super section by reading directly from a file.
 * This is used for loop devices where we can access the backing file.
 * Returns the byte offset of the section, or 0 if not found or not an ELF file.
 */
static inline loff_t find_lolelffs_section(struct file *file)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr, shstrtab_shdr;
    char *shstrtab = NULL;
    loff_t offset = 0;
    loff_t section_offset = 0;
    int i;
    ssize_t ret;

    /* Read ELF header */
    ret = kernel_read(file, &ehdr, sizeof(ehdr), &offset);
    if (ret != sizeof(ehdr))
        return 0;

    /* Check ELF magic */
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0)
        return 0; /* Not an ELF file */

    /* Only support 64-bit ELF */
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        pr_warn("lolelffs: Only 64-bit ELF binaries supported\n");
        return 0;
    }

    /* Validate section header string table index */
    if (ehdr.e_shstrndx == SHN_UNDEF || ehdr.e_shstrndx >= ehdr.e_shnum) {
        pr_warn("lolelffs: Invalid section header string table index\n");
        return 0;
    }

    /* Read section header string table section header */
    offset = ehdr.e_shoff + ehdr.e_shstrndx * sizeof(Elf64_Shdr);
    ret = kernel_read(file, &shstrtab_shdr, sizeof(shstrtab_shdr), &offset);
    if (ret != sizeof(shstrtab_shdr))
        return 0;

    /* Allocate and read section header string table */
    shstrtab = kmalloc(shstrtab_shdr.sh_size, GFP_KERNEL);
    if (!shstrtab)
        return 0;

    offset = shstrtab_shdr.sh_offset;
    ret = kernel_read(file, shstrtab, shstrtab_shdr.sh_size, &offset);
    if (ret != shstrtab_shdr.sh_size) {
        kfree(shstrtab);
        return 0;
    }

    /* Iterate through section headers to find .lolfs.super */
    for (i = 0; i < ehdr.e_shnum; i++) {
        offset = ehdr.e_shoff + i * sizeof(Elf64_Shdr);
        ret = kernel_read(file, &shdr, sizeof(shdr), &offset);
        if (ret != sizeof(shdr))
            continue;

        /* Check section name */
        if (shdr.sh_name < shstrtab_shdr.sh_size) {
            const char *name = shstrtab + shdr.sh_name;
            if (strcmp(name, LOLELFFS_SECTION) == 0) {
                section_offset = shdr.sh_offset;
                pr_info("lolelffs: Found %s section at offset 0x%llx (size: %llu bytes)\n",
                        LOLELFFS_SECTION, section_offset, shdr.sh_size);
                break;
            }
        }
    }

    kfree(shstrtab);
    return section_offset;
}

/*
 * Find the .lolfs.super section in an ELF file and return its offset.
 * Returns the byte offset of the section, or 0 if not found or not an ELF file.
 */
static inline loff_t find_lolelffs_section_from_bdev(struct super_block *sb)
{
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr, shstrtab_shdr;
    char *shstrtab = NULL;
    loff_t section_offset = 0;
    int i;

    /* Read ELF header from block 0 */
    if (read_from_bdev(sb, &ehdr, 0, sizeof(ehdr)) < 0) {
        pr_info("lolelffs: Failed to read ELF header from block device\n");
        return 0;
    }

    /* Check ELF magic */
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        pr_info("lolelffs: Not an ELF file (magic: %02x %02x %02x %02x)\n",
                ehdr.e_ident[0], ehdr.e_ident[1], ehdr.e_ident[2], ehdr.e_ident[3]);
        return 0; /* Not an ELF file */
    }

    pr_info("lolelffs: Detected ELF file, parsing sections...\n");
    pr_info("lolelffs: e_shnum=%d, e_shoff=0x%llx, e_shstrndx=%d\n",
            ehdr.e_shnum, ehdr.e_shoff, ehdr.e_shstrndx);

    /* Only support 64-bit ELF for now */
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        pr_warn("lolelffs: Only 64-bit ELF binaries supported\n");
        return 0;
    }

    /* Validate section header string table index */
    if (ehdr.e_shstrndx == SHN_UNDEF || ehdr.e_shstrndx >= ehdr.e_shnum) {
        pr_warn("lolelffs: Invalid section header string table index (shstrndx=%d, shnum=%d)\n",
                ehdr.e_shstrndx, ehdr.e_shnum);
        return 0;
    }

    /* Read section header string table section header */
    pr_info("lolelffs: Reading shstrtab section header from offset 0x%llx\n",
            ehdr.e_shoff + ehdr.e_shstrndx * sizeof(Elf64_Shdr));
    if (read_from_bdev(sb, &shstrtab_shdr,
                       ehdr.e_shoff + ehdr.e_shstrndx * sizeof(Elf64_Shdr),
                       sizeof(Elf64_Shdr)) < 0) {
        pr_info("lolelffs: Failed to read shstrtab section header\n");
        return 0;
    }

    /* Allocate and read section header string table */
    pr_info("lolelffs: shstrtab size: %llu bytes at offset 0x%llx\n",
            shstrtab_shdr.sh_size, shstrtab_shdr.sh_offset);

    /* Sanity check the size (should be reasonable for a string table) */
    if (shstrtab_shdr.sh_size > 1024 * 1024) {
        pr_warn("lolelffs: String table too large (%llu bytes), aborting\n",
                shstrtab_shdr.sh_size);
        return 0;
    }

    shstrtab = kmalloc(shstrtab_shdr.sh_size, GFP_KERNEL);
    if (!shstrtab) {
        pr_info("lolelffs: Failed to allocate %llu bytes for shstrtab\n",
                shstrtab_shdr.sh_size);
        return 0;
    }

    pr_info("lolelffs: Reading shstrtab data...\n");

    if (read_from_bdev(sb, shstrtab, shstrtab_shdr.sh_offset,
                       shstrtab_shdr.sh_size) < 0) {
        pr_info("lolelffs: Failed to read section header string table\n");
        kfree(shstrtab);
        return 0;
    }

    pr_info("lolelffs: Successfully read shstrtab\n");

    /* Iterate through section headers to find .lolfs.super */
    pr_info("lolelffs: Searching %d sections for %s\n", ehdr.e_shnum, LOLELFFS_SECTION);
    for (i = 0; i < ehdr.e_shnum; i++) {
        if (read_from_bdev(sb, &shdr,
                          ehdr.e_shoff + i * sizeof(Elf64_Shdr),
                          sizeof(Elf64_Shdr)) < 0)
            continue;

        /* Check section name */
        if (shdr.sh_name < shstrtab_shdr.sh_size) {
            const char *name = shstrtab + shdr.sh_name;
            /* Debug: print first few section names */
            if (i < 5 || strncmp(name, ".lol", 4) == 0)
                pr_info("lolelffs: Section %d: '%s'\n", i, name);
            if (strcmp(name, LOLELFFS_SECTION) == 0) {
                section_offset = shdr.sh_offset;
                pr_info("lolelffs: Found %s section at offset 0x%llx (size: %llu bytes)\n",
                        LOLELFFS_SECTION, section_offset, shdr.sh_size);
                break;
            }
        }
    }

    if (section_offset == 0) {
        pr_info("lolelffs: Section %s not found in ELF binary\n", LOLELFFS_SECTION);
    }

    kfree(shstrtab);
    return section_offset;
}

#endif /* LOLELFFS_ELF_H */
