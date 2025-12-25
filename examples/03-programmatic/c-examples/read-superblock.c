/*
 * read-superblock.c - Parse and display lolelffs superblock
 *
 * This example demonstrates:
 * - Opening a lolelffs image file
 * - Reading the superblock (block 0)
 * - Parsing superblock fields
 * - Validating magic number
 * - Displaying filesystem information
 *
 * Usage: ./read-superblock image.img
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

/* lolelffs constants and structures */
#define LOLELFFS_MAGIC 0x101E1FF5
#define LOLELFFS_BLOCK_SIZE 4096
#define LOLELFFS_VERSION 1

/* Compression algorithm IDs */
#define LOLELFFS_COMP_NONE  0
#define LOLELFFS_COMP_LZ4   1
#define LOLELFFS_COMP_ZLIB  2
#define LOLELFFS_COMP_ZSTD  3

/* Encryption algorithm IDs */
#define LOLELFFS_ENC_NONE           0
#define LOLELFFS_ENC_AES256_XTS     1
#define LOLELFFS_ENC_CHACHA20_POLY  2

/* KDF algorithm IDs */
#define LOLELFFS_KDF_NONE      0
#define LOLELFFS_KDF_ARGON2ID  1
#define LOLELFFS_KDF_PBKDF2    2

/* Superblock structure */
struct lolelffs_sb_info {
    uint32_t magic;

    uint32_t nr_blocks;
    uint32_t nr_inodes;

    uint32_t nr_istore_blocks;
    uint32_t nr_ifree_blocks;
    uint32_t nr_bfree_blocks;

    uint32_t nr_free_inodes;
    uint32_t nr_free_blocks;

    /* Compression support */
    uint32_t version;
    uint32_t comp_default_algo;
    uint32_t comp_enabled;
    uint32_t comp_min_block_size;
    uint32_t comp_features;
    uint32_t max_extent_blocks;

    /* Encryption support */
    uint32_t enc_enabled;
    uint32_t enc_default_algo;
    uint32_t enc_kdf_algo;
    uint32_t enc_kdf_iterations;
    uint32_t enc_kdf_memory;
    uint32_t enc_kdf_parallelism;
    uint8_t  enc_salt[32];
    uint8_t  enc_master_key[32];
    uint32_t enc_features;
    uint32_t reserved[3];
} __attribute__((packed));

const char *comp_algo_name(uint32_t algo) {
    switch (algo) {
        case LOLELFFS_COMP_NONE:  return "none";
        case LOLELFFS_COMP_LZ4:   return "lz4";
        case LOLELFFS_COMP_ZLIB:  return "zlib";
        case LOLELFFS_COMP_ZSTD:  return "zstd";
        default:                  return "unknown";
    }
}

const char *enc_algo_name(uint32_t algo) {
    switch (algo) {
        case LOLELFFS_ENC_NONE:           return "none";
        case LOLELFFS_ENC_AES256_XTS:     return "aes256-xts";
        case LOLELFFS_ENC_CHACHA20_POLY:  return "chacha20-poly1305";
        default:                          return "unknown";
    }
}

const char *kdf_algo_name(uint32_t algo) {
    switch (algo) {
        case LOLELFFS_KDF_NONE:      return "none";
        case LOLELFFS_KDF_ARGON2ID:  return "argon2id";
        case LOLELFFS_KDF_PBKDF2:    return "pbkdf2";
        default:                     return "unknown";
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image.img>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open file");
        return 1;
    }

    /* Get file size */
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("Failed to stat file");
        close(fd);
        return 1;
    }

    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║          lolelffs Superblock Reader                       ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");

    printf("File: %s\n", filename);
    printf("Size: %ld bytes (%.2f MB)\n\n", st.st_size, st.st_size / 1024.0 / 1024.0);

    /* Read superblock (first 4KB block) */
    struct lolelffs_sb_info sb;
    ssize_t bytes_read = read(fd, &sb, sizeof(sb));
    if (bytes_read < (ssize_t)sizeof(sb)) {
        fprintf(stderr, "Error: Failed to read superblock\n");
        close(fd);
        return 1;
    }

    /* Validate magic number */
    printf("═══ Superblock Validation ═══\n");
    if (sb.magic == LOLELFFS_MAGIC) {
        printf("  ✓ Magic:     0x%08X (valid lolelffs)\n", sb.magic);
    } else {
        printf("  ✗ Magic:     0x%08X (invalid, expected 0x%08X)\n",
               sb.magic, LOLELFFS_MAGIC);
        close(fd);
        return 1;
    }

    if (sb.version == LOLELFFS_VERSION) {
        printf("  ✓ Version:   %u\n", sb.version);
    } else {
        printf("  ⚠ Version:   %u (expected %u)\n", sb.version, LOLELFFS_VERSION);
    }

    printf("\n═══ Filesystem Layout ═══\n");
    printf("  Total blocks:       %u (%u MB)\n",
           sb.nr_blocks, sb.nr_blocks * LOLELFFS_BLOCK_SIZE / 1024 / 1024);
    printf("  Block size:         %u bytes\n", LOLELFFS_BLOCK_SIZE);
    printf("  Inode store blocks: %u\n", sb.nr_istore_blocks);
    printf("  Inode bitmap blocks: %u\n", sb.nr_ifree_blocks);
    printf("  Block bitmap blocks: %u\n", sb.nr_bfree_blocks);

    uint32_t metadata_blocks = 1 + sb.nr_istore_blocks + sb.nr_ifree_blocks + sb.nr_bfree_blocks;
    uint32_t data_blocks = sb.nr_blocks - metadata_blocks;
    printf("  Metadata blocks:    %u\n", metadata_blocks);
    printf("  Data blocks:        %u\n", data_blocks);

    printf("\n═══ Capacity ═══\n");
    printf("  Total inodes:       %u\n", sb.nr_inodes);
    printf("  Used inodes:        %u\n", sb.nr_inodes - sb.nr_free_inodes);
    printf("  Free inodes:        %u\n", sb.nr_free_inodes);
    printf("  Inode usage:        %.1f%%\n",
           (float)(sb.nr_inodes - sb.nr_free_inodes) / sb.nr_inodes * 100.0);

    printf("\n  Total blocks:       %u\n", sb.nr_blocks);
    printf("  Used blocks:        %u\n", sb.nr_blocks - sb.nr_free_blocks);
    printf("  Free blocks:        %u\n", sb.nr_free_blocks);
    printf("  Block usage:        %.1f%%\n",
           (float)(sb.nr_blocks - sb.nr_free_blocks) / sb.nr_blocks * 100.0);

    uint64_t total_size = (uint64_t)sb.nr_blocks * LOLELFFS_BLOCK_SIZE;
    uint64_t used_size = (uint64_t)(sb.nr_blocks - sb.nr_free_blocks) * LOLELFFS_BLOCK_SIZE;
    uint64_t free_size = (uint64_t)sb.nr_free_blocks * LOLELFFS_BLOCK_SIZE;

    printf("\n  Total space:        %lu bytes (%.2f MB)\n",
           total_size, total_size / 1024.0 / 1024.0);
    printf("  Used space:         %lu bytes (%.2f MB)\n",
           used_size, used_size / 1024.0 / 1024.0);
    printf("  Free space:         %lu bytes (%.2f MB)\n",
           free_size, free_size / 1024.0 / 1024.0);

    printf("\n═══ Compression ═══\n");
    if (sb.comp_enabled) {
        printf("  Status:             enabled\n");
        printf("  Default algorithm:  %s\n", comp_algo_name(sb.comp_default_algo));
        printf("  Min block size:     %u bytes\n", sb.comp_min_block_size);
        printf("  Max extent blocks:  %u\n", sb.max_extent_blocks);
        printf("  Features:           0x%08X\n", sb.comp_features);
    } else {
        printf("  Status:             disabled\n");
    }

    printf("\n═══ Encryption ═══\n");
    if (sb.enc_enabled) {
        printf("  Status:             enabled\n");
        printf("  Algorithm:          %s\n", enc_algo_name(sb.enc_default_algo));
        printf("  KDF:                %s\n", kdf_algo_name(sb.enc_kdf_algo));
        printf("  KDF iterations:     %u\n", sb.enc_kdf_iterations);
        printf("  KDF memory:         %u KB\n", sb.enc_kdf_memory);
        printf("  KDF parallelism:    %u\n", sb.enc_kdf_parallelism);
        printf("  Salt (hex):         ");
        for (int i = 0; i < 32; i++) {
            printf("%02x", sb.enc_salt[i]);
        }
        printf("\n");
        printf("  Master key (enc):   ");
        for (int i = 0; i < 32; i++) {
            printf("%02x", sb.enc_master_key[i]);
        }
        printf("\n");
        printf("  Features:           0x%08X\n", sb.enc_features);
    } else {
        printf("  Status:             disabled\n");
    }

    printf("\n═══ Layout Summary ═══\n");
    printf("  Block 0:            Superblock\n");
    printf("  Block 1-%u:        Inode store\n", sb.nr_istore_blocks);
    printf("  Block %u-%u:  Inode bitmap\n",
           sb.nr_istore_blocks + 1,
           sb.nr_istore_blocks + sb.nr_ifree_blocks);
    printf("  Block %u-%u:  Block bitmap\n",
           sb.nr_istore_blocks + sb.nr_ifree_blocks + 1,
           sb.nr_istore_blocks + sb.nr_ifree_blocks + sb.nr_bfree_blocks);
    printf("  Block %u-%u:     Data blocks\n",
           metadata_blocks, sb.nr_blocks - 1);

    close(fd);

    printf("\n✓ Superblock read successfully\n");
    return 0;
}
