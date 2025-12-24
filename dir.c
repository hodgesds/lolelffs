#define pr_fmt(fmt) "lolelffs: " fmt

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "lolelffs.h"
#include "encrypt.h"

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
    bh = LOLELFFS_SB_BREAD(sb, ci->ei_block);
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
            bh2 = LOLELFFS_SB_BREAD(sb, eblock->extents[ei].ee_start + bi);
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

/**
 * lolelffs_ioctl - Handle ioctl commands for lolelffs
 * @file: File pointer
 * @cmd: ioctl command
 * @arg: ioctl argument
 *
 * Handles filesystem-level ioctl commands, including unlocking encrypted
 * filesystems and querying encryption status.
 */
static long lolelffs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file_inode(file);
    struct super_block *sb = inode->i_sb;
    struct lolelffs_sb_info *sbi = LOLELFFS_SB(sb);
    struct buffer_head *bh;
    struct lolelffs_sb_info *csb;
    int ret;

    /* Read superblock to get encryption info */
    bh = LOLELFFS_SB_BREAD(sb, 0);
    if (!bh)
        return -EIO;
    csb = (struct lolelffs_sb_info *)bh->b_data;

    switch (cmd) {
    case LOLELFFS_IOC_UNLOCK: {
        struct lolelffs_ioctl_unlock req;
        u8 user_key[32];
        u8 master_key[32];

        /* Check if encryption is enabled */
        if (csb->enc_enabled == 0) {
            pr_info("filesystem is not encrypted\n");
            ret = -EINVAL;
            goto out;
        }

        /* Check if already unlocked */
        mutex_lock(&sbi->enc_lock);
        if (sbi->enc_unlocked) {
            pr_info("filesystem is already unlocked\n");
            mutex_unlock(&sbi->enc_lock);
            ret = 0;
            goto out;
        }
        mutex_unlock(&sbi->enc_lock);

        /* Copy password from userspace */
        if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
            ret = -EFAULT;
            goto out;
        }

        /* Ensure null termination */
        req.password[sizeof(req.password) - 1] = '\0';

        /* Derive user key from password */
        ret = lolelffs_derive_key(
            csb->enc_kdf_algo,
            req.password,
            req.password_len,
            csb->enc_salt,
            csb->enc_kdf_iterations,
            csb->enc_kdf_memory,
            csb->enc_kdf_parallelism,
            user_key);

        if (ret < 0) {
            pr_err("failed to derive key from password: %d\n", ret);
            goto out_zero;
        }

        /* Decrypt master key */
        ret = lolelffs_decrypt_master_key(
            csb->enc_master_key,
            user_key,
            master_key);

        if (ret < 0) {
            pr_err("failed to decrypt master key: %d\n", ret);
            goto out_zero;
        }

        /* Store decrypted master key and mark as unlocked */
        mutex_lock(&sbi->enc_lock);
        memcpy(sbi->enc_master_key_decrypted, master_key, 32);
        sbi->enc_unlocked = true;
        mutex_unlock(&sbi->enc_lock);

        pr_info("filesystem unlocked successfully\n");
        ret = 0;

out_zero:
        /* Zero sensitive data */
        memzero_explicit(user_key, sizeof(user_key));
        memzero_explicit(master_key, sizeof(master_key));
        memzero_explicit(&req, sizeof(req));
        break;
    }

    case LOLELFFS_IOC_ENC_STATUS: {
        struct lolelffs_ioctl_enc_status status;

        status.enc_enabled = csb->enc_enabled;
        status.enc_algorithm = csb->enc_default_algo;

        mutex_lock(&sbi->enc_lock);
        status.enc_unlocked = sbi->enc_unlocked ? 1 : 0;
        mutex_unlock(&sbi->enc_lock);

        if (copy_to_user((void __user *)arg, &status, sizeof(status))) {
            ret = -EFAULT;
            goto out;
        }

        ret = 0;
        break;
    }

    default:
        ret = -ENOTTY;
        break;
    }

out:
    brelse(bh);
    return ret;
}

const struct file_operations lolelffs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = lolelffs_iterate,
    .unlocked_ioctl = lolelffs_ioctl,
};
