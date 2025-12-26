#ifndef LOLELFFS_H
#define LOLELFFS_H

/* source: https://en.wikipedia.org/wiki/Hexspeak */
#define LOLELFFS_MAGIC 0x101E1FF5

#define LOLELFFS_SB_BLOCK_NR 0
#define LOLELFFS_SB_SECTION_NAME .lolfs.super
#define LOLELFFS_SB_SECTION ".lolfs.super"

#define LOLELFFS_BLOCK_SIZE (1 << 12) /* 4 KiB */
#define LOLELFFS_MAX_BLOCKS_PER_EXTENT 2048 /* Max blocks per extent with compression */
#define LOLELFFS_MAX_BLOCKS_PER_EXTENT_LARGE 524288 /* Max for uncompressed/uniform (512K blocks = 2GB) */

#define LOLELFFS_FILENAME_LEN 255

/* Filesystem version (always 1 - compression support is mandatory) */
#define LOLELFFS_VERSION 1

/* Feature flags for comp_features field */
#define LOLELFFS_FEATURE_LARGE_EXTENTS 0x0001

/* Compression algorithm IDs */
#define LOLELFFS_COMP_NONE      0  /* No compression */
#define LOLELFFS_COMP_LZ4       1  /* LZ4 (fast, good ratio) */
#define LOLELFFS_COMP_ZLIB      2  /* zlib/deflate (moderate speed, better ratio) */
#define LOLELFFS_COMP_ZSTD      3  /* zstd (configurable, best ratio) */

/* Encryption algorithm IDs */
#define LOLELFFS_ENC_NONE           0  /* No encryption */
#define LOLELFFS_ENC_AES256_XTS     1  /* AES-256-XTS (block device encryption) */
#define LOLELFFS_ENC_CHACHA20_POLY  2  /* ChaCha20-Poly1305 (authenticated encryption) */

/* Key derivation function IDs */
#define LOLELFFS_KDF_NONE           0  /* No KDF */
#define LOLELFFS_KDF_ARGON2ID       1  /* Argon2id (recommended) */
#define LOLELFFS_KDF_PBKDF2         2  /* PBKDF2-HMAC-SHA256 */

/* Compression metadata magic */
#define LOLELFFS_COMP_META_MAGIC 0xC04FFEE5

/* Extent flags */
#define LOLELFFS_EXT_COMPRESSED   0x0001  /* Extent contains compressed blocks */
#define LOLELFFS_EXT_ENCRYPTED    0x0002  /* Extent contains encrypted blocks */
#define LOLELFFS_EXT_HAS_META     0x0004  /* Has per-block metadata */
#define LOLELFFS_EXT_MIXED        0x0008  /* Mixed compressed/uncompressed/encrypted */

/* Extent structure with compression and encryption support (24 bytes) */
struct lolelffs_extent {
    uint32_t ee_block;      /* First logical block number */
    uint32_t ee_len;        /* Number of blocks in extent */
    uint32_t ee_start;      /* First physical block number */
    uint16_t ee_comp_algo;  /* Compression algorithm for extent */
    uint8_t  ee_enc_algo;   /* Encryption algorithm for extent */
    uint8_t  ee_reserved;   /* Reserved for alignment */
    uint16_t ee_flags;      /* Flags (LOLELFFS_EXT_*) */
    uint16_t ee_reserved2;  /* Reserved for alignment */
    uint32_t ee_meta;       /* Block number of metadata (compression/encryption) */
};

/* Compression metadata for a single block */
struct lolelffs_comp_block_meta {
    uint16_t comp_size;     /* Compressed size (0 = uncompressed) */
    uint8_t  comp_algo;     /* Algorithm override (0 = use extent default) */
    uint8_t  flags;         /* Reserved */
};

/* Compression metadata block (one 4KB block, supports up to 2040 blocks) */
struct lolelffs_comp_metadata {
    uint32_t magic;         /* Magic: LOLELFFS_COMP_META_MAGIC */
    uint32_t nr_blocks;     /* Number of blocks with metadata */
    struct lolelffs_comp_block_meta blocks[2040];  /* 2040 entries = 8160 bytes */
    char padding[1928];     /* Pad to 4096 bytes (8 + 8160 + 1928 = 4096) */
};

/* File entry structure - needed for userspace calculations */
struct lolelffs_file {
    uint32_t inode;
    char filename[LOLELFFS_FILENAME_LEN];
};

#define LOLELFFS_MAX_EXTENTS \
    ((LOLELFFS_BLOCK_SIZE - sizeof(uint32_t)) / sizeof(struct lolelffs_extent))
#define LOLELFFS_MAX_FILESIZE                                      \
    ((uint64_t) LOLELFFS_MAX_BLOCKS_PER_EXTENT_LARGE * LOLELFFS_BLOCK_SIZE \
        * LOLELFFS_MAX_EXTENTS)

#define LOLELFFS_FILES_PER_BLOCK \
    (LOLELFFS_BLOCK_SIZE / sizeof(struct lolelffs_file))
#define LOLELFFS_FILES_PER_EXT \
    (LOLELFFS_FILES_PER_BLOCK *LOLELFFS_MAX_BLOCKS_PER_EXTENT)

#define LOLELFFS_MAX_SUBFILES \
    (LOLELFFS_FILES_PER_EXT *LOLELFFS_MAX_EXTENTS)

/* Extended attribute (xattr) support */
#define LOLELFFS_XATTR_INDEX_USER       0
#define LOLELFFS_XATTR_INDEX_TRUSTED    1
#define LOLELFFS_XATTR_INDEX_SYSTEM     2
#define LOLELFFS_XATTR_INDEX_SECURITY   3

/* Xattr entry header structure */
struct lolelffs_xattr_entry {
    uint8_t name_len;      /* Length of name (not including NUL) */
    uint8_t name_index;    /* Namespace index */
    uint16_t value_len;    /* Length of value */
    uint32_t value_offset; /* Offset from entry header to value */
    uint32_t reserved;     /* Reserved for future use */
};

/* Xattr extent index block */
struct lolelffs_xattr_ei_block {
    uint32_t total_size;   /* Total size of all xattrs */
    uint32_t count;        /* Number of xattr entries */
    struct lolelffs_extent extents[LOLELFFS_MAX_EXTENTS];
};

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
    uint32_t xattr_block; /* Block with xattr extent index (0 = no xattrs) */
    char i_data[28]; /* store symlink content (max 27 chars + NUL) */
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

    /* Compression support (mandatory) */
    uint32_t version;              /* Filesystem version (always 1) */
    uint32_t comp_default_algo;    /* Default compression algorithm */
    uint32_t comp_enabled;         /* Compression enabled flag */
    uint32_t comp_min_block_size;  /* Don't compress blocks smaller than this */
    uint32_t comp_features;        /* Feature flags for future extensions */
    uint32_t max_extent_blocks;    /* Max blocks per extent (2048) */
    uint32_t max_extent_blocks_large; /* Max blocks for extents without metadata (524288) */

    /* Encryption support */
    uint32_t enc_enabled;          /* Encryption enabled flag */
    uint32_t enc_default_algo;     /* Default encryption algorithm */
    uint32_t enc_kdf_algo;         /* Key derivation function (Argon2id) */
    uint32_t enc_kdf_iterations;   /* KDF iterations */
    uint32_t enc_kdf_memory;       /* KDF memory cost (KB) */
    uint32_t enc_kdf_parallelism;  /* KDF parallelism */
    uint8_t  enc_salt[32];         /* Salt for key derivation (32 bytes) */
    uint8_t  enc_master_key[32];   /* Encrypted master key (32 bytes) */
    uint32_t enc_features;         /* Feature flags for future extensions */
    uint32_t reserved[3];          /* Reserved for future use */

#ifdef __KERNEL__
    unsigned long *ifree_bitmap; /* In-memory free inodes bitmap */
    unsigned long *bfree_bitmap; /* In-memory free blocks bitmap */
    struct mutex lock; /* Protects bitmap and free counters */
    loff_t fs_offset; /* Offset to filesystem data (0 for raw, or ELF section offset) */

    /* Encryption runtime state */
    u8 enc_master_key_decrypted[32]; /* Decrypted master key (in memory only) */
    bool enc_unlocked; /* True if filesystem is unlocked */
    struct mutex enc_lock; /* Protects encryption state */
#endif
};

#ifdef __KERNEL__

#include <linux/version.h>
#define USER_NS_REQUIRED() (LINUX_VERSION_CODE >= KERNEL_VERSION(5,12,0) && LINUX_VERSION_CODE < KERNEL_VERSION(6,3,0))
#define MNT_IDMAP_REQUIRED() (LINUX_VERSION_CODE >= KERNEL_VERSION(6,3,0))

/* Helper macro to read blocks with ELF offset adjustment */
#define LOLELFFS_SB_BREAD(sb, block) \
    sb_bread((sb), (block) + ((struct lolelffs_sb_info *)(sb)->s_fs_info)->fs_offset)

struct lolelffs_inode_info {
    uint32_t ei_block;  /* Block with list of extents for this file */
    uint32_t xattr_block; /* Block with xattr extent index (0 = no xattrs) */
    char i_data[28];
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
int lolelffs_write_inode(struct inode *inode, struct writeback_control *wbc);
int lolelffs_sync_fs(struct super_block *sb, int wait);

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

/* xattr functions */
extern const struct xattr_handler *lolelffs_xattr_handlers[];
ssize_t lolelffs_listxattr(struct dentry *dentry, char *buffer, size_t size);
void lolelffs_xattr_free_blocks(struct lolelffs_sb_info *sbi, struct inode *inode);

/* Getters for superbock and inode */
#define LOLELFFS_SB(sb) (sb->s_fs_info)
#define LOLELFFS_INODE(inode) \
    (container_of(inode, struct lolelffs_inode_info, vfs_inode))

/* Compression helper */
#define LOLELFFS_IS_COMPRESSED_ENABLED(sbi) ((sbi)->comp_enabled)

/* ioctl definitions */
#define LOLELFFS_IOC_MAGIC 'L'

/* Unlock encrypted filesystem - requires password */
struct lolelffs_ioctl_unlock {
    char password[256];
    uint32_t password_len;
};

#define LOLELFFS_IOC_UNLOCK _IOW(LOLELFFS_IOC_MAGIC, 1, struct lolelffs_ioctl_unlock)

/* Get encryption status */
struct lolelffs_ioctl_enc_status {
    uint32_t enc_enabled;
    uint32_t enc_unlocked;
    uint32_t enc_algorithm;
};

#define LOLELFFS_IOC_ENC_STATUS _IOR(LOLELFFS_IOC_MAGIC, 2, struct lolelffs_ioctl_enc_status)

#endif /* __KERNEL__ */

#endif /* LOLELFFS_H */
