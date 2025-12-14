#define pr_fmt(fmt) "lolelffs: " fmt

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mpage.h>

#include "bitmap.h"
#include "lolelffs.h"

/*
 * Map the buffer_head passed in argument with the iblock-th block of the file
 * represented by inode. If the requested block is not allocated and create is
 * true,  allocate a new block on disk and map it.
 */
static int lolelffs_file_get_block(struct inode *inode,
                                   sector_t iblock,
                                   struct buffer_head *bh_result,
                                   int create)
{
    struct super_block *sb = inode->i_sb;
    struct lolelffs_sb_info *sbi = LOLELFFS_SB(sb);
    struct lolelffs_inode_info *ci = LOLELFFS_INODE(inode);
    struct lolelffs_file_ei_block *index;
    struct buffer_head *bh_index;
    bool alloc = false;
    int ret = 0, bno;
    uint32_t extent;

    /* If block number exceeds filesize, fail */
    if (iblock >= LOLELFFS_MAX_BLOCKS_PER_EXTENT * LOLELFFS_MAX_EXTENTS)
        return -EFBIG;

    /* Read directory block from disk */
    bh_index = LOLELFFS_SB_BREAD(sb, ci->ei_block);
    if (!bh_index)
        return -EIO;
    index = (struct lolelffs_file_ei_block *) bh_index->b_data;

    extent = lolelffs_ext_search(index, iblock);
    if (extent == -1) {
        ret = -EFBIG;
        goto brelse_index;
    }

    /*
     * Check if iblock is already allocated. If not and create is true,
     * allocate it. Else, get the physical block number.
     */
    if (index->extents[extent].ee_start == 0) {
        uint32_t alloc_size;
        if (!create)
            return 0;
        /* Use adaptive allocation based on current file size */
        alloc_size = calc_optimal_extent_size(sbi, inode->i_blocks);
        bno = get_free_blocks(sbi, alloc_size);
        if (!bno) {
            ret = -ENOSPC;
            goto brelse_index;
        }
        index->extents[extent].ee_start = bno;
        index->extents[extent].ee_len = alloc_size;
        index->extents[extent].ee_block =
            extent ? index->extents[extent - 1].ee_block +
                         index->extents[extent - 1].ee_len
                   : 0;
        alloc = true;
    } else {
        bno = index->extents[extent].ee_start + iblock -
              index->extents[extent].ee_block;
    }

    /* Map the physical block to to the given buffer_head (adjust for ELF offset) */
    map_bh(bh_result, sb, bno + ((struct lolelffs_sb_info *)sb->s_fs_info)->fs_offset);

brelse_index:
    brelse(bh_index);

    return ret;
}

/*
 * Called by the page cache to read a folio from the physical disk and map it in
 * memory.
 */
static int lolelffs_read_folio(struct file *file, struct folio *folio)
{
    return mpage_read_folio(folio, lolelffs_file_get_block);
}

/*
 * Called by the page cache to write a dirty folio to the physical disk (when
 * sync is called or when memory is needed).
 */
static int lolelffs_writepages(struct address_space *mapping,
                               struct writeback_control *wbc)
{
    return mpage_writepages(mapping, wbc, lolelffs_file_get_block);
}

/*
 * Called by the VFS when a write() syscall occurs on file before writing the
 * data in the page cache. This functions checks if the write will be able to
 * complete and allocates the necessary blocks through block_write_begin().
 */
static int lolelffs_write_begin(const struct kiocb *iocb,
                                struct address_space *mapping,
                                loff_t pos,
                                unsigned int len,
                                struct folio **foliop,
                                void **fsdata)
{
    struct inode *inode = mapping->host;
    struct super_block *sb = inode->i_sb;
    struct lolelffs_sb_info *sbi = LOLELFFS_SB(sb);
    struct lolelffs_inode_info *ci = LOLELFFS_INODE(inode);
    struct lolelffs_file_ei_block *index;
    struct buffer_head *bh_index;
    int err;
    uint32_t nr_allocs = 0;
    uint32_t nr_extents_before = 0;
    uint32_t i;

    /* Check if the write can be completed (enough space?) */
    if (pos + len > LOLELFFS_MAX_FILESIZE)
        return -ENOSPC;
    nr_allocs = max(pos + len, inode->i_size) / LOLELFFS_BLOCK_SIZE;
    if (nr_allocs > inode->i_blocks - 1)
        nr_allocs -= inode->i_blocks - 1;
    else
        nr_allocs = 0;
    if (nr_allocs > sbi->nr_free_blocks)
        return -ENOSPC;

    /* Count extents before write to track new allocations */
    bh_index = LOLELFFS_SB_BREAD(sb, ci->ei_block);
    if (!bh_index)
        return -EIO;
    index = (struct lolelffs_file_ei_block *) bh_index->b_data;
    for (i = 0; i < LOLELFFS_MAX_EXTENTS; i++) {
        if (index->extents[i].ee_start == 0)
            break;
        nr_extents_before++;
    }
    brelse(bh_index);

    /* prepare the write */
    err = block_write_begin(mapping, pos, len, foliop,
                            lolelffs_file_get_block);

    /* if this failed, reclaim newly allocated blocks */
    if (err < 0) {
        bh_index = LOLELFFS_SB_BREAD(sb, ci->ei_block);
        if (bh_index) {
            index = (struct lolelffs_file_ei_block *) bh_index->b_data;
            /* Free any extents allocated during the failed write */
            for (i = nr_extents_before; i < LOLELFFS_MAX_EXTENTS; i++) {
                if (index->extents[i].ee_start == 0)
                    break;
                put_blocks(sbi, index->extents[i].ee_start,
                           index->extents[i].ee_len);
                memset(&index->extents[i], 0, sizeof(struct lolelffs_extent));
            }
            mark_buffer_dirty(bh_index);
            brelse(bh_index);
        }
    }
    return err;
}

/*
 * Called by the VFS after writing data from a write() syscall to the page
 * cache. This functions updates inode metadata and truncates the file if
 * necessary.
 */
static int lolelffs_write_end(const struct kiocb *iocb,
                              struct address_space *mapping,
                              loff_t pos,
                              unsigned int len,
                              unsigned int copied,
                              struct folio *folio,
                              void *fsdata)
{
    struct inode *inode = mapping->host;
    struct lolelffs_inode_info *ci = LOLELFFS_INODE(inode);
    struct super_block *sb = inode->i_sb;
    uint32_t nr_blocks_old;

    /* Complete the write() */
    int ret = generic_write_end(iocb, mapping, pos, len, copied, folio, fsdata);
    if (ret < len) {
        pr_err("wrote less than requested.");
        return ret;
    }

    nr_blocks_old = inode->i_blocks;

    /* Update inode metadata */
    inode->i_blocks = inode->i_size / LOLELFFS_BLOCK_SIZE + 2;
    {
        struct timespec64 now = current_time(inode);
        inode_set_mtime_to_ts(inode, now);
        inode_set_ctime_to_ts(inode, now);
    }
    mark_inode_dirty(inode);

    /* If file is smaller than before, free unused blocks */
    if (nr_blocks_old > inode->i_blocks) {
        int i;
        struct buffer_head *bh_index;
        struct lolelffs_file_ei_block *index;
        uint32_t first_ext;

        /* Free unused blocks from page cache */
        truncate_pagecache(inode, inode->i_size);

        /* Read ei_block to remove unused blocks */
        bh_index = LOLELFFS_SB_BREAD(sb, ci->ei_block);
        if (!bh_index) {
            pr_err("failed truncating file. we just lost %llu blocks\n",
                   nr_blocks_old - inode->i_blocks);
            goto end;
        }
        index = (struct lolelffs_file_ei_block *) bh_index->b_data;

        first_ext = lolelffs_ext_search(index, inode->i_blocks - 1);
        /* Reserve unused block in last extent */
        if (inode->i_blocks - 1 != index->extents[first_ext].ee_block)
            first_ext++;

        for (i = first_ext; i < LOLELFFS_MAX_EXTENTS; i++) {
            if (!index->extents[i].ee_start)
                break;
            put_blocks(LOLELFFS_SB(sb), index->extents[i].ee_start,
                       index->extents[i].ee_len);
            memset(&index->extents[i], 0, sizeof(struct lolelffs_extent));
        }
        mark_buffer_dirty(bh_index);
        brelse(bh_index);
    }
end:
    return ret;
}

const struct address_space_operations lolelffs_aops = {
    .read_folio = lolelffs_read_folio,
    .writepages = lolelffs_writepages,
    .write_begin = lolelffs_write_begin,
    .write_end = lolelffs_write_end,
};

const struct file_operations lolelffs_file_ops = {
    .llseek = generic_file_llseek,
    .owner = THIS_MODULE,
    .read_iter = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
    .fsync = generic_file_fsync,
};
