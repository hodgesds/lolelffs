/*
 * fsck.lolelffs - Filesystem consistency checker for lolelffs
 *
 * This tool verifies the integrity of a lolelffs filesystem image by:
 * - Checking superblock validity
 * - Verifying inode and block bitmap consistency
 * - Checking root inode structure
 * - Validating extent structures
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <endian.h>

#include "lolelffs.h"

/* File entry index block for userspace */
struct lolelffs_file_ei_block {
    uint32_t nr_files;
    struct lolelffs_extent extents[(LOLELFFS_BLOCK_SIZE - sizeof(uint32_t)) / sizeof(struct lolelffs_extent)];
};

/* Global state */
static int fd = -1;
static struct lolelffs_sb_info sb;
static int errors = 0;
static int warnings = 0;
static int verbose = 0;

#define ERROR(fmt, ...) do { \
    fprintf(stderr, "ERROR: " fmt "\n", ##__VA_ARGS__); \
    errors++; \
} while(0)

#define WARN(fmt, ...) do { \
    fprintf(stderr, "WARNING: " fmt "\n", ##__VA_ARGS__); \
    warnings++; \
} while(0)

#define INFO(fmt, ...) do { \
    if (verbose) \
        printf("INFO: " fmt "\n", ##__VA_ARGS__); \
} while(0)

/* Read a block from the filesystem */
static int read_block(uint32_t block_num, void *buf)
{
    off_t offset = (off_t)block_num * LOLELFFS_BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) != offset)
        return -1;
    if (read(fd, buf, LOLELFFS_BLOCK_SIZE) != LOLELFFS_BLOCK_SIZE)
        return -1;
    return 0;
}

/* Check superblock validity */
static int check_superblock(void)
{
    char block[LOLELFFS_BLOCK_SIZE];

    printf("Checking superblock...\n");

    if (read_block(0, block) < 0) {
        ERROR("Failed to read superblock");
        return -1;
    }

    memcpy(&sb, block, sizeof(sb));

    /* Check magic number */
    uint32_t magic = le32toh(sb.magic);
    if (magic != LOLELFFS_MAGIC) {
        ERROR("Invalid magic number: 0x%08x (expected 0x%08x)", magic, LOLELFFS_MAGIC);
        return -1;
    }
    INFO("Magic number OK");

    /* Check block count */
    uint32_t nr_blocks = le32toh(sb.nr_blocks);
    if (nr_blocks < 100) {
        ERROR("Invalid block count: %u (minimum 100)", nr_blocks);
        return -1;
    }
    INFO("Block count: %u", nr_blocks);

    /* Check inode count */
    uint32_t nr_inodes = le32toh(sb.nr_inodes);
    if (nr_inodes == 0) {
        ERROR("Invalid inode count: 0");
        return -1;
    }
    if (nr_inodes % LOLELFFS_INODES_PER_BLOCK != 0) {
        WARN("Inode count %u not aligned to block boundary", nr_inodes);
    }
    INFO("Inode count: %u", nr_inodes);

    /* Check filesystem version and compression settings */
    uint32_t version = le32toh(sb.version);
    INFO("Filesystem version: %u", version);

    if (version != LOLELFFS_VERSION) {
        ERROR("Unsupported filesystem version: %u (expected: %u)",
              version, LOLELFFS_VERSION);
        return -1;
    }

    uint32_t comp_algo = le32toh(sb.comp_default_algo);
    uint32_t comp_enabled = le32toh(sb.comp_enabled);
    uint32_t max_extent_blocks = le32toh(sb.max_extent_blocks);

    /* Validate compression algorithm */
    if (comp_algo > LOLELFFS_COMP_ZSTD) {
        ERROR("Invalid compression algorithm: %u", comp_algo);
        return -1;
    }

    /* Validate max extent blocks */
    if (max_extent_blocks != LOLELFFS_MAX_BLOCKS_PER_EXTENT) {
        WARN("Unexpected max_extent_blocks: %u (expected %u)",
             max_extent_blocks, LOLELFFS_MAX_BLOCKS_PER_EXTENT);
    }

    INFO("Compression: %s (algorithm: %u)", comp_enabled ? "enabled" : "disabled", comp_algo);
    INFO("Max extent blocks: %u", max_extent_blocks);

    /* Check encryption settings */
    uint32_t enc_enabled = le32toh(sb.enc_enabled);
    uint32_t enc_algo = le32toh(sb.enc_default_algo);
    uint32_t enc_kdf_algo = le32toh(sb.enc_kdf_algo);
    uint32_t enc_kdf_iterations = le32toh(sb.enc_kdf_iterations);
    uint32_t enc_kdf_memory = le32toh(sb.enc_kdf_memory);
    uint32_t enc_kdf_parallelism = le32toh(sb.enc_kdf_parallelism);

    /* Validate encryption algorithm */
    if (enc_algo > LOLELFFS_ENC_CHACHA20_POLY) {
        ERROR("Invalid encryption algorithm: %u", enc_algo);
        return -1;
    }

    /* Validate KDF algorithm */
    if (enc_kdf_algo > LOLELFFS_KDF_PBKDF2) {
        ERROR("Invalid KDF algorithm: %u", enc_kdf_algo);
        return -1;
    }

    /* Validate KDF parameters */
    if (enc_kdf_algo != LOLELFFS_KDF_NONE) {
        if (enc_kdf_iterations == 0) {
            WARN("KDF iterations is 0 (insecure)");
        }
        if (enc_kdf_iterations > 1000000) {
            WARN("KDF iterations %u seems excessive", enc_kdf_iterations);
        }

        if (enc_kdf_algo == LOLELFFS_KDF_ARGON2ID) {
            if (enc_kdf_memory < 1024) {
                WARN("Argon2id memory %u KB is very low (insecure)", enc_kdf_memory);
            }
            if (enc_kdf_memory > 4194304) {  // 4GB
                WARN("Argon2id memory %u KB seems excessive", enc_kdf_memory);
            }
            if (enc_kdf_parallelism == 0 || enc_kdf_parallelism > 256) {
                WARN("Argon2id parallelism %u is out of reasonable range", enc_kdf_parallelism);
            }
        }
    }

    INFO("Encryption: %s (algorithm: %u, KDF: %u)",
         enc_enabled ? "enabled" : "disabled", enc_algo, enc_kdf_algo);
    if (enc_kdf_algo != LOLELFFS_KDF_NONE) {
        INFO("KDF parameters: iterations=%u, memory=%u KB, parallelism=%u",
             enc_kdf_iterations, enc_kdf_memory, enc_kdf_parallelism);
    }

    /* Verify layout calculations */
    uint32_t nr_istore = le32toh(sb.nr_istore_blocks);
    uint32_t nr_ifree = le32toh(sb.nr_ifree_blocks);
    uint32_t nr_bfree = le32toh(sb.nr_bfree_blocks);
    uint32_t nr_free_inodes = le32toh(sb.nr_free_inodes);
    uint32_t nr_free_blocks = le32toh(sb.nr_free_blocks);

    uint32_t expected_istore = nr_inodes / LOLELFFS_INODES_PER_BLOCK;
    if (nr_istore != expected_istore) {
        ERROR("Inode store blocks mismatch: %u (expected %u)", nr_istore, expected_istore);
    }

    /* Check free counts are reasonable */
    if (nr_free_inodes > nr_inodes) {
        ERROR("Free inodes (%u) exceeds total inodes (%u)", nr_free_inodes, nr_inodes);
    }
    if (nr_free_blocks > nr_blocks) {
        ERROR("Free blocks (%u) exceeds total blocks (%u)", nr_free_blocks, nr_blocks);
    }

    /* Verify block sum */
    uint32_t metadata = 1 + nr_istore + nr_ifree + nr_bfree;
    uint32_t used_blocks = nr_blocks - nr_free_blocks;
    if (used_blocks < metadata) {
        ERROR("Used blocks (%u) less than metadata blocks (%u)", used_blocks, metadata);
    }

    INFO("Layout: superblock(1) + istore(%u) + ifree(%u) + bfree(%u) = %u metadata blocks",
         nr_istore, nr_ifree, nr_bfree, metadata);
    INFO("Free inodes: %u, Free blocks: %u", nr_free_inodes, nr_free_blocks);

    printf("  Superblock OK\n");
    return 0;
}

/* Check root inode */
static int check_root_inode(void)
{
    char block[LOLELFFS_BLOCK_SIZE];
    struct lolelffs_inode *inode;

    printf("Checking root inode...\n");

    /* Root inode is inode 0, in block 1 */
    if (read_block(1, block) < 0) {
        ERROR("Failed to read inode store block");
        return -1;
    }

    inode = (struct lolelffs_inode *)block;

    /* Check mode */
    uint32_t mode = le32toh(inode->i_mode);
    if ((mode & S_IFMT) != S_IFDIR) {
        ERROR("Root inode is not a directory (mode=0%o)", mode);
        return -1;
    }
    INFO("Root is a directory");

    /* Check permissions */
    if (!(mode & S_IRUSR)) {
        WARN("Root directory not readable by owner");
    }
    if (!(mode & S_IXUSR)) {
        WARN("Root directory not executable by owner");
    }

    /* Check link count */
    uint32_t nlink = le32toh(inode->i_nlink);
    if (nlink < 2) {
        ERROR("Root inode link count too low: %u (expected >= 2)", nlink);
    }
    INFO("Root link count: %u", nlink);

    /* Check size */
    uint32_t size = le32toh(inode->i_size);
    if (size != LOLELFFS_BLOCK_SIZE) {
        WARN("Root directory size unexpected: %u (expected %u)", size, LOLELFFS_BLOCK_SIZE);
    }

    /* Check blocks */
    uint32_t blocks = le32toh(inode->i_blocks);
    if (blocks == 0) {
        ERROR("Root inode has 0 blocks");
    }

    /* Check ei_block points to valid location */
    uint32_t ei_block = le32toh(inode->ei_block);
    uint32_t metadata_end = 1 + le32toh(sb.nr_istore_blocks) +
                           le32toh(sb.nr_ifree_blocks) +
                           le32toh(sb.nr_bfree_blocks);
    if (ei_block < metadata_end || ei_block >= le32toh(sb.nr_blocks)) {
        ERROR("Root ei_block %u outside data area [%u, %u)",
              ei_block, metadata_end, le32toh(sb.nr_blocks));
        return -1;
    }
    INFO("Root extent block: %u", ei_block);

    /* Check xattr_block if present */
    uint32_t xattr_block = le32toh(inode->xattr_block);
    if (xattr_block != 0) {
        if (xattr_block < metadata_end || xattr_block >= le32toh(sb.nr_blocks)) {
            ERROR("Root xattr_block %u outside data area [%u, %u)",
                  xattr_block, metadata_end, le32toh(sb.nr_blocks));
            return -1;
        }
        INFO("Root xattr block: %u", xattr_block);
    } else {
        INFO("Root has no xattrs");
    }

    printf("  Root inode OK\n");
    return 0;
}

/* Check root directory extent block */
static int check_root_extent_block(void)
{
    char block[LOLELFFS_BLOCK_SIZE];
    struct lolelffs_inode *root;
    struct lolelffs_file_ei_block *eblock;

    printf("Checking root extent block...\n");

    /* Read root inode to get ei_block */
    if (read_block(1, block) < 0) {
        ERROR("Failed to read inode store block");
        return -1;
    }
    root = (struct lolelffs_inode *)block;
    uint32_t ei_block = le32toh(root->ei_block);

    /* Read extent block */
    if (read_block(ei_block, block) < 0) {
        ERROR("Failed to read root extent block");
        return -1;
    }
    eblock = (struct lolelffs_file_ei_block *)block;

    uint32_t nr_files = le32toh(eblock->nr_files);
    INFO("Root directory contains %u files", nr_files);

    if (nr_files > LOLELFFS_MAX_SUBFILES) {
        ERROR("Root directory file count %u exceeds maximum %lu",
              nr_files, (unsigned long)LOLELFFS_MAX_SUBFILES);
    }

    /* Check extents if there are files */
    if (nr_files > 0) {
        uint32_t nr_extents = (LOLELFFS_BLOCK_SIZE - sizeof(uint32_t)) / sizeof(struct lolelffs_extent);
        for (uint32_t i = 0; i < nr_extents; i++) {
            uint32_t ee_start = le32toh(eblock->extents[i].ee_start);
            if (ee_start == 0)
                break;

            uint32_t ee_len = le32toh(eblock->extents[i].ee_len);
            uint32_t ee_block = le32toh(eblock->extents[i].ee_block);
            uint16_t ee_comp_algo = le16toh(eblock->extents[i].ee_comp_algo);
            uint8_t ee_enc_algo = eblock->extents[i].ee_enc_algo;
            uint16_t ee_flags = le16toh(eblock->extents[i].ee_flags);

            INFO("Extent %u: start=%u, len=%u, logical=%u, comp=%u, enc=%u, flags=0x%04x",
                 i, ee_start, ee_len, ee_block, ee_comp_algo, ee_enc_algo, ee_flags);

            /* Validate extent */
            if (ee_len == 0) {
                ERROR("Extent %u has zero length", i);
            }
            if (ee_len > LOLELFFS_MAX_BLOCKS_PER_EXTENT) {
                ERROR("Extent %u length %u exceeds maximum %u",
                      i, ee_len, LOLELFFS_MAX_BLOCKS_PER_EXTENT);
            }
            if (ee_start + ee_len > le32toh(sb.nr_blocks)) {
                ERROR("Extent %u [%u, %u) outside filesystem",
                      i, ee_start, ee_start + ee_len);
            }

            /* Validate compression algorithm */
            if (ee_comp_algo > LOLELFFS_COMP_ZSTD) {
                ERROR("Extent %u has invalid compression algorithm: %u", i, ee_comp_algo);
            }

            /* Validate encryption algorithm */
            if (ee_enc_algo > LOLELFFS_ENC_CHACHA20_POLY) {
                ERROR("Extent %u has invalid encryption algorithm: %u", i, ee_enc_algo);
            }

            /* Validate flags consistency */
            if ((ee_flags & LOLELFFS_EXT_COMPRESSED) && ee_comp_algo == LOLELFFS_COMP_NONE) {
                WARN("Extent %u has COMPRESSED flag but compression algorithm is NONE", i);
            }
            if ((ee_flags & LOLELFFS_EXT_ENCRYPTED) && ee_enc_algo == LOLELFFS_ENC_NONE) {
                WARN("Extent %u has ENCRYPTED flag but encryption algorithm is NONE", i);
            }
        }
    }

    printf("  Root extent block OK\n");
    return 0;
}

/* Check inode bitmap */
static int check_inode_bitmap(void)
{
    uint32_t nr_inodes = le32toh(sb.nr_inodes);
    uint32_t nr_free_inodes = le32toh(sb.nr_free_inodes);
    uint32_t bitmap_start = 1 + le32toh(sb.nr_istore_blocks);
    uint32_t nr_ifree_blocks = le32toh(sb.nr_ifree_blocks);

    printf("Checking inode bitmap...\n");

    /* Count free bits in bitmap */
    uint32_t free_count = 0;
    for (uint32_t b = 0; b < nr_ifree_blocks; b++) {
        char block[LOLELFFS_BLOCK_SIZE];
        if (read_block(bitmap_start + b, block) < 0) {
            ERROR("Failed to read inode bitmap block %u", b);
            return -1;
        }

        for (uint32_t i = 0; i < LOLELFFS_BLOCK_SIZE; i++) {
            uint8_t byte = block[i];
            for (int bit = 0; bit < 8; bit++) {
                uint32_t inode_num = b * LOLELFFS_BLOCK_SIZE * 8 + i * 8 + bit;
                if (inode_num >= nr_inodes)
                    break;
                if (byte & (1 << bit))
                    free_count++;
            }
        }
    }

    if (free_count != nr_free_inodes) {
        ERROR("Inode bitmap free count mismatch: counted %u, superblock says %u",
              free_count, nr_free_inodes);
    } else {
        INFO("Inode bitmap: %u free inodes verified", free_count);
    }

    /* Verify root inode (inode 0) is marked as used */
    char block[LOLELFFS_BLOCK_SIZE];
    if (read_block(bitmap_start, block) < 0) {
        ERROR("Failed to read first inode bitmap block");
        return -1;
    }
    if (block[0] & 0x01) {
        ERROR("Root inode (inode 0) marked as free in bitmap");
    }

    printf("  Inode bitmap OK\n");
    return 0;
}

/* Check block bitmap */
static int check_block_bitmap(void)
{
    uint32_t nr_blocks = le32toh(sb.nr_blocks);
    uint32_t nr_free_blocks = le32toh(sb.nr_free_blocks);
    uint32_t bitmap_start = 1 + le32toh(sb.nr_istore_blocks) + le32toh(sb.nr_ifree_blocks);
    uint32_t nr_bfree_blocks = le32toh(sb.nr_bfree_blocks);

    printf("Checking block bitmap...\n");

    /* Count free bits in bitmap */
    uint32_t free_count = 0;
    for (uint32_t b = 0; b < nr_bfree_blocks; b++) {
        char block[LOLELFFS_BLOCK_SIZE];
        if (read_block(bitmap_start + b, block) < 0) {
            ERROR("Failed to read block bitmap block %u", b);
            return -1;
        }

        for (uint32_t i = 0; i < LOLELFFS_BLOCK_SIZE; i++) {
            uint8_t byte = block[i];
            for (int bit = 0; bit < 8; bit++) {
                uint32_t block_num = b * LOLELFFS_BLOCK_SIZE * 8 + i * 8 + bit;
                if (block_num >= nr_blocks)
                    break;
                if (byte & (1 << bit))
                    free_count++;
            }
        }
    }

    if (free_count != nr_free_blocks) {
        ERROR("Block bitmap free count mismatch: counted %u, superblock says %u",
              free_count, nr_free_blocks);
    } else {
        INFO("Block bitmap: %u free blocks verified", free_count);
    }

    /* Verify superblock (block 0) is marked as used */
    char block[LOLELFFS_BLOCK_SIZE];
    if (read_block(bitmap_start, block) < 0) {
        ERROR("Failed to read first block bitmap block");
        return -1;
    }
    if (block[0] & 0x01) {
        ERROR("Superblock (block 0) marked as free in bitmap");
    }

    printf("  Block bitmap OK\n");
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-v] <image>\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -v    Verbose output\n");
    fprintf(stderr, "\nCheck the consistency of a lolelffs filesystem image.\n");
}

int main(int argc, char *argv[])
{
    int opt;
    const char *image = NULL;

    while ((opt = getopt(argc, argv, "vh")) != -1) {
        switch (opt) {
        case 'v':
            verbose = 1;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }

    image = argv[optind];

    printf("Checking lolelffs filesystem: %s\n\n", image);

    fd = open(image, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", image, strerror(errno));
        return 1;
    }

    /* Run all checks */
    if (check_superblock() < 0)
        goto done;

    check_root_inode();
    check_root_extent_block();
    check_inode_bitmap();
    check_block_bitmap();

done:
    close(fd);

    printf("\n========================================\n");
    if (errors == 0 && warnings == 0) {
        printf("Filesystem OK - no errors or warnings\n");
    } else {
        printf("Errors: %d, Warnings: %d\n", errors, warnings);
    }
    printf("========================================\n");

    return (errors > 0) ? 1 : 0;
}
