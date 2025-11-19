#define pr_fmt(fmt) "lolelffs: " fmt

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "lolelffs.h"

/*
 * Iterate over the files contained in dir and commit them in ctx.
 * This function is called by the VFS while ctx->pos changes.
 * Return 0 on success.
 */
static int lolelffs_iterate(struct file *dir, struct dir_context *ctx)
{
    struct inode *inode = file_inode(dir);
    struct lolelffs_inode_info *ci = LOLELFFS_INODE(inode);
    struct super_block *sb = inode->i_sb;
    struct buffer_head *bh = NULL, *bh2 = NULL;
    struct lolelffs_file_ei_block *eblock = NULL;
    struct lolelffs_dir_block *dblock = NULL;
    struct lolelffs_file *f = NULL;
    int ei = 0, bi = 0, fi = 0;
    int ret = 0;

    /* Check that dir is a directory */
    if (!S_ISDIR(inode->i_mode))
        return -ENOTDIR;

    /*
     * Check that ctx->pos is not bigger than what we can handle (including
     * . and ..)
     */
    if (ctx->pos > LOLELFFS_MAX_SUBFILES + 2)
        return 0;

    /* Commit . and .. to ctx */
    if (!dir_emit_dots(dir, ctx))
        return 0;

    /* Read the directory index block on disk */
    bh = sb_bread(sb, ci->ei_block);
    if (!bh)
        return -EIO;
    eblock = (struct lolelffs_file_ei_block *) bh->b_data;

    ei = (ctx->pos - 2) / LOLELFFS_FILES_PER_EXT;
    bi = (ctx->pos - 2) % LOLELFFS_FILES_PER_EXT
         / LOLELFFS_FILES_PER_BLOCK;
    fi = (ctx->pos - 2) % LOLELFFS_FILES_PER_BLOCK;

    /* Iterate over the index block and commit subfiles */
    for (; ei < LOLELFFS_MAX_EXTENTS; ei++) {
        if (eblock->extents[ei].ee_start == 0) {
            break;
        }
        /* Iterate over blocks in one extent */
        for (; bi < eblock->extents[ei].ee_len; bi++) {
            bh2 = sb_bread(sb, eblock->extents[ei].ee_start + bi);
            if (!bh2) {
                ret = -EIO;
                goto release_bh;
            }
            dblock = (struct lolelffs_dir_block *) bh2->b_data;
            if (dblock->files[0].inode == 0) {
                break;
            }
            /* Iterate every file in one block */
            for (; fi < LOLELFFS_FILES_PER_BLOCK; fi++) {
                f = &dblock->files[fi];
                if (f->inode && !dir_emit(ctx, f->filename,
                               strnlen(f->filename, LOLELFFS_FILENAME_LEN),
                               f->inode, DT_UNKNOWN))
                    break;
                ctx->pos++;
            }
            brelse(bh2);
            bh2 = NULL;
        }
    }

release_bh:
    brelse(bh);

    return ret;
}

const struct file_operations lolelffs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = lolelffs_iterate,
};
