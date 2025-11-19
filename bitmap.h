#ifndef LOLELFFS_BITMAP_H
#define LOLELFFS_BITMAP_H

#include <linux/bitmap.h>
#include "lolelffs.h"

/*
 * Return the first bit we found and clear the the following `len` consecutive
 * free bit(s) (set to 1) in a given in-memory bitmap spanning over multiple
 * blocks. Return 0 if no enough free bit(s) were found (we assume that the
 * first bit is never free because of the superblock and the root inode, thus
 * allowing us to use 0 as an error value).
 */
static inline uint32_t get_first_free_bits(unsigned long *freemap,
                                           unsigned long size,
                                           uint32_t len)
{
    uint32_t bit, prev = 0, count = 0;
    for_each_set_bit (bit, freemap, size) {
        if (prev != bit - 1)
            count = 0;
        prev = bit;
        if (++count == len) {
            bitmap_clear(freemap, bit - len + 1, len);
            return bit - len + 1;
        }
    }
    return 0;
}

/*
 * Return an unused inode number and mark it used.
 * Return 0 if no free inode was found.
 */
static inline uint32_t get_free_inode(struct lolelffs_sb_info *sbi)
{
    uint32_t ret = get_first_free_bits(sbi->ifree_bitmap, sbi->nr_inodes, 1);
    if (ret)
        sbi->nr_free_inodes--;
    return ret;
}

/*
 * Return `len` unused block(s) number and mark it used.
 * Return 0 if no enough free block(s) were found.
 */
static inline uint32_t get_free_blocks(struct lolelffs_sb_info *sbi,
                                       uint32_t len)
{
    uint32_t ret = get_first_free_bits(sbi->bfree_bitmap, sbi->nr_blocks, len);
    if (ret)
        sbi->nr_free_blocks -= len;
    return ret;
}


/* Mark the `len` bit(s) from i-th bit in freemap as free (i.e. 1) */
static inline int put_free_bits(unsigned long *freemap,
                                unsigned long size,
                                uint32_t i,
                                uint32_t len)
{
    /* i is greater than freemap size */
    if (i + len - 1 > size)
        return -1;

    bitmap_set(freemap, i, len);

    return 0;
}

/* Mark an inode as unused */
static inline void put_inode(struct lolelffs_sb_info *sbi, uint32_t ino)
{
    if (put_free_bits(sbi->ifree_bitmap, sbi->nr_inodes, ino, 1))
        return;

    sbi->nr_free_inodes++;
}

/* Mark len block(s) as unused */
static inline void put_blocks(struct lolelffs_sb_info *sbi,
                              uint32_t bno,
                              uint32_t len)
{
    if (put_free_bits(sbi->bfree_bitmap, sbi->nr_blocks, bno, len))
        return;

    sbi->nr_free_blocks += len;
}

/*
 * Calculate optimal extent allocation size based on current file state.
 * Strategy:
 * - Small files (< 8 blocks): allocate 2 blocks to reduce waste
 * - Medium files (8-32 blocks): allocate 4 blocks
 * - Large files (> 32 blocks): allocate 8 blocks (max per extent)
 * Always ensures we don't exceed available free blocks.
 */
static inline uint32_t calc_optimal_extent_size(struct lolelffs_sb_info *sbi,
                                                uint32_t current_blocks)
{
    uint32_t alloc_size;

    if (current_blocks < 8)
        alloc_size = 2;
    else if (current_blocks < 32)
        alloc_size = 4;
    else
        alloc_size = LOLELFFS_MAX_BLOCKS_PER_EXTENT;

    /* Ensure we don't exceed available blocks */
    if (alloc_size > sbi->nr_free_blocks)
        alloc_size = sbi->nr_free_blocks;

    /* Minimum allocation is 1 block */
    if (alloc_size == 0)
        alloc_size = 1;

    return alloc_size;
}

#endif /* LOLELFFS_BITMAP_H */
