#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "lolelffs.h"
#include "compress.h"
#include "encrypt.h"

/* Wrapper for fill_super to match new kernel API */
static int lolelffs_fill_super_fc(struct super_block *sb, struct fs_context *fc)
{
    return lolelffs_fill_super(sb, fc->fs_private, fc->sb_flags & SB_SILENT);
}

/* Mount a lolelffs partition */
static int lolelffs_get_tree(struct fs_context *fc)
{
    int ret = get_tree_bdev(fc, lolelffs_fill_super_fc);
    if (ret)
        pr_err("mount failure\n");
    else
        pr_info("mount success\n");

    return ret;
}

static const struct fs_context_operations lolelffs_context_ops = {
    .get_tree = lolelffs_get_tree,
};

static int lolelffs_init_fs_context(struct fs_context *fc)
{
    fc->ops = &lolelffs_context_ops;
    return 0;
}

/* Unmount a lolelffs partition */
static void lolelffs_kill_sb(struct super_block *sb)
{
    kill_block_super(sb);

    pr_info("unmounted disk\n");
}

static struct file_system_type lolelffs_file_system_type = {
    .owner = THIS_MODULE,
    .name = "lolelffs",
    .init_fs_context = lolelffs_init_fs_context,
    .kill_sb = lolelffs_kill_sb,
    .fs_flags = FS_REQUIRES_DEV,
    .next = NULL,
};

static int __init lolelffs_init(void)
{
    int ret;

    ret = lolelffs_comp_init();
    if (ret) {
        pr_err("compression initialization failed\n");
        return ret;
    }

    ret = lolelffs_enc_init();
    if (ret) {
        pr_err("encryption initialization failed\n");
        goto cleanup_comp;
    }

    ret = lolelffs_init_inode_cache();
    if (ret) {
        pr_err("inode cache creation failed\n");
        goto cleanup_enc;
    }

    ret = register_filesystem(&lolelffs_file_system_type);
    if (ret) {
        pr_err("register_filesystem() failed\n");
        goto cleanup_cache;
    }

    pr_info("module loaded\n");
    return 0;

cleanup_cache:
    lolelffs_destroy_inode_cache();
cleanup_enc:
    lolelffs_enc_exit();
cleanup_comp:
    lolelffs_comp_exit();
    return ret;
}

static void __exit lolelffs_exit(void)
{
    int ret = unregister_filesystem(&lolelffs_file_system_type);
    if (ret)
        pr_err("unregister_filesystem() failed\n");

    lolelffs_destroy_inode_cache();
    lolelffs_enc_exit();
    lolelffs_comp_exit();

    pr_info("module unloaded\n");
}

module_init(lolelffs_init);
module_exit(lolelffs_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Daniel Hodges <hodges.daniel.scott@gmail.com>");
MODULE_DESCRIPTION("lol an elf file system");
