#ifndef LOLELFFS_H
#define LOLELFFS_H

/* source: https://en.wikipedia.org/wiki/Hexspeak */
#define LOLELFFS_MAGIC 0x101E1FF5

#define LOLELFFS_SB_BLOCK_NR 0
#define LOLELFFS_SB_SECTION_NAME .lolfs.super
#define LOLELFFS_SB_SECTION ".lolfs.super"

#define LOLELFFS_BLOCK_SIZE (1 << 12) /* 4 KiB */
#define LOLELFFS_MAX_BLOCKS_PER_EXTENT 65536 /* It can be ~(uint32) 0 */

#define LOLELFFS_FILENAME_LEN 255

/* Extent structure - needed for userspace calculations */
struct lolelffs_extent {
    uint32_t ee_block; /* first logical block extent covers */
    uint32_t ee_len;   /* number of blocks covered by extent */
    uint32_t ee_start; /* first physical block extent covers */
};

/* File entry structure - needed for userspace calculations */
struct lolelffs_file {
    uint32_t inode;
    char filename[LOLELFFS_FILENAME_LEN];
};

#define LOLELFFS_MAX_EXTENTS \
    ((LOLELFFS_BLOCK_SIZE - sizeof(uint32_t)) / sizeof(struct lolelffs_extent))
#define LOLELFFS_MAX_FILESIZE                                      \
    ((uint64_t) LOLELFFS_MAX_BLOCKS_PER_EXTENT *LOLELFFS_BLOCK_SIZE \
        *LOLELFFS_MAX_EXTENTS)

#define LOLELFFS_FILES_PER_BLOCK \
    (LOLELFFS_BLOCK_SIZE / sizeof(struct lolelffs_file))
#define LOLELFFS_FILES_PER_EXT \
    (LOLELFFS_FILES_PER_BLOCK *LOLELFFS_MAX_BLOCKS_PER_EXTENT)

#define LOLELFFS_MAX_SUBFILES \
    (LOLELFFS_FILES_PER_EXT *LOLELFFS_MAX_EXTENTS)


#include <linux/version.h>
#define USER_NS_REQUIRED() (LINUX_VERSION_CODE >= KERNEL_VERSION(5,12,0) && LINUX_VERSION_CODE < KERNEL_VERSION(6,3,0))
#define MNT_IDMAP_REQUIRED() (LINUX_VERSION_CODE >= KERNEL_VERSION(6,3,0))

/*
 * lolelffs partition layout (within a .lolfs elf section)
 * +---------------+
 * |  superblock   |  1 block
 * +---------------+
 * |  inode store  |  sb->nr_istore_blocks blocks
 * +---------------+
 * | ifree bitmap  |  sb->nr_ifree_blocks blocks
 * +---------------+
 * | bfree bitmap  |  sb->nr_bfree_blocks blocks
 * +---------------+
 * |    data       |
 * |      blocks   |  rest of the blocks
 * +---------------+
 */

struct lolelffs_inode {
    uint32_t i_mode;   /* File mode */
    uint32_t i_uid;    /* Owner id */
    uint32_t i_gid;    /* Group id */
    uint32_t i_size;   /* Size in bytes */
    uint32_t i_ctime;  /* Inode change time */
    uint32_t i_atime;  /* Access time */
    uint32_t i_mtime;  /* Modification time */
    uint32_t i_blocks; /* Block count */
    uint32_t i_nlink;  /* Hard links count */
    uint32_t ei_block;  /* Block with list of extents for this file */
    char i_data[32]; /* store symlink content */
};

#define LOLELFFS_INODES_PER_BLOCK \
    (LOLELFFS_BLOCK_SIZE / sizeof(struct lolelffs_inode))

struct lolelffs_sb_info {
    uint32_t magic; /* Magic number */

    uint32_t nr_blocks; /* Total number of blocks (incl sb & inodes) */
    uint32_t nr_inodes; /* Total number of inodes */

    uint32_t nr_istore_blocks; /* Number of inode store blocks */
    uint32_t nr_ifree_blocks;  /* Number of inode free bitmap blocks */
    uint32_t nr_bfree_blocks;  /* Number of block free bitmap blocks */

    uint32_t nr_free_inodes; /* Number of free inodes */
    uint32_t nr_free_blocks; /* Number of free blocks */

#ifdef __KERNEL__
    unsigned long *ifree_bitmap; /* In-memory free inodes bitmap */
    unsigned long *bfree_bitmap; /* In-memory free blocks bitmap */
    struct mutex lock; /* Protects bitmap and free counters */
    loff_t fs_offset; /* Offset to filesystem data (0 for raw, or ELF section offset) */
#endif
};

#ifdef __KERNEL__

/* Helper macro to read blocks with ELF offset adjustment */
#define LOLELFFS_SB_BREAD(sb, block) \
    sb_bread((sb), (block) + ((struct lolelffs_sb_info *)(sb)->s_fs_info)->fs_offset)

struct lolelffs_inode_info {
    uint32_t ei_block;  /* Block with list of extents for this file */
    char i_data[32];
    /* Extent cache hints for performance optimization */
    uint32_t cached_extent_idx;   /* Last accessed extent index */
    uint32_t cached_extent_count; /* Cached number of used extents */
    uint32_t cache_valid;         /* Cache validity flags */
    struct inode vfs_inode;
};

/* Cache validity flags */
#define LOLELFFS_CACHE_EXTENT_COUNT  0x01
#define LOLELFFS_CACHE_EXTENT_IDX    0x02

struct lolelffs_file_ei_block {
    uint32_t nr_files; /* Number of files in directory */
    struct lolelffs_extent extents[LOLELFFS_MAX_EXTENTS];
};

struct lolelffs_dir_block {
    struct lolelffs_file files[LOLELFFS_FILES_PER_BLOCK];
};

/* superblock functions */
int lolelffs_fill_super(struct super_block *sb, void *data, int silent);

/* inode functions */
int lolelffs_init_inode_cache(void);
void lolelffs_destroy_inode_cache(void);
struct inode *lolelffs_iget(struct super_block *sb, unsigned long ino);

/* file functions */
extern const struct file_operations lolelffs_file_ops;
extern const struct file_operations lolelffs_dir_ops;
extern const struct address_space_operations lolelffs_aops;

/* extent functions */
extern uint32_t lolelffs_ext_search(struct lolelffs_file_ei_block *index,
                                    uint32_t iblock);

/* Getters for superbock and inode */
#define LOLELFFS_SB(sb) (sb->s_fs_info)
#define LOLELFFS_INODE(inode) \
    (container_of(inode, struct lolelffs_inode_info, vfs_inode))

#endif /* __KERNEL__ */

#endif /* LOLELFFS_H */
