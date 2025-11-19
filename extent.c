#include <linux/fs.h>
#include <linux/kernel.h>

#include "lolelffs.h"

/*
 * Search the extent which contains the target block using binary search.
 * Return the extent index if found.
 * Return the first unused extent index if not found (for allocation).
 * Return -1 if all extents are used and none contain the block.
 */
uint32_t lolelffs_ext_search(struct lolelffs_file_ei_block *index,
                             uint32_t iblock)
{
    uint32_t nr_extents = 0;
    uint32_t left, right, mid;

    /* Find the number of used extents */
    for (nr_extents = 0; nr_extents < LOLELFFS_MAX_EXTENTS; nr_extents++) {
        if (index->extents[nr_extents].ee_start == 0)
            break;
    }

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
