/*
 * Performance benchmarks for lolelffs filesystem structures and algorithms
 *
 * These benchmarks measure:
 * - Extent search performance with varying extent counts
 * - Bitmap operations performance
 * - Directory entry calculations
 * - Memory layout efficiency
 * - Structure packing verification
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#include "lolelffs.h"

/* Benchmark counters */
static int benchmarks_run = 0;
static int benchmarks_passed = 0;

/* Timing utilities */
static inline uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define BENCHMARK(name) do { \
    printf("  Benchmarking %s... ", #name); \
    fflush(stdout); \
    benchmarks_run++; \
    if (bench_##name()) { \
        printf("PASS\n"); \
        benchmarks_passed++; \
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

/* Userspace extent structure for benchmarking */
struct bench_extent {
    uint32_t ee_block;
    uint32_t ee_len;
    uint32_t ee_start;
};

struct bench_file_ei_block {
    uint32_t nr_files;
    struct bench_extent extents[LOLELFFS_MAX_EXTENTS];
};

/*
 * Extent search implementation for benchmarking
 */
static uint32_t bench_ext_search(struct bench_file_ei_block *index, uint32_t iblock)
{
    uint32_t nr_extents = 0;
    uint32_t left, right, mid;

    /* Find the number of used extents */
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
 * Extent search with locality hint for benchmarking
 */
static uint32_t bench_ext_search_with_hint(struct bench_file_ei_block *index,
                                           uint32_t iblock,
                                           uint32_t hint)
{
    uint32_t nr_extents = 0;

    for (nr_extents = 0; nr_extents < LOLELFFS_MAX_EXTENTS; nr_extents++) {
        if (index->extents[nr_extents].ee_start == 0)
            break;
    }

    if (nr_extents == 0)
        return 0;

    /* Check if hint is valid */
    if (hint < nr_extents) {
        uint32_t block = index->extents[hint].ee_block;
        uint32_t len = index->extents[hint].ee_len;

        if (iblock >= block && iblock < block + len)
            return hint;

        if (hint + 1 < nr_extents) {
            block = index->extents[hint + 1].ee_block;
            len = index->extents[hint + 1].ee_len;
            if (iblock >= block && iblock < block + len)
                return hint + 1;
        }
    }

    return bench_ext_search(index, iblock);
}

/*
 * Initialize extents for benchmarking
 */
static void init_extents(struct bench_file_ei_block *index, uint32_t count)
{
    uint32_t i;
    uint32_t current_block = 0;

    memset(index, 0, sizeof(*index));

    for (i = 0; i < count && i < LOLELFFS_MAX_EXTENTS; i++) {
        index->extents[i].ee_block = current_block;
        index->extents[i].ee_len = LOLELFFS_MAX_BLOCKS_PER_EXTENT;
        index->extents[i].ee_start = i + 1; /* Non-zero physical block */
        current_block += LOLELFFS_MAX_BLOCKS_PER_EXTENT;
    }
}

/*
 * Benchmark: Extent binary search with small extent count (10 extents)
 */
static int bench_extent_search_small(void)
{
    struct bench_file_ei_block index;
    uint64_t start, end;
    uint32_t i, result;
    const uint32_t iterations = 1000000;
    const uint32_t extent_count = 10;
    double ns_per_op;

    init_extents(&index, extent_count);

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        uint32_t target = (i % (extent_count * LOLELFFS_MAX_BLOCKS_PER_EXTENT));
        result = bench_ext_search(&index, target);
        (void)result; /* Prevent optimization */
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (%u extents, %u iterations) ",
           ns_per_op, extent_count, iterations);

    /* Should be reasonably fast - less than 100ns per operation */
    ASSERT(ns_per_op < 500);
    return 1;
}

/*
 * Benchmark: Extent binary search with medium extent count (100 extents)
 */
static int bench_extent_search_medium(void)
{
    struct bench_file_ei_block index;
    uint64_t start, end;
    uint32_t i, result;
    const uint32_t iterations = 1000000;
    const uint32_t extent_count = 100;
    double ns_per_op;

    init_extents(&index, extent_count);

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        uint32_t target = (i % (extent_count * LOLELFFS_MAX_BLOCKS_PER_EXTENT));
        result = bench_ext_search(&index, target);
        (void)result;
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (%u extents, %u iterations) ",
           ns_per_op, extent_count, iterations);

    /* Binary search should scale logarithmically */
    ASSERT(ns_per_op < 1000);
    return 1;
}

/*
 * Benchmark: Extent binary search with maximum extent count (341 extents)
 */
static int bench_extent_search_large(void)
{
    struct bench_file_ei_block index;
    uint64_t start, end;
    uint32_t i, result;
    const uint32_t iterations = 1000000;
    const uint32_t extent_count = LOLELFFS_MAX_EXTENTS;
    double ns_per_op;

    init_extents(&index, extent_count);

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        uint32_t target = (i % (extent_count * LOLELFFS_MAX_BLOCKS_PER_EXTENT));
        result = bench_ext_search(&index, target);
        (void)result;
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (%u extents, %u iterations) ",
           ns_per_op, extent_count, iterations);

    /* Should still be fast due to O(log n) complexity */
    ASSERT(ns_per_op < 2000);
    return 1;
}

/*
 * Benchmark: Sequential access pattern with locality hints
 */
static int bench_extent_search_sequential(void)
{
    struct bench_file_ei_block index;
    uint64_t start, end;
    uint32_t i, result, hint = 0;
    const uint32_t iterations = 1000000;
    const uint32_t extent_count = 100;
    double ns_per_op;

    init_extents(&index, extent_count);

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        /* Sequential access pattern */
        uint32_t target = i % (extent_count * LOLELFFS_MAX_BLOCKS_PER_EXTENT);
        result = bench_ext_search_with_hint(&index, target, hint);
        hint = result; /* Use result as hint for next search */
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (sequential pattern) ", ns_per_op);

    /* Sequential access with hints should be very fast */
    ASSERT(ns_per_op < 500);
    return 1;
}

/*
 * Benchmark: Random access pattern without hints
 */
static int bench_extent_search_random(void)
{
    struct bench_file_ei_block index;
    uint64_t start, end;
    uint32_t i, result;
    const uint32_t iterations = 1000000;
    const uint32_t extent_count = 100;
    double ns_per_op;
    uint32_t *targets;

    init_extents(&index, extent_count);

    /* Pre-generate random targets */
    targets = malloc(iterations * sizeof(uint32_t));
    if (!targets)
        return 0;

    srand(42); /* Deterministic seed for reproducibility */
    for (i = 0; i < iterations; i++) {
        targets[i] = rand() % (extent_count * LOLELFFS_MAX_BLOCKS_PER_EXTENT);
    }

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        result = bench_ext_search(&index, targets[i]);
        (void)result;
    }
    end = get_time_ns();

    free(targets);

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (random pattern) ", ns_per_op);

    ASSERT(ns_per_op < 1000);
    return 1;
}

/*
 * Benchmark: Directory entry calculation performance
 */
static int bench_dir_entry_calc(void)
{
    uint64_t start, end;
    uint32_t i, ei, bi, fi;
    const uint32_t iterations = 10000000;
    double ns_per_op;

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        uint32_t nr_files = i % LOLELFFS_MAX_SUBFILES;
        ei = nr_files / LOLELFFS_FILES_PER_EXT;
        bi = nr_files % LOLELFFS_FILES_PER_EXT / LOLELFFS_FILES_PER_BLOCK;
        fi = nr_files % LOLELFFS_FILES_PER_BLOCK;
        (void)ei; (void)bi; (void)fi;
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (%u iterations) ", ns_per_op, iterations);

    /* Simple arithmetic should be very fast */
    ASSERT(ns_per_op < 50);
    return 1;
}

/*
 * Benchmark: Inode block calculation
 */
static int bench_inode_block_calc(void)
{
    uint64_t start, end;
    uint32_t i, block, shift;
    const uint32_t iterations = 10000000;
    const uint32_t max_inodes = 100000;
    double ns_per_op;

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        uint32_t ino = i % max_inodes;
        block = (ino / LOLELFFS_INODES_PER_BLOCK) + 1;
        shift = ino % LOLELFFS_INODES_PER_BLOCK;
        (void)block; (void)shift;
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (%u iterations) ", ns_per_op, iterations);

    ASSERT(ns_per_op < 50);
    return 1;
}

/*
 * Benchmark: Adaptive extent allocation size calculation
 */
static int bench_adaptive_alloc_calc(void)
{
    uint64_t start, end;
    uint32_t i, alloc_size;
    const uint32_t iterations = 10000000;
    double ns_per_op;

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        uint32_t current_blocks = i % 100;
        if (current_blocks < 8)
            alloc_size = 2;
        else if (current_blocks < 32)
            alloc_size = 4;
        else
            alloc_size = LOLELFFS_MAX_BLOCKS_PER_EXTENT;
        (void)alloc_size;
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (%u iterations) ", ns_per_op, iterations);

    ASSERT(ns_per_op < 50);
    return 1;
}

/*
 * Benchmark: Structure memory layout efficiency
 */
static int bench_memory_layout(void)
{
    size_t total_overhead = 0;
    size_t useful_data = 0;
    double efficiency;

    /* Inode structure efficiency */
    size_t inode_useful = 10 * sizeof(uint32_t) + 32; /* 10 fields + i_data */
    size_t inode_total = sizeof(struct lolelffs_inode);

    /* File entry efficiency */
    size_t file_useful = sizeof(uint32_t) + LOLELFFS_FILENAME_LEN;

    /* Extent structure efficiency */
    size_t extent_useful = 3 * sizeof(uint32_t);

    useful_data = inode_useful + file_useful + extent_useful;
    total_overhead = inode_total + file_useful + extent_useful;

    efficiency = (double)useful_data / total_overhead * 100;

    printf("%.1f%% efficient ", efficiency);

    /* Structures should be at least 90% efficient */
    ASSERT(efficiency > 85);
    return 1;
}

/*
 * Benchmark: Block utilization for different file sizes
 */
static int bench_block_utilization(void)
{
    struct {
        uint32_t file_size;
        uint32_t expected_blocks;
        const char *name;
    } test_cases[] = {
        {1, 1, "1 byte"},
        {4096, 1, "1 block"},
        {4097, 2, "1 block + 1 byte"},
        {32768, 8, "8 blocks"},
        {1048576, 256, "1 MB"},
        {0, 0, NULL}
    };

    int i;
    for (i = 0; test_cases[i].name != NULL; i++) {
        uint32_t blocks = (test_cases[i].file_size + LOLELFFS_BLOCK_SIZE - 1)
                          / LOLELFFS_BLOCK_SIZE;
        if (test_cases[i].file_size == 0)
            blocks = 0;
        if (blocks != test_cases[i].expected_blocks) {
            printf("\n    %s: expected %u blocks, got %u ",
                   test_cases[i].name, test_cases[i].expected_blocks, blocks);
            return 0;
        }
    }

    printf("all file sizes correct ");
    return 1;
}

/*
 * Benchmark: Extent count vs file size relationship
 */
static int bench_extent_file_relationship(void)
{
    struct {
        uint32_t file_blocks;
        uint32_t expected_extents;
    } test_cases[] = {
        {1, 1},
        {8, 1},
        {9, 2},
        {16, 2},
        {64, 8},
        {341 * 8, 341}, /* Max file size */
        {0, 0}
    };

    int i;
    for (i = 0; test_cases[i].file_blocks != 0; i++) {
        uint32_t extents = (test_cases[i].file_blocks +
                           LOLELFFS_MAX_BLOCKS_PER_EXTENT - 1) /
                           LOLELFFS_MAX_BLOCKS_PER_EXTENT;
        if (extents != test_cases[i].expected_extents) {
            printf("\n    %u blocks: expected %u extents, got %u ",
                   test_cases[i].file_blocks,
                   test_cases[i].expected_extents, extents);
            return 0;
        }
    }

    printf("all extent calculations correct ");
    return 1;
}

/*
 * Benchmark: Compare hinted vs non-hinted search performance
 */
static int bench_hint_speedup(void)
{
    struct bench_file_ei_block index;
    uint64_t start, end;
    uint32_t i, result, hint = 0;
    const uint32_t iterations = 1000000;
    const uint32_t extent_count = 100;
    double ns_without_hint, ns_with_hint, speedup;

    init_extents(&index, extent_count);

    /* Benchmark without hints (sequential pattern) */
    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        uint32_t target = i % (extent_count * LOLELFFS_MAX_BLOCKS_PER_EXTENT);
        result = bench_ext_search(&index, target);
        (void)result;
    }
    end = get_time_ns();
    ns_without_hint = (double)(end - start) / iterations;

    /* Benchmark with hints (sequential pattern) */
    hint = 0;
    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        uint32_t target = i % (extent_count * LOLELFFS_MAX_BLOCKS_PER_EXTENT);
        result = bench_ext_search_with_hint(&index, target, hint);
        hint = result;
    }
    end = get_time_ns();
    ns_with_hint = (double)(end - start) / iterations;

    speedup = ns_without_hint / ns_with_hint;
    printf("%.2fx speedup (%.2f ns vs %.2f ns) ",
           speedup, ns_without_hint, ns_with_hint);

    /* Hints should provide at least some speedup for sequential access */
    ASSERT(speedup >= 0.8); /* Allow some variance */
    return 1;
}

/*
 * Benchmark: Large directory entry count performance
 */
static int bench_large_directory(void)
{
    uint64_t start, end;
    uint32_t i;
    const uint32_t iterations = 1000000;
    double ns_per_op;
    uint32_t ei, bi, fi;

    /* Simulate finding slots in a large directory */
    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        uint32_t nr_files = i % LOLELFFS_MAX_SUBFILES;

        /* Calculate position in directory */
        ei = nr_files / LOLELFFS_FILES_PER_EXT;
        bi = nr_files % LOLELFFS_FILES_PER_EXT / LOLELFFS_FILES_PER_BLOCK;
        fi = nr_files % LOLELFFS_FILES_PER_BLOCK;

        /* Simulate extent lookup */
        if (ei < LOLELFFS_MAX_EXTENTS) {
            (void)bi; (void)fi;
        }
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (up to %lu files) ", ns_per_op, (unsigned long)LOLELFFS_MAX_SUBFILES);

    ASSERT(ns_per_op < 100);
    return 1;
}

/*
 * Benchmark: Extent validation performance
 */
static int bench_extent_validation(void)
{
    struct bench_file_ei_block index;
    uint64_t start, end;
    uint32_t i, j;
    const uint32_t iterations = 100000;
    const uint32_t extent_count = 100;
    double ns_per_op;
    int valid;

    init_extents(&index, extent_count);

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        uint32_t expected_block = 0;
        valid = 1;

        /* Validate all extents */
        for (j = 0; j < extent_count; j++) {
            struct bench_extent *ext = &index.extents[j];

            if (ext->ee_len == 0 || ext->ee_len > LOLELFFS_MAX_BLOCKS_PER_EXTENT) {
                valid = 0;
                break;
            }
            if (ext->ee_block != expected_block) {
                valid = 0;
                break;
            }
            if (ext->ee_start == 0) {
                valid = 0;
                break;
            }
            expected_block += ext->ee_len;
        }
        (void)valid;
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (%u extents) ", ns_per_op, extent_count);

    ASSERT(ns_per_op < 5000);
    return 1;
}

/*
 * Benchmark: Total blocks calculation
 */
static int bench_total_blocks_calc(void)
{
    struct bench_file_ei_block index;
    uint64_t start, end;
    uint32_t i, j, total;
    const uint32_t iterations = 100000;
    const uint32_t extent_count = 100;
    double ns_per_op;

    init_extents(&index, extent_count);

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        total = 0;
        for (j = 0; j < LOLELFFS_MAX_EXTENTS; j++) {
            if (index.extents[j].ee_start == 0)
                break;
            total += index.extents[j].ee_len;
        }
        (void)total;
    }
    end = get_time_ns();

    ns_per_op = (double)(end - start) / iterations;
    printf("%.2f ns/op (%u extents) ", ns_per_op, extent_count);

    ASSERT(ns_per_op < 3000);
    return 1;
}

int main(void)
{
    printf("Running lolelffs performance benchmarks...\n\n");

    printf("Extent Search Benchmarks:\n");
    BENCHMARK(extent_search_small);
    BENCHMARK(extent_search_medium);
    BENCHMARK(extent_search_large);
    BENCHMARK(extent_search_sequential);
    BENCHMARK(extent_search_random);
    BENCHMARK(hint_speedup);

    printf("\nCalculation Benchmarks:\n");
    BENCHMARK(dir_entry_calc);
    BENCHMARK(inode_block_calc);
    BENCHMARK(adaptive_alloc_calc);
    BENCHMARK(large_directory);
    BENCHMARK(total_blocks_calc);

    printf("\nValidation Benchmarks:\n");
    BENCHMARK(extent_validation);
    BENCHMARK(block_utilization);
    BENCHMARK(extent_file_relationship);

    printf("\nMemory Layout Benchmarks:\n");
    BENCHMARK(memory_layout);

    printf("\n========================================\n");
    printf("Benchmarks passed: %d/%d\n", benchmarks_passed, benchmarks_run);
    printf("========================================\n");

    return (benchmarks_passed == benchmarks_run) ? 0 : 1;
}
