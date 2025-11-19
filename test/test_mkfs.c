/*
 * Tests for mkfs.lolelffs functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

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

/* Superblock structure for reading */
struct superblock {
    struct lolelffs_sb_info info;
    char padding[4064];
};

/* Helper function to create a test image */
static int create_test_image(const char *filename, size_t size_mb)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "dd if=/dev/zero of=%s bs=1M count=%zu status=none 2>/dev/null",
             filename, size_mb);
    return system(cmd);
}

/* Helper function to run mkfs on an image */
static int run_mkfs(const char *filename)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "./mkfs.lolelffs %s > /dev/null 2>&1", filename);
    return system(cmd);
}

/* Helper function to read superblock */
static int read_superblock(const char *filename, struct superblock *sb)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;

    ssize_t ret = read(fd, sb, sizeof(struct superblock));
    close(fd);

    return (ret == sizeof(struct superblock)) ? 0 : -1;
}

/* Test creating a 1MB filesystem */
static int test_create_1mb(void)
{
    const char *img = "test/test_1mb.img";

    ASSERT(create_test_image(img, 1) == 0);
    ASSERT(run_mkfs(img) == 0);

    struct superblock sb;
    ASSERT(read_superblock(img, &sb) == 0);

    /* Verify magic number */
    ASSERT_EQ(le32toh(sb.info.magic), LOLELFFS_MAGIC);

    /* Verify block count (1MB / 4KB = 256 blocks) */
    ASSERT_EQ(le32toh(sb.info.nr_blocks), 256);

    /* Verify free inodes (total - 1 for root) */
    uint32_t nr_inodes = le32toh(sb.info.nr_inodes);
    ASSERT_EQ(le32toh(sb.info.nr_free_inodes), nr_inodes - 1);

    unlink(img);
    return 1;
}

/* Test creating a 10MB filesystem */
static int test_create_10mb(void)
{
    const char *img = "test/test_10mb.img";

    ASSERT(create_test_image(img, 10) == 0);
    ASSERT(run_mkfs(img) == 0);

    struct superblock sb;
    ASSERT(read_superblock(img, &sb) == 0);

    ASSERT_EQ(le32toh(sb.info.magic), LOLELFFS_MAGIC);
    ASSERT_EQ(le32toh(sb.info.nr_blocks), 2560);

    unlink(img);
    return 1;
}

/* Test creating a 100MB filesystem */
static int test_create_100mb(void)
{
    const char *img = "test/test_100mb.img";

    ASSERT(create_test_image(img, 100) == 0);
    ASSERT(run_mkfs(img) == 0);

    struct superblock sb;
    ASSERT(read_superblock(img, &sb) == 0);

    ASSERT_EQ(le32toh(sb.info.magic), LOLELFFS_MAGIC);
    ASSERT_EQ(le32toh(sb.info.nr_blocks), 25600);

    unlink(img);
    return 1;
}

/* Test that mkfs fails on too-small image */
static int test_too_small_image(void)
{
    const char *img = "test/test_small.img";

    /* Create image smaller than minimum (100 blocks = 400KB) */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "dd if=/dev/zero of=%s bs=1K count=100 status=none 2>/dev/null", img);
    ASSERT(system(cmd) == 0);

    /* mkfs should fail */
    int ret = run_mkfs(img);
    ASSERT(ret != 0);

    unlink(img);
    return 1;
}

/* Test that mkfs fails on nonexistent file */
static int test_nonexistent_file(void)
{
    int ret = run_mkfs("/nonexistent/path/to/file.img");
    ASSERT(ret != 0);
    return 1;
}

/* Test superblock layout correctness */
static int test_superblock_layout(void)
{
    const char *img = "test/test_layout.img";

    ASSERT(create_test_image(img, 5) == 0);
    ASSERT(run_mkfs(img) == 0);

    struct superblock sb;
    ASSERT(read_superblock(img, &sb) == 0);

    uint32_t nr_blocks = le32toh(sb.info.nr_blocks);
    uint32_t nr_inodes = le32toh(sb.info.nr_inodes);
    uint32_t nr_istore_blocks = le32toh(sb.info.nr_istore_blocks);
    uint32_t nr_ifree_blocks = le32toh(sb.info.nr_ifree_blocks);
    uint32_t nr_bfree_blocks = le32toh(sb.info.nr_bfree_blocks);
    uint32_t nr_free_blocks = le32toh(sb.info.nr_free_blocks);

    /* Verify inodes are aligned to block boundary */
    ASSERT_EQ(nr_inodes % LOLELFFS_INODES_PER_BLOCK, 0);

    /* Verify istore block count */
    ASSERT_EQ(nr_istore_blocks, nr_inodes / LOLELFFS_INODES_PER_BLOCK);

    /* Verify we have enough space for data */
    uint32_t metadata = 1 + nr_istore_blocks + nr_ifree_blocks + nr_bfree_blocks;
    ASSERT(metadata < nr_blocks);

    /* Verify free blocks count is correct */
    /* Free blocks = total - metadata - 1 (for root dir data block) */
    ASSERT_EQ(nr_free_blocks, nr_blocks - metadata - 1);

    unlink(img);
    return 1;
}

/* Test inode bitmap initialization */
static int test_inode_bitmap(void)
{
    const char *img = "test/test_ibitmap.img";

    ASSERT(create_test_image(img, 1) == 0);
    ASSERT(run_mkfs(img) == 0);

    struct superblock sb;
    ASSERT(read_superblock(img, &sb) == 0);

    uint32_t nr_istore_blocks = le32toh(sb.info.nr_istore_blocks);

    /* Read inode bitmap (starts after inode store) */
    int fd = open(img, O_RDONLY);
    ASSERT(fd >= 0);

    /* Seek to inode bitmap */
    off_t bitmap_offset = (1 + nr_istore_blocks) * LOLELFFS_BLOCK_SIZE;
    ASSERT(lseek(fd, bitmap_offset, SEEK_SET) == bitmap_offset);

    /* Read first uint64 of bitmap */
    uint64_t bitmap_word;
    ASSERT(read(fd, &bitmap_word, sizeof(bitmap_word)) == sizeof(bitmap_word));

    /* First bit should be 0 (inode 0 is used for root), rest should be 1 */
    uint64_t expected = htole64(0xfffffffffffffffe);
    ASSERT_EQ(bitmap_word, expected);

    close(fd);
    unlink(img);
    return 1;
}

/* Test block bitmap initialization */
static int test_block_bitmap(void)
{
    const char *img = "test/test_bbitmap.img";

    ASSERT(create_test_image(img, 1) == 0);
    ASSERT(run_mkfs(img) == 0);

    struct superblock sb;
    ASSERT(read_superblock(img, &sb) == 0);

    uint32_t nr_istore_blocks = le32toh(sb.info.nr_istore_blocks);
    uint32_t nr_ifree_blocks = le32toh(sb.info.nr_ifree_blocks);

    /* Read block bitmap */
    int fd = open(img, O_RDONLY);
    ASSERT(fd >= 0);

    /* Seek to block bitmap */
    off_t bitmap_offset = (1 + nr_istore_blocks + nr_ifree_blocks) * LOLELFFS_BLOCK_SIZE;
    ASSERT(lseek(fd, bitmap_offset, SEEK_SET) == bitmap_offset);

    /* Read first bytes of bitmap */
    uint8_t bitmap[8];
    ASSERT(read(fd, bitmap, sizeof(bitmap)) == sizeof(bitmap));

    /* First several bits should be 0 (used blocks), then 1s */
    /* Used blocks: superblock + istore + ifree + bfree + root data block */
    uint32_t nr_used = 1 + nr_istore_blocks + nr_ifree_blocks +
                       le32toh(sb.info.nr_bfree_blocks) + 1;

    /* Verify first bit is 0 (superblock is used) */
    ASSERT_EQ(bitmap[0] & 0x01, 0);

    close(fd);
    unlink(img);
    return 1;
}

/* Test root inode initialization */
static int test_root_inode(void)
{
    const char *img = "test/test_root.img";

    ASSERT(create_test_image(img, 1) == 0);
    ASSERT(run_mkfs(img) == 0);

    /* Read root inode (first inode in inode store, block 1) */
    int fd = open(img, O_RDONLY);
    ASSERT(fd >= 0);

    /* Seek to inode store (block 1) */
    ASSERT(lseek(fd, LOLELFFS_BLOCK_SIZE, SEEK_SET) == LOLELFFS_BLOCK_SIZE);

    struct lolelffs_inode root;
    ASSERT(read(fd, &root, sizeof(root)) == sizeof(root));

    /* Verify root inode properties */
    uint32_t mode = le32toh(root.i_mode);
    ASSERT((mode & S_IFMT) == S_IFDIR);  /* Is directory */
    ASSERT(mode & S_IRUSR);  /* Owner read */
    ASSERT(mode & S_IWUSR);  /* Owner write */
    ASSERT(mode & S_IXUSR);  /* Owner execute */

    ASSERT_EQ(le32toh(root.i_uid), 0);
    ASSERT_EQ(le32toh(root.i_gid), 0);
    ASSERT_EQ(le32toh(root.i_size), LOLELFFS_BLOCK_SIZE);
    ASSERT_EQ(le32toh(root.i_blocks), 1);
    ASSERT_EQ(le32toh(root.i_nlink), 2);  /* . and .. */

    /* Verify ei_block points to valid data block */
    uint32_t ei_block = le32toh(root.ei_block);
    ASSERT(ei_block > 0);

    close(fd);
    unlink(img);
    return 1;
}

/* Test root directory extent block initialization */
static int test_root_extent_block(void)
{
    const char *img = "test/test_extent.img";

    ASSERT(create_test_image(img, 1) == 0);
    ASSERT(run_mkfs(img) == 0);

    /* Read superblock to get layout */
    struct superblock sb;
    ASSERT(read_superblock(img, &sb) == 0);

    /* Calculate first data block position */
    uint32_t first_data = 1 + le32toh(sb.info.nr_istore_blocks) +
                          le32toh(sb.info.nr_ifree_blocks) +
                          le32toh(sb.info.nr_bfree_blocks);

    int fd = open(img, O_RDONLY);
    ASSERT(fd >= 0);

    /* Seek to root's extent block */
    off_t offset = first_data * LOLELFFS_BLOCK_SIZE;
    ASSERT(lseek(fd, offset, SEEK_SET) == offset);

    /* Read nr_files (first 4 bytes) */
    uint32_t nr_files;
    ASSERT(read(fd, &nr_files, sizeof(nr_files)) == sizeof(nr_files));

    /* Should be 0 for empty directory */
    ASSERT_EQ(le32toh(nr_files), 0);

    close(fd);
    unlink(img);
    return 1;
}

/* Test multiple consecutive mkfs operations */
static int test_multiple_mkfs(void)
{
    const char *img = "test/test_multi.img";

    ASSERT(create_test_image(img, 2) == 0);

    /* Run mkfs multiple times */
    for (int i = 0; i < 3; i++) {
        ASSERT(run_mkfs(img) == 0);

        struct superblock sb;
        ASSERT(read_superblock(img, &sb) == 0);
        ASSERT_EQ(le32toh(sb.info.magic), LOLELFFS_MAGIC);
    }

    unlink(img);
    return 1;
}

/* Test that second inode is free */
static int test_second_inode_free(void)
{
    const char *img = "test/test_second.img";

    ASSERT(create_test_image(img, 1) == 0);
    ASSERT(run_mkfs(img) == 0);

    /* Read second inode */
    int fd = open(img, O_RDONLY);
    ASSERT(fd >= 0);

    /* Seek to second inode (offset by one inode size) */
    off_t offset = LOLELFFS_BLOCK_SIZE + sizeof(struct lolelffs_inode);
    ASSERT(lseek(fd, offset, SEEK_SET) == offset);

    struct lolelffs_inode inode;
    ASSERT(read(fd, &inode, sizeof(inode)) == sizeof(inode));

    /* Second inode should be zeroed */
    ASSERT_EQ(le32toh(inode.i_mode), 0);
    ASSERT_EQ(le32toh(inode.i_size), 0);
    ASSERT_EQ(le32toh(inode.i_nlink), 0);

    close(fd);
    unlink(img);
    return 1;
}

/* Test various filesystem sizes */
static int test_various_sizes(void)
{
    const char *img = "test/test_sizes.img";
    size_t sizes[] = {1, 2, 4, 8, 16, 32};

    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        ASSERT(create_test_image(img, sizes[i]) == 0);
        ASSERT(run_mkfs(img) == 0);

        struct superblock sb;
        ASSERT(read_superblock(img, &sb) == 0);

        /* Verify block count */
        uint32_t expected_blocks = (sizes[i] * 1024 * 1024) / LOLELFFS_BLOCK_SIZE;
        ASSERT_EQ(le32toh(sb.info.nr_blocks), expected_blocks);

        unlink(img);
    }

    return 1;
}

/* Test free block/inode accounting */
static int test_free_accounting(void)
{
    const char *img = "test/test_account.img";

    ASSERT(create_test_image(img, 5) == 0);
    ASSERT(run_mkfs(img) == 0);

    struct superblock sb;
    ASSERT(read_superblock(img, &sb) == 0);

    uint32_t nr_blocks = le32toh(sb.info.nr_blocks);
    uint32_t nr_inodes = le32toh(sb.info.nr_inodes);
    uint32_t nr_free_inodes = le32toh(sb.info.nr_free_inodes);
    uint32_t nr_free_blocks = le32toh(sb.info.nr_free_blocks);

    /* Free inodes should be total - 1 (root) */
    ASSERT_EQ(nr_free_inodes, nr_inodes - 1);

    /* Free blocks should be less than total (metadata + root data) */
    ASSERT(nr_free_blocks < nr_blocks);
    ASSERT(nr_free_blocks > 0);

    unlink(img);
    return 1;
}

/* Test that all blocks sum up correctly */
static int test_block_sum(void)
{
    const char *img = "test/test_sum.img";

    ASSERT(create_test_image(img, 10) == 0);
    ASSERT(run_mkfs(img) == 0);

    struct superblock sb;
    ASSERT(read_superblock(img, &sb) == 0);

    uint32_t nr_blocks = le32toh(sb.info.nr_blocks);
    uint32_t nr_istore = le32toh(sb.info.nr_istore_blocks);
    uint32_t nr_ifree = le32toh(sb.info.nr_ifree_blocks);
    uint32_t nr_bfree = le32toh(sb.info.nr_bfree_blocks);
    uint32_t nr_free = le32toh(sb.info.nr_free_blocks);

    /* Total = superblock(1) + istore + ifree + bfree + free + 1(root data) */
    uint32_t sum = 1 + nr_istore + nr_ifree + nr_bfree + nr_free + 1;
    ASSERT_EQ(sum, nr_blocks);

    unlink(img);
    return 1;
}

int main(void)
{
    printf("Running mkfs.lolelffs tests...\n\n");

    /* Check if mkfs.lolelffs exists */
    if (access("./mkfs.lolelffs", X_OK) != 0) {
        printf("Error: mkfs.lolelffs not found. Please build it first.\n");
        return 1;
    }

    printf("Basic Creation Tests:\n");
    TEST(create_1mb);
    TEST(create_10mb);
    TEST(create_100mb);

    printf("\nError Handling Tests:\n");
    TEST(too_small_image);
    TEST(nonexistent_file);

    printf("\nSuperblock Tests:\n");
    TEST(superblock_layout);
    TEST(free_accounting);
    TEST(block_sum);

    printf("\nBitmap Tests:\n");
    TEST(inode_bitmap);
    TEST(block_bitmap);

    printf("\nInode Tests:\n");
    TEST(root_inode);
    TEST(second_inode_free);
    TEST(root_extent_block);

    printf("\nMiscellaneous Tests:\n");
    TEST(multiple_mkfs);
    TEST(various_sizes);

    printf("\n========================================\n");
    printf("Tests passed: %d/%d\n", tests_passed, tests_run);
    printf("========================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
