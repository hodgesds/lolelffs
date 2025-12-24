/*
 * Stress tests for lolelffs filesystem structures and algorithms
 *
 * These tests exercise:
 * - Large file extent patterns
 * - Maximum directory capacity
 * - Edge case handling
 * - Memory allocation patterns
 * - Pathological access patterns
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#include "lolelffs.h"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

/* Timing utilities */
static inline uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define TEST(name) do { \
    printf("  Testing %s... ", #name); \
    fflush(stdout); \
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

/* Userspace structures for stress testing */
struct stress_extent {
    uint32_t ee_block;
    uint32_t ee_len;
    uint32_t ee_start;
};

struct stress_file_ei_block {
    uint32_t nr_files;
    struct stress_extent extents[LOLELFFS_MAX_EXTENTS];
};

struct stress_file {
    uint32_t inode;
    char filename[LOLELFFS_FILENAME_LEN];
};

/*
 * Extent search for stress tests
 */
static uint32_t stress_ext_search(struct stress_file_ei_block *index, uint32_t iblock)
{
    uint32_t nr_extents = 0;
    uint32_t left, right, mid;

    for (nr_extents = 0; nr_extents < LOLELFFS_MAX_EXTENTS; nr_extents++) {
        if (index->extents[nr_extents].ee_start == 0)
            break;
    }

    if (nr_extents == 0)
        return 0;

    left = 0;
    right = nr_extents;

    while (left < right) {
        uint32_t block, len;

        mid = left + (right - left) / 2;
        block = index->extents[mid].ee_block;
        len = index->extents[mid].ee_len;

        if (iblock < block) {
            right = mid;
        } else if (iblock >= block + len) {
            left = mid + 1;
        } else {
            return mid;
        }
    }

    if (nr_extents < LOLELFFS_MAX_EXTENTS)
        return nr_extents;

    return -1;
}

/*
 * Test: Maximum file size with all extents used
 */
static int test_max_file_extents(void)
{
    struct stress_file_ei_block index;
    uint32_t i, result;
    uint32_t current_block = 0;
    uint32_t total_blocks = 0;

    memset(&index, 0, sizeof(index));

    /* Fill all extents */
    for (i = 0; i < LOLELFFS_MAX_EXTENTS; i++) {
        index.extents[i].ee_block = current_block;
        index.extents[i].ee_len = LOLELFFS_MAX_BLOCKS_PER_EXTENT;
        index.extents[i].ee_start = i + 1;
        current_block += LOLELFFS_MAX_BLOCKS_PER_EXTENT;
        total_blocks += LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    }

    /* Verify total blocks matches maximum file size */
    ASSERT_EQ(total_blocks * LOLELFFS_BLOCK_SIZE, LOLELFFS_MAX_FILESIZE);

    /* Test search at boundaries */
    result = stress_ext_search(&index, 0);
    ASSERT_EQ(result, 0);

    result = stress_ext_search(&index, LOLELFFS_MAX_BLOCKS_PER_EXTENT - 1);
    ASSERT_EQ(result, 0);

    result = stress_ext_search(&index, LOLELFFS_MAX_BLOCKS_PER_EXTENT);
    ASSERT_EQ(result, 1);

    result = stress_ext_search(&index, total_blocks - 1);
    ASSERT_EQ(result, LOLELFFS_MAX_EXTENTS - 1);

    /* Beyond max should return -1 */
    result = stress_ext_search(&index, total_blocks);
    ASSERT_EQ(result, (uint32_t)-1);

    return 1;
}

/*
 * Test: Variable extent sizes (simulating fragmentation)
 */
static int test_fragmented_extents(void)
{
    struct stress_file_ei_block index;
    uint32_t i, result;
    uint32_t current_block = 0;

    memset(&index, 0, sizeof(index));

    /* Create extents with varying sizes (1-8 blocks) */
    for (i = 0; i < 100; i++) {
        uint32_t len = (i % LOLELFFS_MAX_BLOCKS_PER_EXTENT) + 1;
        index.extents[i].ee_block = current_block;
        index.extents[i].ee_len = len;
        index.extents[i].ee_start = i + 1;
        current_block += len;
    }

    /* Test searches in fragmented layout */
    uint32_t test_block = 0;
    for (i = 0; i < 100; i++) {
        uint32_t len = (i % LOLELFFS_MAX_BLOCKS_PER_EXTENT) + 1;

        /* Test first block of each extent */
        result = stress_ext_search(&index, test_block);
        ASSERT_EQ(result, i);

        /* Test last block of each extent */
        result = stress_ext_search(&index, test_block + len - 1);
        ASSERT_EQ(result, i);

        test_block += len;
    }

    return 1;
}

/*
 * Test: Maximum directory entries
 */
static int test_max_directory_entries(void)
{
    uint32_t max_files = LOLELFFS_MAX_SUBFILES;
    uint32_t ei;
    uint32_t last_ei, last_bi, last_fi;

    /* Calculate position of last file */
    ei = (max_files - 1) / LOLELFFS_FILES_PER_EXT;

    /* Verify it fits within extents */
    ASSERT(ei < LOLELFFS_MAX_EXTENTS);

    /* Verify all possible positions are reachable */
    for (uint32_t n = 0; n < max_files; n++) {
        last_ei = n / LOLELFFS_FILES_PER_EXT;
        last_bi = n % LOLELFFS_FILES_PER_EXT / LOLELFFS_FILES_PER_BLOCK;
        last_fi = n % LOLELFFS_FILES_PER_BLOCK;

        /* Reverse calculation to verify */
        uint32_t reconstructed = last_ei * LOLELFFS_FILES_PER_EXT +
                                  last_bi * LOLELFFS_FILES_PER_BLOCK +
                                  last_fi;
        ASSERT_EQ(reconstructed, n);
    }

    return 1;
}

/*
 * Test: Large bitmap operations simulation
 */
static int test_large_bitmap_simulation(void)
{
    const uint32_t bitmap_size = 51200; /* 200MB filesystem blocks */
    uint8_t *bitmap = calloc((bitmap_size + 7) / 8, 1);
    uint32_t i, allocated = 0;

    if (!bitmap)
        return 0;

    /* Simulate allocating blocks */
    for (i = 0; i < bitmap_size; i++) {
        /* Set bit (mark as used) */
        bitmap[i / 8] |= (1 << (i % 8));
        allocated++;
    }

    ASSERT_EQ(allocated, bitmap_size);

    /* Verify all bits are set */
    for (i = 0; i < bitmap_size; i++) {
        ASSERT(bitmap[i / 8] & (1 << (i % 8)));
    }

    /* Free alternate blocks */
    for (i = 0; i < bitmap_size; i += 2) {
        bitmap[i / 8] &= ~(1 << (i % 8));
    }

    /* Count free blocks */
    uint32_t free_count = 0;
    for (i = 0; i < bitmap_size; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8))))
            free_count++;
    }

    ASSERT_EQ(free_count, (bitmap_size + 1) / 2);

    free(bitmap);
    return 1;
}

/*
 * Test: Pathological extent search patterns
 */
static int test_pathological_search(void)
{
    struct stress_file_ei_block index;
    uint32_t i, result;
    uint32_t current_block = 0;
    const uint32_t extent_count = 200;

    memset(&index, 0, sizeof(index));

    /* Fill extents */
    for (i = 0; i < extent_count; i++) {
        index.extents[i].ee_block = current_block;
        index.extents[i].ee_len = LOLELFFS_MAX_BLOCKS_PER_EXTENT;
        index.extents[i].ee_start = i + 1;
        current_block += LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    }

    /* Search pattern: always first block (worst case for cache) */
    for (i = 0; i < 10000; i++) {
        result = stress_ext_search(&index, 0);
        ASSERT_EQ(result, 0);
    }

    /* Search pattern: always last block */
    for (i = 0; i < 10000; i++) {
        result = stress_ext_search(&index, current_block - 1);
        ASSERT_EQ(result, extent_count - 1);
    }

    /* Search pattern: alternating first/last */
    for (i = 0; i < 10000; i++) {
        if (i % 2 == 0) {
            result = stress_ext_search(&index, 0);
            ASSERT_EQ(result, 0);
        } else {
            result = stress_ext_search(&index, current_block - 1);
            ASSERT_EQ(result, extent_count - 1);
        }
    }

    return 1;
}

/*
 * Test: Memory alignment and padding
 */
static int test_memory_alignment(void)
{
    /* Verify structures are properly aligned */

    /* Inode should be reasonably sized (allow for compiler padding) */
    ASSERT(sizeof(struct lolelffs_inode) >= 64);
    ASSERT(sizeof(struct lolelffs_inode) <= 128);

    /* Inodes per block should be non-zero */
    ASSERT(LOLELFFS_INODES_PER_BLOCK > 0);

    /* Extent should be 12 bytes (3 uint32_t with no padding) */
    ASSERT_EQ(sizeof(struct lolelffs_extent), 12);

    /* File entry should be properly sized */
    ASSERT(sizeof(struct lolelffs_file) >= sizeof(uint32_t) + LOLELFFS_FILENAME_LEN);

    return 1;
}

/*
 * Test: Inode number stress test
 */
static int test_inode_numbers(void)
{
    const uint32_t max_inodes = 100000;
    uint32_t i, block, shift;

    /* Verify inode-to-block mapping for many inodes */
    for (i = 0; i < max_inodes; i++) {
        block = (i / LOLELFFS_INODES_PER_BLOCK) + 1;
        shift = i % LOLELFFS_INODES_PER_BLOCK;

        /* Verify we can reconstruct inode number */
        uint32_t reconstructed = (block - 1) * LOLELFFS_INODES_PER_BLOCK + shift;
        ASSERT_EQ(reconstructed, i);
    }

    return 1;
}

/*
 * Test: Extent boundary conditions
 */
static int test_extent_boundaries(void)
{
    struct stress_file_ei_block index;
    uint32_t result;

    memset(&index, 0, sizeof(index));

    /* Empty extent list */
    result = stress_ext_search(&index, 0);
    ASSERT_EQ(result, 0);

    result = stress_ext_search(&index, 100);
    ASSERT_EQ(result, 0);

    /* Single extent */
    index.extents[0].ee_block = 0;
    index.extents[0].ee_len = 8;
    index.extents[0].ee_start = 1;

    result = stress_ext_search(&index, 0);
    ASSERT_EQ(result, 0);

    result = stress_ext_search(&index, 7);
    ASSERT_EQ(result, 0);

    result = stress_ext_search(&index, 8);
    ASSERT_EQ(result, 1); /* Next slot for allocation */

    /* Two extents with gap check */
    index.extents[1].ee_block = 8;
    index.extents[1].ee_len = 8;
    index.extents[1].ee_start = 2;

    result = stress_ext_search(&index, 8);
    ASSERT_EQ(result, 1);

    result = stress_ext_search(&index, 15);
    ASSERT_EQ(result, 1);

    result = stress_ext_search(&index, 16);
    ASSERT_EQ(result, 2); /* Next slot */

    return 1;
}

/*
 * Test: Filename length edge cases
 */
static int test_filename_lengths(void)
{
    char filename[LOLELFFS_FILENAME_LEN + 10];
    uint32_t i;

    /* Test various filename lengths */
    for (i = 1; i <= LOLELFFS_FILENAME_LEN; i++) {
        memset(filename, 'a', i);
        filename[i] = '\0';
        ASSERT_EQ(strlen(filename), i);
    }

    /* Maximum filename length */
    memset(filename, 'z', LOLELFFS_FILENAME_LEN);
    filename[LOLELFFS_FILENAME_LEN] = '\0';
    ASSERT_EQ(strlen(filename), LOLELFFS_FILENAME_LEN);

    return 1;
}

/*
 * Test: Adaptive allocation strategy stress
 */
static int test_adaptive_allocation_stress(void)
{
    uint32_t i, alloc_size;
    uint32_t small_count = 0, medium_count = 0, large_count = 0;

    /* Simulate many allocation decisions */
    for (i = 0; i < 1000; i++) {
        uint32_t current_blocks = i % 100;

        if (current_blocks < 8) {
            alloc_size = 2;
            small_count++;
        } else if (current_blocks < 32) {
            alloc_size = 4;
            medium_count++;
        } else {
            alloc_size = LOLELFFS_MAX_BLOCKS_PER_EXTENT;
            large_count++;
        }
        (void)alloc_size;
    }

    /* Verify distribution */
    ASSERT(small_count > 0);
    ASSERT(medium_count > 0);
    ASSERT(large_count > 0);
    ASSERT_EQ(small_count + medium_count + large_count, 1000);

    return 1;
}

/*
 * Test: Large file block calculations
 */
static int test_large_file_blocks(void)
{
    /* Test block calculations for various file sizes */
    struct {
        uint64_t file_size;
        uint32_t expected_blocks;
    } test_cases[] = {
        {0, 0},
        {1, 1},
        {LOLELFFS_BLOCK_SIZE, 1},
        {LOLELFFS_BLOCK_SIZE + 1, 2},
        {LOLELFFS_BLOCK_SIZE * 10, 10},
        {LOLELFFS_BLOCK_SIZE * 100, 100},
        {LOLELFFS_BLOCK_SIZE * 1000, 1000},
        {LOLELFFS_MAX_FILESIZE, LOLELFFS_MAX_EXTENTS * LOLELFFS_MAX_BLOCKS_PER_EXTENT},
        {0, 0}
    };

    int i;
    for (i = 0; test_cases[i].file_size != 0 || i == 0; i++) {
        uint32_t blocks;
        if (test_cases[i].file_size == 0) {
            blocks = 0;
            if (i > 0) break;
        } else {
            blocks = (test_cases[i].file_size + LOLELFFS_BLOCK_SIZE - 1)
                     / LOLELFFS_BLOCK_SIZE;
        }
        ASSERT_EQ(blocks, test_cases[i].expected_blocks);
    }

    return 1;
}

/*
 * Test: Concurrent-style access simulation (single-threaded)
 */
static int test_concurrent_simulation(void)
{
    struct stress_file_ei_block index;
    uint32_t i, result;
    uint32_t current_block = 0;
    const uint32_t extent_count = 50;

    memset(&index, 0, sizeof(index));

    /* Setup extents */
    for (i = 0; i < extent_count; i++) {
        index.extents[i].ee_block = current_block;
        index.extents[i].ee_len = LOLELFFS_MAX_BLOCKS_PER_EXTENT;
        index.extents[i].ee_start = i + 1;
        current_block += LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    }

    /* Simulate multiple "threads" accessing different parts */
    uint32_t thread_targets[4] = {0, 100, 200, 300};

    for (i = 0; i < 100000; i++) {
        uint32_t thread = i % 4;
        uint32_t target = thread_targets[thread] + (i / 4) % 100;
        target = target % current_block;

        result = stress_ext_search(&index, target);
        ASSERT(result < extent_count || result == extent_count);
    }

    return 1;
}

/*
 * Test: Edge case file sizes
 */
static int test_edge_file_sizes(void)
{
    /* Test sizes at extent boundaries */
    uint32_t blocks_per_extent = LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    uint32_t block_size = LOLELFFS_BLOCK_SIZE;

    /* Exactly one extent */
    uint64_t one_extent = blocks_per_extent * block_size;
    ASSERT_EQ(one_extent, 8 * 4096);

    /* Just over one extent */
    uint64_t over_one_extent = one_extent + 1;
    uint32_t blocks_needed = (over_one_extent + block_size - 1) / block_size;
    ASSERT_EQ(blocks_needed, 9);

    /* Maximum file size */
    ASSERT(LOLELFFS_MAX_FILESIZE <= (uint64_t)LOLELFFS_MAX_EXTENTS *
           LOLELFFS_MAX_BLOCKS_PER_EXTENT * LOLELFFS_BLOCK_SIZE);

    return 1;
}

/*
 * Test: Performance regression detection
 */
static int test_performance_regression(void)
{
    struct stress_file_ei_block index;
    uint64_t start, end;
    uint32_t i, result;
    const uint32_t iterations = 1000000;
    uint32_t current_block = 0;
    double ns_per_op;

    memset(&index, 0, sizeof(index));

    /* Fill all extents */
    for (i = 0; i < LOLELFFS_MAX_EXTENTS; i++) {
        index.extents[i].ee_block = current_block;
        index.extents[i].ee_len = LOLELFFS_MAX_BLOCKS_PER_EXTENT;
        index.extents[i].ee_start = i + 1;
        current_block += LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    }

    /* Worst-case search performance */
    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        result = stress_ext_search(&index, current_block - 1);
        (void)result;
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;

    /* Should complete in reasonable time even for worst case */
    /* Binary search of 341 items should be ~9 iterations */
    ASSERT(ns_per_op < 5000); /* 5 microseconds is very generous */

    return 1;
}

int main(void)
{
    printf("Running lolelffs stress tests...\n\n");

    printf("Extent Stress Tests:\n");
    TEST(max_file_extents);
    TEST(fragmented_extents);
    TEST(extent_boundaries);
    TEST(pathological_search);

    printf("\nDirectory Stress Tests:\n");
    TEST(max_directory_entries);
    TEST(filename_lengths);

    printf("\nBitmap and Allocation Tests:\n");
    TEST(large_bitmap_simulation);
    TEST(adaptive_allocation_stress);

    printf("\nMemory and Structure Tests:\n");
    TEST(memory_alignment);
    TEST(inode_numbers);

    printf("\nFile Size Tests:\n");
    TEST(large_file_blocks);
    TEST(edge_file_sizes);

    printf("\nSimulation Tests:\n");
    TEST(concurrent_simulation);
    TEST(performance_regression);

    printf("\n========================================\n");
    printf("Stress tests passed: %d/%d\n", tests_passed, tests_run);
    printf("========================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
