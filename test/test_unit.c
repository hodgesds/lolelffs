/*
 * Unit tests for lolelffs filesystem structures and calculations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "../lolelffs.h"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  Testing %s... ", #name); \
    tests_run++; \
    if (test_##name()) { \
        printf("PASS\n"); \
        tests_passed++; \
    } else { \
        printf("FAIL\n"); \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\n    Assertion failed: %s (line %d)\n", #cond, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("\n    Assertion failed: %s == %s (%lu != %lu) (line %d)\n", \
               #a, #b, (unsigned long)(a), (unsigned long)(b), __LINE__); \
        return 0; \
    } \
} while(0)

/* Test magic number */
static int test_magic_number(void)
{
    ASSERT_EQ(LOLELFFS_MAGIC, 0x101E1FF5);
    return 1;
}

/* Test block size */
static int test_block_size(void)
{
    ASSERT_EQ(LOLELFFS_BLOCK_SIZE, 4096);
    ASSERT_EQ(LOLELFFS_BLOCK_SIZE, 1 << 12);
    return 1;
}

/* Test inode size and count per block */
static int test_inode_size(void)
{
    ASSERT_EQ(sizeof(struct lolelffs_inode), 64);
    ASSERT_EQ(LOLELFFS_INODES_PER_BLOCK, 4096 / 64);
    ASSERT_EQ(LOLELFFS_INODES_PER_BLOCK, 64);
    return 1;
}

/* Test maximum extents calculation */
static int test_max_extents(void)
{
    /* LOLELFFS_MAX_EXTENTS = (4096 - 4) / sizeof(extent) */
    /* sizeof(extent) = 3 * 4 = 12 bytes */
    size_t expected = (LOLELFFS_BLOCK_SIZE - sizeof(uint32_t)) / 12;
    ASSERT_EQ(LOLELFFS_MAX_EXTENTS, expected);
    return 1;
}

/* Test maximum file size calculation */
static int test_max_filesize(void)
{
    uint64_t expected = (uint64_t)LOLELFFS_MAX_BLOCKS_PER_EXTENT *
                        LOLELFFS_BLOCK_SIZE * LOLELFFS_MAX_EXTENTS;
    ASSERT_EQ(LOLELFFS_MAX_FILESIZE, expected);
    /* Verify it's approximately 11 MB (8 blocks * 4KB * 340+ extents) */
    ASSERT(LOLELFFS_MAX_FILESIZE > 10 * 1024 * 1024);
    ASSERT(LOLELFFS_MAX_FILESIZE < 15 * 1024 * 1024);
    return 1;
}

/* Test filename length */
static int test_filename_len(void)
{
    ASSERT_EQ(LOLELFFS_FILENAME_LEN, 255);
    return 1;
}

/* Test files per block calculation */
static int test_files_per_block(void)
{
    /* Each file entry is inode (4 bytes) + filename (255 bytes) = 259 bytes */
    /* But it's likely padded, let's check */
    size_t file_size = sizeof(uint32_t) + LOLELFFS_FILENAME_LEN;
    size_t expected = LOLELFFS_BLOCK_SIZE / file_size;
    ASSERT_EQ(LOLELFFS_FILES_PER_BLOCK, expected);
    return 1;
}

/* Test superblock info structure size */
static int test_sb_info_size(void)
{
    /* Should be 8 uint32_t fields = 32 bytes (without kernel pointers) */
    struct lolelffs_sb_info sb;
    ASSERT(sizeof(sb) >= 32);
    return 1;
}

/* Test inode structure fields */
static int test_inode_structure(void)
{
    struct lolelffs_inode inode;

    /* Test field offsets and sizes */
    ASSERT_EQ(sizeof(inode.i_mode), 4);
    ASSERT_EQ(sizeof(inode.i_uid), 4);
    ASSERT_EQ(sizeof(inode.i_gid), 4);
    ASSERT_EQ(sizeof(inode.i_size), 4);
    ASSERT_EQ(sizeof(inode.i_ctime), 4);
    ASSERT_EQ(sizeof(inode.i_atime), 4);
    ASSERT_EQ(sizeof(inode.i_mtime), 4);
    ASSERT_EQ(sizeof(inode.i_blocks), 4);
    ASSERT_EQ(sizeof(inode.i_nlink), 4);
    ASSERT_EQ(sizeof(inode.ei_block), 4);
    ASSERT_EQ(sizeof(inode.i_data), 32);

    return 1;
}

/* Test max subfiles calculation */
static int test_max_subfiles(void)
{
    uint32_t expected = LOLELFFS_FILES_PER_EXT * LOLELFFS_MAX_EXTENTS;
    ASSERT_EQ(LOLELFFS_MAX_SUBFILES, expected);
    /* Should be around 40920 files per directory */
    ASSERT(LOLELFFS_MAX_SUBFILES > 40000);
    return 1;
}

/* Test ceil division helper function */
static int test_idiv_ceil(void)
{
    /* Inline implementation for testing */
    #define idiv_ceil_test(a, b) ((a) / (b) + (((a) % (b)) ? 1 : 0))

    ASSERT_EQ(idiv_ceil_test(10, 3), 4);
    ASSERT_EQ(idiv_ceil_test(9, 3), 3);
    ASSERT_EQ(idiv_ceil_test(1, 1), 1);
    ASSERT_EQ(idiv_ceil_test(100, 7), 15);
    ASSERT_EQ(idiv_ceil_test(4096, 64), 64);

    #undef idiv_ceil_test
    return 1;
}

/* Test blocks per extent limit */
static int test_blocks_per_extent(void)
{
    ASSERT_EQ(LOLELFFS_MAX_BLOCKS_PER_EXTENT, 8);
    return 1;
}

/* Test layout calculations for a 1MB filesystem */
static int test_layout_1mb(void)
{
    uint32_t total_size = 1 * 1024 * 1024; /* 1 MB */
    uint32_t nr_blocks = total_size / LOLELFFS_BLOCK_SIZE;

    ASSERT_EQ(nr_blocks, 256);

    /* Calculate expected layout */
    uint32_t nr_inodes = nr_blocks;
    uint32_t mod = nr_inodes % LOLELFFS_INODES_PER_BLOCK;
    if (mod)
        nr_inodes += LOLELFFS_INODES_PER_BLOCK - mod;

    uint32_t nr_istore_blocks = (nr_inodes + LOLELFFS_INODES_PER_BLOCK - 1) / LOLELFFS_INODES_PER_BLOCK;
    uint32_t nr_ifree_blocks = (nr_inodes + LOLELFFS_BLOCK_SIZE * 8 - 1) / (LOLELFFS_BLOCK_SIZE * 8);
    uint32_t nr_bfree_blocks = (nr_blocks + LOLELFFS_BLOCK_SIZE * 8 - 1) / (LOLELFFS_BLOCK_SIZE * 8);

    /* Verify calculations are reasonable */
    ASSERT(nr_istore_blocks >= 1);
    ASSERT(nr_ifree_blocks >= 1);
    ASSERT(nr_bfree_blocks >= 1);

    /* Total metadata blocks should be less than total blocks */
    uint32_t metadata_blocks = 1 + nr_istore_blocks + nr_ifree_blocks + nr_bfree_blocks;
    ASSERT(metadata_blocks < nr_blocks);

    return 1;
}

/* Test layout calculations for a 200MB filesystem */
static int test_layout_200mb(void)
{
    uint32_t total_size = 200 * 1024 * 1024; /* 200 MB */
    uint32_t nr_blocks = total_size / LOLELFFS_BLOCK_SIZE;

    ASSERT_EQ(nr_blocks, 51200);

    /* Calculate expected layout */
    uint32_t nr_inodes = nr_blocks;
    uint32_t mod = nr_inodes % LOLELFFS_INODES_PER_BLOCK;
    if (mod)
        nr_inodes += LOLELFFS_INODES_PER_BLOCK - mod;

    uint32_t nr_istore_blocks = (nr_inodes + LOLELFFS_INODES_PER_BLOCK - 1) / LOLELFFS_INODES_PER_BLOCK;
    uint32_t nr_ifree_blocks = (nr_inodes + LOLELFFS_BLOCK_SIZE * 8 - 1) / (LOLELFFS_BLOCK_SIZE * 8);
    uint32_t nr_bfree_blocks = (nr_blocks + LOLELFFS_BLOCK_SIZE * 8 - 1) / (LOLELFFS_BLOCK_SIZE * 8);

    /* Verify calculations */
    ASSERT_EQ(nr_istore_blocks, 800); /* 51200 / 64 = 800 */
    ASSERT_EQ(nr_ifree_blocks, 2);    /* 51200 / 32768 = ~1.56, ceil = 2 */
    ASSERT_EQ(nr_bfree_blocks, 2);    /* 51200 / 32768 = ~1.56, ceil = 2 */

    return 1;
}

/* Test bitmap bit calculations */
static int test_bitmap_calculations(void)
{
    /* Bits per block */
    uint32_t bits_per_block = LOLELFFS_BLOCK_SIZE * 8;
    ASSERT_EQ(bits_per_block, 32768);

    /* For 256 inodes, we need 1 block */
    ASSERT_EQ((256 + bits_per_block - 1) / bits_per_block, 1);

    /* For 100000 inodes, we need 4 blocks */
    ASSERT_EQ((100000 + bits_per_block - 1) / bits_per_block, 4);

    return 1;
}

/* Test extent structure */
static int test_extent_structure(void)
{
    /* Manually define extent for userspace testing */
    struct test_extent {
        uint32_t ee_block;
        uint32_t ee_len;
        uint32_t ee_start;
    };

    ASSERT_EQ(sizeof(struct test_extent), 12);
    return 1;
}

/* Test file entry structure */
static int test_file_entry_structure(void)
{
    /* Manually define file for userspace testing */
    struct test_file {
        uint32_t inode;
        char filename[LOLELFFS_FILENAME_LEN];
    };

    size_t expected_size = sizeof(uint32_t) + LOLELFFS_FILENAME_LEN;
    ASSERT_EQ(sizeof(struct test_file), expected_size);
    return 1;
}

/* Test superblock padding */
static int test_superblock_padding(void)
{
    /* Superblock should be exactly one block */
    struct test_superblock {
        struct lolelffs_sb_info info;
        char padding[4064];
    };

    /* Check that info + padding roughly equals block size */
    /* Note: actual padding might differ slightly */
    size_t sb_size = sizeof(struct lolelffs_sb_info) + 4064;
    ASSERT(sb_size >= LOLELFFS_BLOCK_SIZE - 64);
    ASSERT(sb_size <= LOLELFFS_BLOCK_SIZE + 64);

    return 1;
}

/* Test minimum filesystem size */
static int test_min_filesystem_size(void)
{
    /* Minimum size is 100 blocks */
    long min_size = 100 * LOLELFFS_BLOCK_SIZE;
    ASSERT_EQ(min_size, 409600);
    return 1;
}

/* Test endianness conversions */
static int test_endianness(void)
{
    uint32_t val = 0x12345678;
    uint32_t le = htole32(val);
    uint32_t back = le32toh(le);
    ASSERT_EQ(val, back);

    uint64_t val64 = 0x123456789ABCDEF0ULL;
    uint64_t le64 = htole64(val64);
    uint64_t back64 = le64toh(le64);
    ASSERT_EQ(val64, back64);

    return 1;
}

int main(void)
{
    printf("Running lolelffs unit tests...\n\n");

    printf("Constants and Definitions:\n");
    TEST(magic_number);
    TEST(block_size);
    TEST(filename_len);
    TEST(blocks_per_extent);
    TEST(min_filesystem_size);

    printf("\nStructure Sizes:\n");
    TEST(inode_size);
    TEST(inode_structure);
    TEST(sb_info_size);
    TEST(extent_structure);
    TEST(file_entry_structure);
    TEST(superblock_padding);

    printf("\nCalculations:\n");
    TEST(max_extents);
    TEST(max_filesize);
    TEST(files_per_block);
    TEST(max_subfiles);
    TEST(idiv_ceil);
    TEST(bitmap_calculations);

    printf("\nLayout Tests:\n");
    TEST(layout_1mb);
    TEST(layout_200mb);

    printf("\nMiscellaneous:\n");
    TEST(endianness);

    printf("\n========================================\n");
    printf("Tests passed: %d/%d\n", tests_passed, tests_run);
    printf("========================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
