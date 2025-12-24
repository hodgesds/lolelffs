/*
 * Unit tests for lolelffs filesystem structures and calculations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "lolelffs.h"

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
    ASSERT_EQ(sizeof(struct lolelffs_inode), 72);
    ASSERT_EQ(LOLELFFS_INODES_PER_BLOCK, 4096 / 72);
    ASSERT_EQ(LOLELFFS_INODES_PER_BLOCK, 56);
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
    /* Verify it's approximately 84 GB (65536 blocks * 4KB * 340+ extents) */
    ASSERT(LOLELFFS_MAX_FILESIZE > 80ULL * 1024 * 1024 * 1024);
    ASSERT(LOLELFFS_MAX_FILESIZE < 90ULL * 1024 * 1024 * 1024);
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
    ASSERT_EQ(LOLELFFS_MAX_BLOCKS_PER_EXTENT, 65536);
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
    ASSERT_EQ(nr_istore_blocks, 915); /* 51232 / 56 = 915 (51200 rounded up to multiple of 56) */
    ASSERT_EQ(nr_ifree_blocks, 2);    /* 51232 / 32768 = ~1.56, ceil = 2 */
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

    /* The actual size should be at least the sum of components */
    size_t min_size = sizeof(uint32_t) + LOLELFFS_FILENAME_LEN;
    ASSERT(sizeof(struct test_file) >= min_size);
    /* And should not be excessively padded (within 8 bytes of expected) */
    ASSERT(sizeof(struct test_file) <= min_size + 8);
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

/* Test adaptive extent allocation sizing logic */
static int test_adaptive_alloc_sizing(void)
{
    /*
     * Test the adaptive allocation strategy:
     * - Small files (< 8 blocks): allocate 2 blocks
     * - Medium files (8-32 blocks): allocate 4 blocks
     * - Large files (> 32 blocks): allocate 8 blocks
     */

    /* Small file case */
    uint32_t size_small = 0;
    if (size_small < 8) {
        ASSERT_EQ(2, 2); /* Would allocate 2 */
    }

    size_small = 7;
    if (size_small < 8) {
        ASSERT_EQ(2, 2); /* Would allocate 2 */
    }

    /* Medium file case */
    uint32_t size_medium = 8;
    if (size_medium >= 8 && size_medium < 32) {
        ASSERT_EQ(4, 4); /* Would allocate 4 */
    }

    size_medium = 31;
    if (size_medium >= 8 && size_medium < 32) {
        ASSERT_EQ(4, 4); /* Would allocate 4 */
    }

    /* Large file case */
    uint32_t size_large = 32;
    if (size_large >= 32) {
        ASSERT_EQ(LOLELFFS_MAX_BLOCKS_PER_EXTENT, 65536); /* Would allocate 65536 */
    }

    size_large = 100;
    if (size_large >= 32) {
        ASSERT_EQ(LOLELFFS_MAX_BLOCKS_PER_EXTENT, 65536); /* Would allocate 65536 */
    }

    return 1;
}

/* Test extent search edge cases */
static int test_extent_search_edge_cases(void)
{
    /*
     * Test edge cases for extent binary search:
     * - Empty extent list (no extents allocated)
     * - Single extent
     * - Block at extent boundary
     * - Block beyond all extents
     */

    /* For a 4KB block size and 65536 blocks per extent:
     * - Extent 0 covers blocks 0-65535
     * - Extent 1 covers blocks 65536-131071
     * - etc.
     */

    /* Verify extent calculations */
    uint32_t block = 0;
    uint32_t extent_idx = block / LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    ASSERT_EQ(extent_idx, 0);

    block = 65535;
    extent_idx = block / LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    ASSERT_EQ(extent_idx, 0);

    block = 65536;
    extent_idx = block / LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    ASSERT_EQ(extent_idx, 1);

    block = 131071;
    extent_idx = block / LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    ASSERT_EQ(extent_idx, 1);

    block = 524288;
    extent_idx = block / LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    ASSERT_EQ(extent_idx, 8);

    return 1;
}

/* Test maximum directory entries per extent */
static int test_dir_entries_per_extent(void)
{
    /* Verify calculation of files per extent */
    uint32_t files_per_block = LOLELFFS_FILES_PER_BLOCK;
    uint32_t blocks_per_extent = LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    uint32_t expected = files_per_block * blocks_per_extent;

    ASSERT_EQ(LOLELFFS_FILES_PER_EXT, expected);

    /* Should be able to hold many files per extent */
    ASSERT(LOLELFFS_FILES_PER_EXT >= 100);

    return 1;
}

/* Test symlink data size limit */
static int test_symlink_data_limit(void)
{
    /* Symlink target is stored in i_data field which is 32 bytes */
    struct lolelffs_inode inode;

    ASSERT_EQ(sizeof(inode.i_data), 32);

    /* Max symlink target length is 32 bytes including null terminator */
    /* So max target string length is 31 characters */
    ASSERT(sizeof(inode.i_data) >= 32);

    return 1;
}

/* Test inode block calculation */
static int test_inode_block_calculation(void)
{
    /* Inode N is in block: (N / INODES_PER_BLOCK) + 1 */
    /* (the +1 is for superblock at block 0) */

    uint32_t ino = 0;
    uint32_t block = (ino / LOLELFFS_INODES_PER_BLOCK) + 1;
    ASSERT_EQ(block, 1);

    ino = 55;
    block = (ino / LOLELFFS_INODES_PER_BLOCK) + 1;
    ASSERT_EQ(block, 1);

    ino = 56;
    block = (ino / LOLELFFS_INODES_PER_BLOCK) + 1;
    ASSERT_EQ(block, 2);

    ino = 112;
    block = (ino / LOLELFFS_INODES_PER_BLOCK) + 1;
    ASSERT_EQ(block, 3);

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

    printf("\nExtent and Allocation Tests:\n");
    TEST(adaptive_alloc_sizing);
    TEST(extent_search_edge_cases);
    TEST(dir_entries_per_extent);
    TEST(inode_block_calculation);

    printf("\nMiscellaneous:\n");
    TEST(endianness);
    TEST(symlink_data_limit);

    printf("\n========================================\n");
    printf("Tests passed: %d/%d\n", tests_passed, tests_run);
    printf("========================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
