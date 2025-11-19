#include <linux/fs.h>
#include <linux/kernel.h>

#include "lolelffs.h"

/*
 * Count the number of used extents in an index block.
 * This is useful for caching and validation.
 */
static inline uint32_t lolelffs_count_extents(struct lolelffs_file_ei_block *index)
{
    uint32_t nr_extents = 0;

    for (nr_extents = 0; nr_extents < LOLELFFS_MAX_EXTENTS; nr_extents++) {
        if (index->extents[nr_extents].ee_start == 0)
            break;
    }

    return nr_extents;
}

/*
 * Validate extent ordering and consistency.
 * Returns 0 if valid, negative error code otherwise.
 *
 * Checks:
 * - Extents are contiguous in logical space
 * - Extent lengths are within bounds
 * - No overlapping extents
 */
int lolelffs_validate_extents(struct lolelffs_file_ei_block *index)
{
    uint32_t i, nr_extents;
    uint32_t expected_block = 0;

    nr_extents = lolelffs_count_extents(index);

    for (i = 0; i < nr_extents; i++) {
        struct lolelffs_extent *ext = &index->extents[i];

        /* Check length is valid */
        if (ext->ee_len == 0 || ext->ee_len > LOLELFFS_MAX_BLOCKS_PER_EXTENT)
            return -EINVAL;

        /* Check logical blocks are contiguous */
        if (ext->ee_block != expected_block)
            return -EINVAL;

        /* Check physical block is non-zero */
        if (ext->ee_start == 0)
            return -EINVAL;

        expected_block += ext->ee_len;
    }

    return 0;
}

/*
 * Calculate total blocks used by all extents.
 */
uint32_t lolelffs_extents_total_blocks(struct lolelffs_file_ei_block *index)
{
    uint32_t i, total = 0;

    for (i = 0; i < LOLELFFS_MAX_EXTENTS; i++) {
        if (index->extents[i].ee_start == 0)
            break;
        total += index->extents[i].ee_len;
    }

    return total;
}

/*
 * Search the extent which contains the target block using binary search.
 * Return the extent index if found.
 * Return the first unused extent index if not found (for allocation).
 * Return -1 if all extents are used and none contain the block.
 *
 * Optimized version with locality hint support.
 */
uint32_t lolelffs_ext_search(struct lolelffs_file_ei_block *index,
                             uint32_t iblock)
{
    uint32_t nr_extents = 0;
    uint32_t left, right, mid;

    /* Find the number of used extents */
    nr_extents = lolelffs_count_extents(index);

    /* If no extents are allocated, return 0 (first slot for allocation) */
    if (nr_extents == 0)
        return 0;

    /* Binary search among used extents */
    left = 0;
    right = nr_extents;

    while (left < right) {
        uint32_t block, len;

        mid = left + (right - left) / 2;
        block = index->extents[mid].ee_block;
        len = index->extents[mid].ee_len;

        if (iblock < block) {
            /* Target is before this extent */
            right = mid;
        } else if (iblock >= block + len) {
            /* Target is after this extent */
            left = mid + 1;
        } else {
            /* Found: iblock is within this extent */
            return mid;
        }
    }

    /* Not found in any extent, return first unused slot for allocation */
    if (nr_extents < LOLELFFS_MAX_EXTENTS)
        return nr_extents;

    /* All extents are used and none contain the block */
    return -1;
}

/*
 * Search with locality hint - check the hinted extent first before
 * falling back to binary search. This is useful for sequential access
 * patterns where the next block is likely in the same or adjacent extent.
 */
uint32_t lolelffs_ext_search_with_hint(struct lolelffs_file_ei_block *index,
                                       uint32_t iblock,
                                       uint32_t hint)
{
    uint32_t nr_extents;

    nr_extents = lolelffs_count_extents(index);

    if (nr_extents == 0)
        return 0;

    /* Check if hint is valid */
    if (hint < nr_extents) {
        uint32_t block = index->extents[hint].ee_block;
        uint32_t len = index->extents[hint].ee_len;

        /* Check if iblock is in the hinted extent */
        if (iblock >= block && iblock < block + len)
            return hint;

        /* Check next extent (common for sequential access) */
        if (hint + 1 < nr_extents) {
            block = index->extents[hint + 1].ee_block;
            len = index->extents[hint + 1].ee_len;
            if (iblock >= block && iblock < block + len)
                return hint + 1;
        }
    }

    /* Fall back to binary search */
    return lolelffs_ext_search(index, iblock);
}
