#define pr_fmt(fmt) "lolelffs: " fmt

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mpage.h>
#include <linux/vmalloc.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>

#include "bitmap.h"
#include "lolelffs.h"
#include "compress.h"
#include "encrypt.h"

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

    /* If we allocated a new extent, mark the index as dirty and sync it */
    if (alloc) {
        mark_buffer_dirty(bh_index);
        sync_dirty_buffer(bh_index);
    }

brelse_index:
    brelse(bh_index);

    return ret;
}

/*
 * Called by the page cache to read a folio from the physical disk and map it in
 * memory. Handles transparent decompression if the block is compressed.
 */
static int lolelffs_read_folio(struct file *file, struct folio *folio)
{
    struct inode *inode = folio->mapping->host;
    struct super_block *sb = inode->i_sb;
    struct lolelffs_sb_info *sbi = LOLELFFS_SB(sb);
    struct lolelffs_inode_info *ci = LOLELFFS_INODE(inode);
    struct lolelffs_file_ei_block *index;
    struct buffer_head *bh_index, *bh_block;
    struct page *page = &folio->page;
    void *page_data;
    sector_t iblock;
    uint32_t extent_idx, phys_block;
    u8 comp_algo, enc_algo;
    void *decrypt_buf = NULL;
    int ret = 0;

    /* Calculate which block to read */
    iblock = folio_pos(folio) >> 12; /* LOLELFFS_BLOCK_SIZE = 4096 = 1 << 12 */

    /* If beyond file size, zero-fill and return */
    if (folio_pos(folio) >= inode->i_size) {
        folio_zero_range(folio, 0, folio_size(folio));
        folio_mark_uptodate(folio);
        folio_unlock(folio);
        return 0;
    }

    /* Read extent index block */
    bh_index = LOLELFFS_SB_BREAD(sb, ci->ei_block);
    if (!bh_index) {
        ret = -EIO;
        goto error;
    }
    index = (struct lolelffs_file_ei_block *) bh_index->b_data;

    /* Find the extent containing this block */
    extent_idx = lolelffs_ext_search(index, iblock);
    if (extent_idx == (uint32_t)-1 || index->extents[extent_idx].ee_start == 0) {
        /* Block not allocated - zero-fill */
        brelse(bh_index);
        folio_zero_range(folio, 0, folio_size(folio));
        folio_mark_uptodate(folio);
        folio_unlock(folio);
        return 0;
    }

    /* Calculate physical block number */
    phys_block = index->extents[extent_idx].ee_start +
                 (iblock - index->extents[extent_idx].ee_block);
    comp_algo = index->extents[extent_idx].ee_comp_algo;
    enc_algo = index->extents[extent_idx].ee_enc_algo;

    brelse(bh_index);

    /* Read the physical block (adjust for ELF offset) */
    bh_block = LOLELFFS_SB_BREAD(sb, phys_block + sbi->fs_offset);
    if (!bh_block) {
        ret = -EIO;
        goto error;
    }

    page_data = kmap_local_page(page);

    /* Handle decrypt-then-decompress pipeline */
    void *source_buf = bh_block->b_data;

    /* Step 1: Decrypt if needed */
    if (enc_algo != LOLELFFS_ENC_NONE && lolelffs_enc_supported(enc_algo)) {
        /* Check if filesystem is unlocked */
        if (!sbi->enc_unlocked) {
            pr_err("cannot read encrypted block: filesystem is locked\n");
            ret = -EPERM;
            kunmap_local(page_data);
            brelse(bh_block);
            goto error;
        }

        /* Allocate temporary buffer for decrypted data */
        decrypt_buf = kmalloc(LOLELFFS_BLOCK_SIZE, GFP_NOFS);
        if (!decrypt_buf) {
            ret = -ENOMEM;
            kunmap_local(page_data);
            brelse(bh_block);
            goto error;
        }

        /* Decrypt the block */
        ret = lolelffs_decrypt_block(enc_algo, sbi->enc_master_key_decrypted,
                                     iblock, source_buf, decrypt_buf);
        if (ret < 0) {
            pr_err("decryption failed for inode %lu block %llu: %d\n",
                   inode->i_ino, (u64)iblock, ret);
            kfree(decrypt_buf);
            kunmap_local(page_data);
            brelse(bh_block);
            goto error;
        }

        /* Use decrypted data as source for next step */
        source_buf = decrypt_buf;
    }

    /* Step 2: Decompress if needed */
    if (comp_algo != LOLELFFS_COMP_NONE && lolelffs_comp_supported(comp_algo)) {
        ret = lolelffs_decompress_block(comp_algo, source_buf, LOLELFFS_BLOCK_SIZE,
                                        page_data, LOLELFFS_BLOCK_SIZE);
        if (ret < 0) {
            pr_err("decompression failed for inode %lu block %llu: %d\n",
                   inode->i_ino, (u64)iblock, ret);
            if (decrypt_buf)
                kfree(decrypt_buf);
            kunmap_local(page_data);
            brelse(bh_block);
            goto error;
        }
    } else {
        /* No compression - direct copy */
        memcpy(page_data, source_buf, LOLELFFS_BLOCK_SIZE);
    }

    /* Clean up */
    if (decrypt_buf)
        kfree(decrypt_buf);
    kunmap_local(page_data);
    brelse(bh_block);

    /* Mark page as uptodate and unlock */
    folio_mark_uptodate(folio);
    folio_unlock(folio);
    return 0;

error:
    folio_unlock(folio);
    return ret;
}

/*
 * Called by the page cache to write a dirty folio to the physical disk (when
 * sync is called or when memory is needed).
 *
 * TODO: Implement encryption and compression during write. For now, data is
 * written uncompressed and unencrypted. Compressed/encrypted blocks can be
 * created using the Rust tools and read transparently via the read path.
 *
 * Write-side encryption requires:
 * 1. Kernel 6.2+ folio/writeback APIs
 * 2. Custom writepage implementation with compress-then-encrypt pipeline
 * 3. Proper extent metadata updates
 */
/* Helper function to write a single page/folio with compression and encryption */
static int lolelffs_writepage_locked(struct folio *folio, struct writeback_control *wbc)
{
    struct inode *inode = folio->mapping->host;
    struct super_block *sb = inode->i_sb;
    struct lolelffs_sb_info *sbi = LOLELFFS_SB(sb);
    struct lolelffs_inode_info *ci = LOLELFFS_INODE(inode);
    struct lolelffs_file_ei_block *index;
    struct buffer_head *bh_index = NULL, *bh_block = NULL;
    struct page *page = &folio->page;
    void *page_data = NULL;
    void *work_buf = NULL;
    void *final_buf = NULL;
    sector_t iblock;
    uint32_t extent_idx, phys_block;
    u8 comp_algo, enc_algo;
    u8 used_comp_algo = LOLELFFS_COMP_NONE;
    u8 used_enc_algo = LOLELFFS_ENC_NONE;
    u16 flags = 0;
    int ret = 0;
    size_t comp_size = 0;

    /* Calculate which block to write */
    iblock = folio->index;

    /* Get extent index */
    bh_index = LOLELFFS_SB_BREAD(sb, ci->ei_block);
    if (!bh_index) {
        ret = -EIO;
        goto error;
    }
    index = (struct lolelffs_file_ei_block *) bh_index->b_data;

    /* Find extent for this block */
    extent_idx = lolelffs_ext_search(index, iblock);
    if (extent_idx == (uint32_t)-1 || index->extents[extent_idx].ee_start == 0) {
        /* Block not allocated - this shouldn't happen for dirty pages */
        pr_err("lolelffs: trying to write unallocated block %lu\n", (unsigned long)iblock);
        ret = -EIO;
        goto error;
    }

    /* Calculate physical block number */
    phys_block = index->extents[extent_idx].ee_start +
                 (iblock - index->extents[extent_idx].ee_block);

    /* Get compression and encryption settings */
    comp_algo = sbi->comp_enabled ? sbi->comp_default_algo : LOLELFFS_COMP_NONE;
    enc_algo = sbi->enc_enabled ? sbi->enc_default_algo : LOLELFFS_ENC_NONE;

    /* Map the page to get data */
    page_data = kmap(page);
    if (!page_data) {
        ret = -ENOMEM;
        goto error;
    }

    /* Allocate working buffer */
    work_buf = kmalloc(LOLELFFS_BLOCK_SIZE, GFP_NOFS);
    if (!work_buf) {
        ret = -ENOMEM;
        goto error;
    }

    /* Copy page data to working buffer */
    memcpy(work_buf, page_data, LOLELFFS_BLOCK_SIZE);
    final_buf = work_buf;

    /* Step 1: Compress if enabled */
    if (comp_algo != LOLELFFS_COMP_NONE && lolelffs_comp_supported(comp_algo)) {
        void *comp_buf = kmalloc(LOLELFFS_BLOCK_SIZE, GFP_NOFS);
        if (comp_buf) {
            ret = lolelffs_compress_block(comp_algo, work_buf, LOLELFFS_BLOCK_SIZE,
                                         comp_buf, &comp_size);
            if (ret == 0 && comp_size < LOLELFFS_BLOCK_SIZE * 95 / 100) {
                /* Compression saved at least 5% - use it */
                memcpy(work_buf, comp_buf, comp_size);
                memset(work_buf + comp_size, 0, LOLELFFS_BLOCK_SIZE - comp_size);
                used_comp_algo = comp_algo;
                flags |= LOLELFFS_EXT_COMPRESSED;
            }
            kfree(comp_buf);
        }
    }

    /* Step 2: Encrypt if enabled (compress-then-encrypt) */
    if (enc_algo != LOLELFFS_ENC_NONE && lolelffs_enc_supported(enc_algo)) {
        void *enc_buf = kmalloc(LOLELFFS_BLOCK_SIZE, GFP_NOFS);
        if (!enc_buf) {
            ret = -ENOMEM;
            goto error;
        }

        /* Check if filesystem is unlocked */
        if (!sbi->enc_unlocked) {
            pr_err("lolelffs: cannot write encrypted block: filesystem is locked\n");
            kfree(enc_buf);
            ret = -EPERM;
            goto error;
        }

        ret = lolelffs_encrypt_block(enc_algo, sbi->enc_master_key_decrypted,
                                     iblock, work_buf, enc_buf);
        if (ret == 0) {
            memcpy(work_buf, enc_buf, LOLELFFS_BLOCK_SIZE);
            used_enc_algo = enc_algo;
            flags |= LOLELFFS_EXT_ENCRYPTED;
        }
        kfree(enc_buf);

        if (ret != 0) {
            pr_err("lolelffs: encryption failed: %d\n", ret);
            goto error;
        }
    }

    /* Read the physical block buffer */
    bh_block = LOLELFFS_SB_BREAD(sb, phys_block);
    if (!bh_block) {
        ret = -EIO;
        goto error;
    }

    /* Copy encrypted/compressed data to block buffer */
    memcpy(bh_block->b_data, work_buf, LOLELFFS_BLOCK_SIZE);
    mark_buffer_dirty(bh_block);
    sync_dirty_buffer(bh_block);

    /* Update extent metadata if compression or encryption was used */
    if (used_comp_algo != index->extents[extent_idx].ee_comp_algo ||
        used_enc_algo != index->extents[extent_idx].ee_enc_algo ||
        flags != index->extents[extent_idx].ee_flags) {

        index->extents[extent_idx].ee_comp_algo = used_comp_algo;
        index->extents[extent_idx].ee_enc_algo = used_enc_algo;
        index->extents[extent_idx].ee_flags = flags;
        mark_buffer_dirty(bh_index);
        sync_dirty_buffer(bh_index);
    }

    /* Mark page clean */
    folio_start_writeback(folio);
    folio_unlock(folio);
    folio_end_writeback(folio);
    ret = 0;

error:
    if (page_data)
        kunmap(page);
    if (work_buf)
        kfree(work_buf);
    if (bh_block)
        brelse(bh_block);
    if (bh_index)
        brelse(bh_index);

    if (ret != 0) {
        /* Mark page as having an error */
        mapping_set_error(folio->mapping, ret);
        folio_unlock(folio);
    }

    return ret;
}

static int lolelffs_writepages(struct address_space *mapping,
                               struct writeback_control *wbc)
{
    struct inode *inode = mapping->host;
    struct blk_plug plug;
    pgoff_t index = 0;
    pgoff_t end;
    int ret = 0;
    int done = 0;

    /* For now, fall back to mpage_writepages if encryption is not enabled */
    struct super_block *sb = inode->i_sb;
    struct lolelffs_sb_info *sbi = LOLELFFS_SB(sb);

    if (!sbi->enc_enabled && !sbi->comp_enabled) {
        return mpage_writepages(mapping, wbc, lolelffs_file_get_block);
    }

    /* Custom writepages for compression and encryption */
    blk_start_plug(&plug);

    if (wbc->range_cyclic) {
        index = mapping->writeback_index;
        end = -1;
    } else {
        index = wbc->range_start >> PAGE_SHIFT;
        end = wbc->range_end >> PAGE_SHIFT;
    }

    while (!done && (index <= end)) {
        struct folio_batch fbatch;
        unsigned int i, nr;

        folio_batch_init(&fbatch);
        nr = filemap_get_folios_tag(mapping, &index, end,
                                     PAGECACHE_TAG_DIRTY, &fbatch);
        if (nr == 0)
            break;

        for (i = 0; i < nr; i++) {
            struct folio *folio = fbatch.folios[i];

            folio_lock(folio);

            if (!folio_clear_dirty_for_io(folio)) {
                folio_unlock(folio);
                continue;
            }

            ret = lolelffs_writepage_locked(folio, wbc);
            if (ret) {
                done = 1;
                break;
            }

            if (wbc->nr_to_write <= 0) {
                done = 1;
                break;
            }
        }

        folio_batch_release(&fbatch);
        cond_resched();
    }

    blk_finish_plug(&plug);

    if (wbc->range_cyclic)
        mapping->writeback_index = index;

    return ret;
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
    lolelffs_write_inode(inode, NULL);

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
        sync_dirty_buffer(bh_index);
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
