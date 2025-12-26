#include <errno.h>
#include <fcntl.h>
#include <libelf.h>
#include <gelf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/fs.h>

#include "lolelffs.h"

struct superblock {
    struct lolelffs_sb_info info;
    char padding[LOLELFFS_BLOCK_SIZE - sizeof(struct lolelffs_sb_info)];
};

/* Returns ceil(a/b) */
static inline uint32_t idiv_ceil(uint32_t a, uint32_t b)
{
    uint32_t ret = a / b;
    if (a % b)
        return ret + 1;
    return ret;
}

static struct superblock *write_superblock(int fd, struct stat *fstats)
{
    struct superblock *sb = malloc(sizeof(struct superblock));
    if (!sb)
        return NULL;

    uint32_t nr_blocks = fstats->st_size / LOLELFFS_BLOCK_SIZE;
    uint32_t nr_inodes = nr_blocks;
    uint32_t mod = nr_inodes % LOLELFFS_INODES_PER_BLOCK;
    if (mod)
        nr_inodes += LOLELFFS_INODES_PER_BLOCK - mod;
    uint32_t nr_istore_blocks = idiv_ceil(nr_inodes, LOLELFFS_INODES_PER_BLOCK);
    uint32_t nr_ifree_blocks = idiv_ceil(nr_inodes, LOLELFFS_BLOCK_SIZE * 8);
    uint32_t nr_bfree_blocks = idiv_ceil(nr_blocks, LOLELFFS_BLOCK_SIZE * 8);
    uint32_t nr_data_blocks =
        nr_blocks - 1 - nr_istore_blocks - nr_ifree_blocks - nr_bfree_blocks;

    memset(sb, 0, sizeof(struct superblock));
    sb->info = (struct lolelffs_sb_info){
        .magic = htole32(LOLELFFS_MAGIC),
        .nr_blocks = htole32(nr_blocks),
        .nr_inodes = htole32(nr_inodes),
        .nr_istore_blocks = htole32(nr_istore_blocks),
        .nr_ifree_blocks = htole32(nr_ifree_blocks),
        .nr_bfree_blocks = htole32(nr_bfree_blocks),
        .nr_free_inodes = htole32(nr_inodes - 1),
        .nr_free_blocks = htole32(nr_data_blocks - 1),
        /* Compression support */
        .version = htole32(LOLELFFS_VERSION),
        .comp_default_algo = htole32(LOLELFFS_COMP_LZ4),
        .comp_enabled = htole32(1),  /* Compression enabled by default */
        .comp_min_block_size = htole32(128),  /* Don't compress blocks < 128 bytes */
        .comp_features = htole32(LOLELFFS_FEATURE_LARGE_EXTENTS),
        .max_extent_blocks = htole32(LOLELFFS_MAX_BLOCKS_PER_EXTENT),
        .max_extent_blocks_large = htole32(LOLELFFS_MAX_BLOCKS_PER_EXTENT_LARGE),
        /* Encryption support */
        .enc_enabled = htole32(0),  /* Encryption disabled by default */
        .enc_default_algo = htole32(LOLELFFS_ENC_NONE),
        .enc_kdf_algo = htole32(LOLELFFS_KDF_ARGON2ID),
        .enc_kdf_iterations = htole32(3),
        .enc_kdf_memory = htole32(65536),  /* 64 MB */
        .enc_kdf_parallelism = htole32(4),
        .enc_salt = {0},
        .enc_master_key = {0},
        .enc_features = htole32(0),
        .reserved = {0},
    };

    ssize_t ret = write(fd, sb, sizeof(struct superblock));
    if (ret != sizeof(struct superblock)) {
        free(sb);
        return NULL;
    }

    const char *comp_algo_str = "none";
    switch (le32toh(sb->info.comp_default_algo)) {
        case LOLELFFS_COMP_LZ4: comp_algo_str = "lz4"; break;
        case LOLELFFS_COMP_ZLIB: comp_algo_str = "zlib"; break;
        case LOLELFFS_COMP_ZSTD: comp_algo_str = "zstd"; break;
    }

    printf(
        "Superblock: (%ld)\n"
        "\tmagic=%#x\n"
        "\tversion=%u\n"
        "\tnr_blocks=%u\n"
        "\tnr_inodes=%u (istore=%u blocks)\n"
        "\tnr_ifree_blocks=%u\n"
        "\tnr_bfree_blocks=%u\n"
        "\tnr_free_inodes=%u\n"
        "\tnr_free_blocks=%u\n"
        "\tcompression=%s (algo=%s, enabled=%u)\n"
        "\tmax_extent_blocks=%u\n",
        sizeof(struct superblock), le32toh(sb->info.magic),
        le32toh(sb->info.version), le32toh(sb->info.nr_blocks),
        le32toh(sb->info.nr_inodes), le32toh(sb->info.nr_istore_blocks),
        le32toh(sb->info.nr_ifree_blocks), le32toh(sb->info.nr_bfree_blocks),
        le32toh(sb->info.nr_free_inodes), le32toh(sb->info.nr_free_blocks),
        le32toh(sb->info.comp_enabled) ? "yes" : "no", comp_algo_str,
        le32toh(sb->info.comp_enabled), le32toh(sb->info.max_extent_blocks));

    return sb;
}

static int write_inode_store(int fd, struct superblock *sb)
{
    /* Allocate a zeroed block for inode store */
    char *block = malloc(LOLELFFS_BLOCK_SIZE);
    if (!block)
        return -1;

    memset(block, 0, LOLELFFS_BLOCK_SIZE);

    /* Root inode (inode 0) */
    struct lolelffs_inode *inode = (struct lolelffs_inode *) block;
    uint32_t first_data_block = 1 + le32toh(sb->info.nr_bfree_blocks) +
                                le32toh(sb->info.nr_ifree_blocks) +
                                le32toh(sb->info.nr_istore_blocks);
    inode->i_mode = htole32(S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR |
                            S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH);
    inode->i_uid = 0;
    inode->i_gid = 0;
    inode->i_size = htole32(LOLELFFS_BLOCK_SIZE);
    inode->i_ctime = inode->i_atime = inode->i_mtime = htole32(0);
    inode->i_blocks = htole32(1);
    inode->i_nlink = htole32(2);
    inode->ei_block = htole32(first_data_block);
    inode->xattr_block = 0; /* No xattrs initially */

    ssize_t ret = write(fd, block, LOLELFFS_BLOCK_SIZE);
    if (ret != LOLELFFS_BLOCK_SIZE) {
        ret = -1;
        goto end;
    }

    /* Reset inode store blocks to zero */
    memset(block, 0, LOLELFFS_BLOCK_SIZE);
    uint32_t i;
    for (i = 1; i < sb->info.nr_istore_blocks; i++) {
        ret = write(fd, block, LOLELFFS_BLOCK_SIZE);
        if (ret != LOLELFFS_BLOCK_SIZE) {
            ret = -1;
            goto end;
        }
    }
    ret = 0;

    printf(
        "Inode store: wrote %d blocks\n"
        "\tinode size = %ld B\n",
        i, sizeof(struct lolelffs_inode));

end:
    free(block);
    return ret;
}

static int write_ifree_blocks(int fd, struct superblock *sb)
{
    char *block = malloc(LOLELFFS_BLOCK_SIZE);
    if (!block)
        return -1;

    uint64_t *ifree = (uint64_t *) block;

    /* Set all bits to 1 */
    memset(ifree, 0xff, LOLELFFS_BLOCK_SIZE);

    /* First ifree block, containing first used inode */
    ifree[0] = htole64(0xfffffffffffffffe);
    ssize_t ret = write(fd, ifree, LOLELFFS_BLOCK_SIZE);
    if (ret != LOLELFFS_BLOCK_SIZE) {
        ret = -1;
        goto end;
    }

    /* All ifree blocks except the one containing 2 first inodes */
    ifree[0] = 0xffffffffffffffff;
    uint32_t i;
    for (i = 1; i < le32toh(sb->info.nr_ifree_blocks); i++) {
        ret = write(fd, ifree, LOLELFFS_BLOCK_SIZE);
        if (ret != LOLELFFS_BLOCK_SIZE) {
            ret = -1;
            goto end;
        }
    }
    ret = 0;

    printf("Ifree blocks: wrote %d blocks\n", i);

end:
    free(block);

    return ret;
}

static int write_bfree_blocks(int fd, struct superblock *sb)
{
    uint32_t nr_used = le32toh(sb->info.nr_istore_blocks) +
                       le32toh(sb->info.nr_ifree_blocks) +
                       le32toh(sb->info.nr_bfree_blocks) + 2;

    char *block = malloc(LOLELFFS_BLOCK_SIZE);
    if (!block)
        return -1;
    uint64_t *bfree = (uint64_t *) block;

    /*
     * First blocks (incl. sb + istore + ifree + bfree + 1 used block)
     * we suppose it won't go further than the first block
     */
    memset(bfree, 0xff, LOLELFFS_BLOCK_SIZE);
    uint32_t i = 0;
    while (nr_used) {
        uint64_t line = 0xffffffffffffffff;
        for (uint64_t mask = 0x1; mask; mask <<= 1) {
            line &= ~mask;
            nr_used--;
            if (!nr_used)
                break;
        }
        bfree[i] = htole64(line);
        i++;
    }
    ssize_t ret = write(fd, bfree, LOLELFFS_BLOCK_SIZE);
    if (ret != LOLELFFS_BLOCK_SIZE) {
        ret = -1;
        goto end;
    }

    /* other blocks */
    memset(bfree, 0xff, LOLELFFS_BLOCK_SIZE);
    for (i = 1; i < le32toh(sb->info.nr_bfree_blocks); i++) {
        ret = write(fd, bfree, LOLELFFS_BLOCK_SIZE);
        if (ret != LOLELFFS_BLOCK_SIZE) {
            ret = -1;
            goto end;
        }
    }
    ret = 0;

    printf("Bfree blocks: wrote %d blocks\n", i);
end:
    free(block);

    return ret;
}

/*
 * Initialize root directory's extent index block.
 * This block contains the extent list and nr_files counter.
 */
static int write_data_blocks(int fd)
{
    char *block = malloc(LOLELFFS_BLOCK_SIZE);
    if (!block)
        return -1;

    /* Initialize the extent index block for root directory */
    memset(block, 0, LOLELFFS_BLOCK_SIZE);

    /* The first 4 bytes are nr_files (0 for empty directory) */
    uint32_t *nr_files = (uint32_t *)block;
    *nr_files = htole32(0);

    /* Extents array follows - all zeros for empty directory */
    /* First extent will point to data blocks when files are added */

    ssize_t ret = write(fd, block, LOLELFFS_BLOCK_SIZE);
    if (ret != LOLELFFS_BLOCK_SIZE) {
        free(block);
        return -1;
    }

    printf("Data blocks: wrote root directory extent index block\n");

    free(block);
    return 0;
}

/* Check if file is an ELF binary and print info */
static int check_elf_file(int fd)
{
    if (elf_version(EV_CURRENT) == EV_NONE) {
        fprintf(stderr, "ELF library initialization failed: %s\n", elf_errmsg(-1));
        return -1;
    }

    Elf *elf = elf_begin(fd, ELF_C_READ, NULL);
    if (!elf) {
        fprintf(stderr, "elf_begin() failed: %s\n", elf_errmsg(-1));
        return -1;
    }

    if (elf_kind(elf) != ELF_K_ELF) {
        fprintf(stderr, "Not an ELF file\n");
        elf_end(elf);
        return -1;
    }

    GElf_Ehdr ehdr;
    if (!gelf_getehdr(elf, &ehdr)) {
        fprintf(stderr, "gelf_getehdr() failed: %s\n", elf_errmsg(-1));
        elf_end(elf);
        return -1;
    }

    printf("ELF file detected:\n");
    printf("\tClass: %s\n", ehdr.e_ident[EI_CLASS] == ELFCLASS64 ? "64-bit" : "32-bit");
    printf("\tType: %d\n", ehdr.e_type);
    printf("\tMachine: %d\n", ehdr.e_machine);
    printf("\tEntry point: 0x%lx\n", (unsigned long)ehdr.e_entry);

    /* List sections */
    size_t shstrndx;
    if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
        fprintf(stderr, "elf_getshdrstrndx() failed: %s\n", elf_errmsg(-1));
        elf_end(elf);
        return -1;
    }

    Elf_Scn *scn = NULL;
    int found_lolfs = 0;
    printf("\tSections:\n");
    while ((scn = elf_nextscn(elf, scn)) != NULL) {
        GElf_Shdr shdr;
        if (gelf_getshdr(scn, &shdr) != &shdr) {
            fprintf(stderr, "gelf_getshdr() failed: %s\n", elf_errmsg(-1));
            continue;
        }

        char *name = elf_strptr(elf, shstrndx, shdr.sh_name);
        if (name) {
            printf("\t\t%s (size: %lu, offset: 0x%lx)\n",
                   name, (unsigned long)shdr.sh_size, (unsigned long)shdr.sh_offset);
            if (strcmp(name, LOLELFFS_SB_SECTION) == 0) {
                found_lolfs = 1;
                printf("\t\t  ^ Found lolelffs superblock section!\n");
            }
        }
    }

    if (!found_lolfs) {
        printf("\tNote: No %s section found (will be used as raw storage)\n", LOLELFFS_SB_SECTION);
    }

    elf_end(elf);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s disk\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Open disk image */
    int fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        perror("open():");
        return EXIT_FAILURE;
    }

    /* Get image size */
    struct stat stat_buf;
    int ret = fstat(fd, &stat_buf);
    if (ret) {
        perror("fstat():");
        ret = EXIT_FAILURE;
        goto close_fd;
    }

    /* Check if it's an ELF file and print info */
    lseek(fd, 0, SEEK_SET);
    check_elf_file(fd);

    /* Reset file position for writing */
    lseek(fd, 0, SEEK_SET);

    /* Get block device size */
    if ((stat_buf.st_mode & S_IFMT) == S_IFBLK) {
        long int blk_size = 0;
        ret = ioctl(fd, BLKGETSIZE64, &blk_size);
        if (ret != 0) {
            perror("BLKGETSIZE64:");
            ret = EXIT_FAILURE;
            goto close_fd;
        }
        stat_buf.st_size = blk_size;
    }

    /* Check if image is large enough */
    long int min_size = 100 * LOLELFFS_BLOCK_SIZE;
    if (stat_buf.st_size <= min_size) {
        fprintf(stderr, "File is not large enough (size=%ld, min size=%ld)\n",
                stat_buf.st_size, min_size);
        ret = EXIT_FAILURE;
        goto close_fd;
    }

    /* Write superblock (block 0) */
    struct superblock *sb = write_superblock(fd, &stat_buf);
    if (!sb) {
        perror("write_superblock():");
        ret = EXIT_FAILURE;
        goto close_fd;
    }

    /* Write inode store blocks (from block 1) */
    ret = write_inode_store(fd, sb);
    if (ret) {
        perror("write_inode_store():");
        ret = EXIT_FAILURE;
        goto free_sb;
    }

    /* Write inode free bitmap blocks */
    ret = write_ifree_blocks(fd, sb);
    if (ret) {
        perror("write_ifree_blocks()");
        ret = EXIT_FAILURE;
        goto free_sb;
    }

    /* Write block free bitmap blocks */
    ret = write_bfree_blocks(fd, sb);
    if (ret) {
        perror("write_bfree_blocks()");
        ret = EXIT_FAILURE;
        goto free_sb;
    }

    /* Write data blocks */
    ret = write_data_blocks(fd);
    if (ret) {
        perror("write_data_blocks():");
        ret = EXIT_FAILURE;
        goto free_sb;
    }

    printf("\nFilesystem created successfully!\n");
    printf("Total size: %ld bytes (%u blocks)\n",
           stat_buf.st_size, le32toh(sb->info.nr_blocks));

free_sb:
    free(sb);
close_fd:
    close(fd);

    return ret;
}
