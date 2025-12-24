#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/xattr.h>
#include <linux/string.h>

#include "bitmap.h"
#include "lolelffs.h"

/* Namespace prefixes */
static const char *xattr_prefixes[] = {
    [LOLELFFS_XATTR_INDEX_USER]     = XATTR_USER_PREFIX,
    [LOLELFFS_XATTR_INDEX_TRUSTED]  = XATTR_TRUSTED_PREFIX,
    [LOLELFFS_XATTR_INDEX_SYSTEM]   = XATTR_SYSTEM_PREFIX,
    [LOLELFFS_XATTR_INDEX_SECURITY] = XATTR_SECURITY_PREFIX,
};

/* Read xattr data from extent blocks */
static int lolelffs_xattr_read_data(struct super_block *sb,
                                    struct lolelffs_xattr_ei_block *ei,
                                    char **data_out, size_t *size_out)
{
    char *data;
    size_t total_size = 0;
    int ei_idx, bi;

    if (ei->total_size == 0) {
        *data_out = NULL;
        *size_out = 0;
        return 0;
    }

    data = kmalloc(ei->total_size, GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    /* Read data from all extents */
    for (ei_idx = 0; ei_idx < LOLELFFS_MAX_EXTENTS; ei_idx++) {
        struct lolelffs_extent *extent = &ei->extents[ei_idx];

        if (extent->ee_start == 0)
            break;

        for (bi = 0; bi < extent->ee_len; bi++) {
            struct buffer_head *bh;
            size_t copy_size;

            bh = LOLELFFS_SB_BREAD(sb, extent->ee_start + bi);
            if (!bh) {
                kfree(data);
                return -EIO;
            }

            copy_size = min_t(size_t, LOLELFFS_BLOCK_SIZE,
                             ei->total_size - total_size);
            memcpy(data + total_size, bh->b_data, copy_size);
            total_size += copy_size;

            brelse(bh);

            if (total_size >= ei->total_size)
                goto done;
        }
    }

done:
    *data_out = data;
    *size_out = total_size;
    return 0;
}

/* Find an xattr entry by name */
static struct lolelffs_xattr_entry *
lolelffs_xattr_find_entry(char *data, size_t data_size,
                          int name_index, const char *name,
                          size_t *entry_offset_out)
{
    size_t offset = 0;
    size_t name_len = strlen(name);

    while (offset + sizeof(struct lolelffs_xattr_entry) <= data_size) {
        struct lolelffs_xattr_entry *entry;
        char *entry_name;

        entry = (struct lolelffs_xattr_entry *)(data + offset);

        /* Check if we've reached the end */
        if (entry->name_len == 0)
            break;

        /* Get entry name */
        entry_name = data + offset + sizeof(struct lolelffs_xattr_entry);

        /* Match namespace and name */
        if (entry->name_index == name_index &&
            entry->name_len == name_len &&
            memcmp(entry_name, name, name_len) == 0) {
            if (entry_offset_out)
                *entry_offset_out = offset;
            return entry;
        }

        /* Move to next entry */
        offset += sizeof(struct lolelffs_xattr_entry);
        offset += entry->name_len + 1; /* +1 for NUL */
        offset += entry->value_len;

        /* Align to 4-byte boundary */
        offset = (offset + 3) & ~3;
    }

    return NULL;
}

/* Get an extended attribute value */
static int lolelffs_xattr_get(struct inode *inode, int name_index,
                              const char *name, void *buffer, size_t size)
{
    struct super_block *sb = inode->i_sb;
    struct lolelffs_inode_info *ci = LOLELFFS_INODE(inode);
    struct buffer_head *bh = NULL;
    struct lolelffs_xattr_ei_block *ei;
    char *data = NULL;
    size_t data_size;
    struct lolelffs_xattr_entry *entry;
    char *value;
    int ret;

    /* No xattrs? */
    if (ci->xattr_block == 0)
        return -ENODATA;

    /* Read xattr extent index */
    bh = LOLELFFS_SB_BREAD(sb, ci->xattr_block);
    if (!bh)
        return -EIO;

    ei = (struct lolelffs_xattr_ei_block *)bh->b_data;

    /* Read xattr data */
    ret = lolelffs_xattr_read_data(sb, ei, &data, &data_size);
    brelse(bh);

    if (ret)
        return ret;

    if (!data || data_size == 0)
        return -ENODATA;

    /* Find the entry */
    entry = lolelffs_xattr_find_entry(data, data_size, name_index, name, NULL);
    if (!entry) {
        kfree(data);
        return -ENODATA;
    }

    /* Get value */
    value = (char *)entry + entry->value_offset;

    if (!buffer) {
        /* Just return the size */
        ret = entry->value_len;
    } else if (size < entry->value_len) {
        ret = -ERANGE;
    } else {
        memcpy(buffer, value, entry->value_len);
        ret = entry->value_len;
    }

    kfree(data);
    return ret;
}

/* Set an extended attribute value */
static int lolelffs_xattr_set(struct inode *inode, int name_index,
                              const char *name, const void *value,
                              size_t value_len, int flags)
{
    struct super_block *sb = inode->i_sb;
    struct lolelffs_sb_info *sbi = LOLELFFS_SB(sb);
    struct lolelffs_inode_info *ci = LOLELFFS_INODE(inode);
    struct buffer_head *bh = NULL, *bh2;
    struct lolelffs_xattr_ei_block *ei;
    char *data = NULL, *new_data = NULL;
    size_t data_size, new_data_size;
    struct lolelffs_xattr_entry *entry;
    size_t entry_offset, name_len;
    uint32_t xattr_block;
    int ret = 0, ei_idx, bi;

    name_len = strlen(name);

    /* Validate inputs */
    if (name_len == 0 || name_len > 255)
        return -EINVAL;

    if (value_len > 65535)
        return -ENOSPC;

    /* Handle deletion */
    if (!value) {
        if (ci->xattr_block == 0)
            return -ENODATA;

        /* Read current xattr data */
        bh = LOLELFFS_SB_BREAD(sb, ci->xattr_block);
        if (!bh)
            return -EIO;

        ei = (struct lolelffs_xattr_ei_block *)bh->b_data;
        ret = lolelffs_xattr_read_data(sb, ei, &data, &data_size);

        if (ret) {
            brelse(bh);
            return ret;
        }

        /* Find the entry */
        entry = lolelffs_xattr_find_entry(data, data_size, name_index, name, &entry_offset);
        if (!entry) {
            kfree(data);
            brelse(bh);
            return -ENODATA;
        }

        /* Calculate entry size */
        size_t entry_size = sizeof(struct lolelffs_xattr_entry) +
                           entry->name_len + 1 + entry->value_len;
        entry_size = (entry_size + 3) & ~3; /* Align to 4 bytes */

        /* Remove the entry by shifting data */
        if (entry_offset + entry_size < data_size) {
            memmove(data + entry_offset,
                   data + entry_offset + entry_size,
                   data_size - entry_offset - entry_size);
        }

        new_data_size = data_size - entry_size;
        ei->total_size = new_data_size;
        ei->count--;

        /* Write back data */
        size_t written = 0;
        for (ei_idx = 0; ei_idx < LOLELFFS_MAX_EXTENTS; ei_idx++) {
            struct lolelffs_extent *extent = &ei->extents[ei_idx];

            if (extent->ee_start == 0)
                break;

            for (bi = 0; bi < extent->ee_len && written < new_data_size; bi++) {
                size_t write_size = min_t(size_t, LOLELFFS_BLOCK_SIZE,
                                         new_data_size - written);

                bh2 = LOLELFFS_SB_BREAD(sb, extent->ee_start + bi);
                if (!bh2) {
                    ret = -EIO;
                    goto out;
                }

                memcpy(bh2->b_data, data + written, write_size);
                if (write_size < LOLELFFS_BLOCK_SIZE)
                    memset(bh2->b_data + write_size, 0, LOLELFFS_BLOCK_SIZE - write_size);

                mark_buffer_dirty(bh2);
                brelse(bh2);
                written += write_size;
            }
        }

        mark_buffer_dirty(bh);
        brelse(bh);
        kfree(data);

        mark_inode_dirty(inode);
        return 0;
    }

    /* Allocate xattr block if needed */
    if (ci->xattr_block == 0) {
        xattr_block = get_free_blocks(sbi, 1);
        if (!xattr_block)
            return -ENOSPC;

        ci->xattr_block = xattr_block;

        /* Initialize xattr extent index */
        bh = LOLELFFS_SB_BREAD(sb, xattr_block);
        if (!bh) {
            put_blocks(sbi, xattr_block, 1);
            ci->xattr_block = 0;
            return -EIO;
        }

        ei = (struct lolelffs_xattr_ei_block *)bh->b_data;
        memset(ei, 0, LOLELFFS_BLOCK_SIZE);
        mark_buffer_dirty(bh);
    } else {
        /* Read existing xattr block */
        bh = LOLELFFS_SB_BREAD(sb, ci->xattr_block);
        if (!bh)
            return -EIO;

        ei = (struct lolelffs_xattr_ei_block *)bh->b_data;
    }

    /* Read current xattr data */
    ret = lolelffs_xattr_read_data(sb, ei, &data, &data_size);
    if (ret) {
        brelse(bh);
        return ret;
    }

    /* Check if entry exists */
    entry = lolelffs_xattr_find_entry(data ? data : "", data_size,
                                      name_index, name, &entry_offset);

    if (entry) {
        /* Entry exists - check flags */
        if (flags & XATTR_CREATE) {
            ret = -EEXIST;
            goto out;
        }

        /* Remove old entry first, then add new one */
        size_t old_entry_size = sizeof(struct lolelffs_xattr_entry) +
                               entry->name_len + 1 + entry->value_len;
        old_entry_size = (old_entry_size + 3) & ~3;

        if (entry_offset + old_entry_size < data_size) {
            memmove(data + entry_offset,
                   data + entry_offset + old_entry_size,
                   data_size - entry_offset - old_entry_size);
        }
        data_size -= old_entry_size;
        ei->count--;
    } else {
        /* Entry doesn't exist - check flags */
        if (flags & XATTR_REPLACE) {
            ret = -ENODATA;
            goto out;
        }
    }

    /* Calculate new entry size */
    size_t new_entry_size = sizeof(struct lolelffs_xattr_entry) +
                           name_len + 1 + value_len;
    new_entry_size = (new_entry_size + 3) & ~3; /* Align to 4 bytes */

    new_data_size = data_size + new_entry_size;

    /* Check if we have space (simple check for now) */
    if (new_data_size > LOLELFFS_BLOCK_SIZE * 8) {
        ret = -ENOSPC;
        goto out;
    }

    /* Allocate new data buffer */
    new_data = kmalloc(new_data_size, GFP_KERNEL);
    if (!new_data) {
        ret = -ENOMEM;
        goto out;
    }

    /* Copy old data */
    if (data && data_size > 0)
        memcpy(new_data, data, data_size);

    /* Add new entry at the end */
    entry = (struct lolelffs_xattr_entry *)(new_data + data_size);
    entry->name_len = name_len;
    entry->name_index = name_index;
    entry->value_len = value_len;
    entry->value_offset = sizeof(struct lolelffs_xattr_entry) + name_len + 1;
    entry->reserved = 0;

    /* Write name */
    memcpy((char *)entry + sizeof(struct lolelffs_xattr_entry), name, name_len);
    *((char *)entry + sizeof(struct lolelffs_xattr_entry) + name_len) = '\0';

    /* Write value */
    memcpy((char *)entry + entry->value_offset, value, value_len);

    /* Pad to alignment */
    memset(new_data + data_size + sizeof(struct lolelffs_xattr_entry) +
           name_len + 1 + value_len, 0,
           new_entry_size - (sizeof(struct lolelffs_xattr_entry) + name_len + 1 + value_len));

    /* Allocate data blocks if needed */
    uint32_t blocks_needed = (new_data_size + LOLELFFS_BLOCK_SIZE - 1) / LOLELFFS_BLOCK_SIZE;

    if (ei->extents[0].ee_start == 0 || ei->extents[0].ee_len < blocks_needed) {
        /* Need to allocate or expand */
        uint32_t new_blocks = get_free_blocks(sbi, blocks_needed);
        if (!new_blocks) {
            ret = -ENOSPC;
            goto out;
        }

        /* Free old blocks if any */
        if (ei->extents[0].ee_start != 0) {
            put_blocks(sbi, ei->extents[0].ee_start, ei->extents[0].ee_len);
        }

        ei->extents[0].ee_block = 0;
        ei->extents[0].ee_start = new_blocks;
        ei->extents[0].ee_len = blocks_needed;
    }

    /* Write new data to blocks */
    size_t written = 0;
    for (bi = 0; bi < ei->extents[0].ee_len && written < new_data_size; bi++) {
        size_t write_size = min_t(size_t, LOLELFFS_BLOCK_SIZE, new_data_size - written);

        bh2 = LOLELFFS_SB_BREAD(sb, ei->extents[0].ee_start + bi);
        if (!bh2) {
            ret = -EIO;
            goto out;
        }

        memcpy(bh2->b_data, new_data + written, write_size);
        if (write_size < LOLELFFS_BLOCK_SIZE)
            memset(bh2->b_data + write_size, 0, LOLELFFS_BLOCK_SIZE - write_size);

        mark_buffer_dirty(bh2);
        brelse(bh2);
        written += write_size;
    }

    /* Update extent index */
    ei->total_size = new_data_size;
    ei->count++;

    mark_buffer_dirty(bh);
    mark_inode_dirty(inode);
    ret = 0;

out:
    if (bh)
        brelse(bh);
    kfree(data);
    kfree(new_data);
    return ret;
}

/* List extended attributes */
ssize_t lolelffs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
    struct inode *inode = d_inode(dentry);
    struct super_block *sb = inode->i_sb;
    struct lolelffs_inode_info *ci = LOLELFFS_INODE(inode);
    struct buffer_head *bh;
    struct lolelffs_xattr_ei_block *ei;
    char *data = NULL;
    size_t data_size, offset = 0, total_size = 0;
    int ret;

    /* No xattrs? */
    if (ci->xattr_block == 0)
        return 0;

    /* Read xattr extent index */
    bh = LOLELFFS_SB_BREAD(sb, ci->xattr_block);
    if (!bh)
        return -EIO;

    ei = (struct lolelffs_xattr_ei_block *)bh->b_data;

    /* Read xattr data */
    ret = lolelffs_xattr_read_data(sb, ei, &data, &data_size);
    brelse(bh);

    if (ret)
        return ret;

    if (!data || data_size == 0)
        return 0;

    /* Iterate through entries */
    while (offset + sizeof(struct lolelffs_xattr_entry) <= data_size) {
        struct lolelffs_xattr_entry *entry;
        const char *prefix;
        size_t prefix_len, name_len, full_len;

        entry = (struct lolelffs_xattr_entry *)(data + offset);

        if (entry->name_len == 0)
            break;

        prefix = xattr_prefixes[entry->name_index];
        prefix_len = strlen(prefix);
        name_len = entry->name_len;
        full_len = prefix_len + name_len + 1; /* +1 for NUL */

        if (buffer) {
            if (total_size + full_len > size) {
                ret = -ERANGE;
                goto out;
            }

            memcpy(buffer + total_size, prefix, prefix_len);
            memcpy(buffer + total_size + prefix_len,
                   data + offset + sizeof(struct lolelffs_xattr_entry),
                   name_len);
            buffer[total_size + prefix_len + name_len] = '\0';
        }

        total_size += full_len;

        /* Move to next entry */
        offset += sizeof(struct lolelffs_xattr_entry);
        offset += entry->name_len + 1;
        offset += entry->value_len;
        offset = (offset + 3) & ~3; /* Align */
    }

    ret = total_size;

out:
    kfree(data);
    return ret;
}

/* Free xattr blocks when deleting an inode */
void lolelffs_xattr_free_blocks(struct lolelffs_sb_info *sbi, struct inode *inode)
{
    struct lolelffs_inode_info *ci = LOLELFFS_INODE(inode);
    struct buffer_head *bh;
    struct lolelffs_xattr_ei_block *ei;
    int ei_idx;

    if (ci->xattr_block == 0)
        return;

    /* Read xattr extent index */
    bh = LOLELFFS_SB_BREAD(inode->i_sb, ci->xattr_block);
    if (!bh)
        return;

    ei = (struct lolelffs_xattr_ei_block *)bh->b_data;

    /* Free all extent blocks */
    for (ei_idx = 0; ei_idx < LOLELFFS_MAX_EXTENTS; ei_idx++) {
        if (ei->extents[ei_idx].ee_start == 0)
            break;

        put_blocks(sbi, ei->extents[ei_idx].ee_start, ei->extents[ei_idx].ee_len);
    }

    brelse(bh);

    /* Free the xattr index block itself */
    put_blocks(sbi, ci->xattr_block, 1);
    ci->xattr_block = 0;
}

/* Xattr handler get function */
static int lolelffs_xattr_handler_get(const struct xattr_handler *handler,
                                      struct dentry *unused, struct inode *inode,
                                      const char *name, void *buffer, size_t size)
{
    return lolelffs_xattr_get(inode, handler->flags, name, buffer, size);
}

/* Xattr handler set function */
static int lolelffs_xattr_handler_set(const struct xattr_handler *handler,
                                      struct mnt_idmap *idmap,
                                      struct dentry *unused, struct inode *inode,
                                      const char *name, const void *value,
                                      size_t size, int flags)
{
    return lolelffs_xattr_set(inode, handler->flags, name, value, size, flags);
}

/* Xattr handlers for each namespace */
static const struct xattr_handler lolelffs_xattr_user_handler = {
    .prefix = XATTR_USER_PREFIX,
    .flags  = LOLELFFS_XATTR_INDEX_USER,
    .get    = lolelffs_xattr_handler_get,
    .set    = lolelffs_xattr_handler_set,
};

static const struct xattr_handler lolelffs_xattr_trusted_handler = {
    .prefix = XATTR_TRUSTED_PREFIX,
    .flags  = LOLELFFS_XATTR_INDEX_TRUSTED,
    .get    = lolelffs_xattr_handler_get,
    .set    = lolelffs_xattr_handler_set,
};

static const struct xattr_handler lolelffs_xattr_security_handler = {
    .prefix = XATTR_SECURITY_PREFIX,
    .flags  = LOLELFFS_XATTR_INDEX_SECURITY,
    .get    = lolelffs_xattr_handler_get,
    .set    = lolelffs_xattr_handler_set,
};

const struct xattr_handler *lolelffs_xattr_handlers[] = {
    &lolelffs_xattr_user_handler,
    &lolelffs_xattr_trusted_handler,
    &lolelffs_xattr_security_handler,
    NULL,
};
