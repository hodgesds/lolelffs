#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "lolelffs.h"

/* Mount a lolelffs partition */
struct dentry *lolelffs_mount(struct file_system_type *fs_type,
                              int flags,
                              const char *dev_name,
                              void *data)
{
    struct dentry *dentry =
        mount_bdev(fs_type, flags, dev_name, data, lolelffs_fill_super);
    if (IS_ERR(dentry))
        pr_err("'%s' mount failure\n", dev_name);
    else
        pr_info("'%s' mount success\n", dev_name);

    return dentry;
}

/* Unmount a lolelffs partition */
void lolelffs_kill_sb(struct super_block *sb)
{
    kill_block_super(sb);

    pr_info("unmounted disk\n");
}

static struct file_system_type lolelffs_file_system_type = {
    .owner = THIS_MODULE,
    .name = "lolelffs",
    .mount = lolelffs_mount,
    .kill_sb = lolelffs_kill_sb,
    .fs_flags = FS_REQUIRES_DEV,
    .next = NULL,
};

static int __init lolelffs_init(void)
{
    int ret = lolelffs_init_inode_cache();
    if (ret) {
        pr_err("inode cache creation failed\n");
        goto end;
    }

    ret = register_filesystem(&lolelffs_file_system_type);
    if (ret) {
        pr_err("register_filesystem() failed\n");
        goto end;
    }

    pr_info("module loaded\n");
end:
    return ret;
}

static void __exit lolelffs_exit(void)
{
    int ret = unregister_filesystem(&lolelffs_file_system_type);
    if (ret)
        pr_err("unregister_filesystem() failed\n");

    lolelffs_destroy_inode_cache();

    pr_info("module unloaded\n");
}

module_init(lolelffs_init);
module_exit(lolelffs_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Daniel Hodges <hodges.daniel.scott@gmail.com>");
MODULE_DESCRIPTION("lol an elf file system");
