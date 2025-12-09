//! Core data structures for lolelffs filesystem

use std::fmt;

/// Magic number for lolelffs filesystems (0x101E1FF5 = "lolelffs" in hexspeak)
pub const LOLELFFS_MAGIC: u32 = 0x101E1FF5;

/// Block size in bytes (4 KB)
pub const LOLELFFS_BLOCK_SIZE: u32 = 4096;

/// Number of inodes per block (4096 / 72 = 56)
pub const LOLELFFS_INODES_PER_BLOCK: u32 = 56;

/// Maximum blocks per extent
pub const LOLELFFS_MAX_BLOCKS_PER_EXTENT: u32 = 65536;

/// Maximum extents per file
pub const LOLELFFS_MAX_EXTENTS: usize = 340;

/// Maximum filename length
pub const LOLELFFS_MAX_FILENAME: usize = 255;

/// Size of file entry structure
pub const LOLELFFS_FILE_ENTRY_SIZE: usize = 259;

/// Number of file entries per block
pub const LOLELFFS_FILES_PER_BLOCK: usize = 15;

/// Bits per bitmap block
pub const LOLELFFS_BITS_PER_BLOCK: u32 = LOLELFFS_BLOCK_SIZE * 8;

/// Root inode number
pub const LOLELFFS_ROOT_INO: u32 = 0;

/// Minimum filesystem size in blocks
pub const LOLELFFS_MIN_BLOCKS: u32 = 100;

/// File mode flags
pub mod mode {
    pub const S_IFMT: u32 = 0o170000;   // Type mask
    pub const S_IFREG: u32 = 0o100000;  // Regular file
    pub const S_IFDIR: u32 = 0o040000;  // Directory
    pub const S_IFLNK: u32 = 0o120000;  // Symbolic link
}

/// Superblock information structure (on-disk format)
#[derive(Debug, Clone)]
pub struct Superblock {
    /// Magic number (0x101E1FF5)
    pub magic: u32,
    /// Total number of blocks
    pub nr_blocks: u32,
    /// Total number of inodes
    pub nr_inodes: u32,
    /// Number of inode store blocks
    pub nr_istore_blocks: u32,
    /// Number of inode free bitmap blocks
    pub nr_ifree_blocks: u32,
    /// Number of block free bitmap blocks
    pub nr_bfree_blocks: u32,
    /// Number of free inodes
    pub nr_free_inodes: u32,
    /// Number of free blocks
    pub nr_free_blocks: u32,
}

impl Superblock {
    /// Size of superblock on disk
    pub const SIZE: usize = 32;

    /// Get the block number where inode store starts
    pub fn inode_store_start(&self) -> u32 {
        1 // Block 0 is superblock, block 1 starts inode store
    }

    /// Get the block number where inode free bitmap starts
    pub fn ifree_bitmap_start(&self) -> u32 {
        self.inode_store_start() + self.nr_istore_blocks
    }

    /// Get the block number where block free bitmap starts
    pub fn bfree_bitmap_start(&self) -> u32 {
        self.ifree_bitmap_start() + self.nr_ifree_blocks
    }

    /// Get the first data block number
    pub fn data_block_start(&self) -> u32 {
        self.bfree_bitmap_start() + self.nr_bfree_blocks
    }
}

/// Inode structure (on-disk format, 64 bytes)
#[derive(Debug, Clone)]
pub struct Inode {
    /// File mode (type + permissions)
    pub i_mode: u32,
    /// Owner user ID
    pub i_uid: u32,
    /// Owner group ID
    pub i_gid: u32,
    /// File size in bytes
    pub i_size: u32,
    /// Inode change time
    pub i_ctime: u32,
    /// Last access time
    pub i_atime: u32,
    /// Last modification time
    pub i_mtime: u32,
    /// Number of blocks
    pub i_blocks: u32,
    /// Hard link count
    pub i_nlink: u32,
    /// Block number containing extent index
    pub ei_block: u32,
    /// Inline data (symlink target or reserved)
    pub i_data: [u8; 32],
}

impl Inode {
    /// Size of inode on disk (10 * u32 + 32 bytes = 72 bytes)
    pub const SIZE: usize = 72;

    /// Check if this inode is a directory
    pub fn is_dir(&self) -> bool {
        (self.i_mode & mode::S_IFMT) == mode::S_IFDIR
    }

    /// Check if this inode is a regular file
    pub fn is_file(&self) -> bool {
        (self.i_mode & mode::S_IFMT) == mode::S_IFREG
    }

    /// Check if this inode is a symlink
    pub fn is_symlink(&self) -> bool {
        (self.i_mode & mode::S_IFMT) == mode::S_IFLNK
    }

    /// Get the file type character for display
    pub fn type_char(&self) -> char {
        if self.is_dir() {
            'd'
        } else if self.is_symlink() {
            'l'
        } else {
            '-'
        }
    }

    /// Get permission string (rwxrwxrwx format)
    pub fn perm_string(&self) -> String {
        let mode = self.i_mode;
        let mut s = String::with_capacity(9);

        // Owner permissions
        s.push(if mode & 0o400 != 0 { 'r' } else { '-' });
        s.push(if mode & 0o200 != 0 { 'w' } else { '-' });
        s.push(if mode & 0o100 != 0 { 'x' } else { '-' });

        // Group permissions
        s.push(if mode & 0o040 != 0 { 'r' } else { '-' });
        s.push(if mode & 0o020 != 0 { 'w' } else { '-' });
        s.push(if mode & 0o010 != 0 { 'x' } else { '-' });

        // Other permissions
        s.push(if mode & 0o004 != 0 { 'r' } else { '-' });
        s.push(if mode & 0o002 != 0 { 'w' } else { '-' });
        s.push(if mode & 0o001 != 0 { 'x' } else { '-' });

        s
    }
}

impl fmt::Display for Inode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} {} {} {}",
            self.type_char(),
            self.perm_string(),
            self.i_nlink,
            self.i_uid,
            self.i_gid,
            self.i_size
        )
    }
}

/// Extent structure (12 bytes)
#[derive(Debug, Clone, Copy, Default)]
pub struct Extent {
    /// First logical block number (in file)
    pub ee_block: u32,
    /// Number of blocks in extent
    pub ee_len: u32,
    /// First physical block number (on disk)
    pub ee_start: u32,
}

impl Extent {
    /// Size of extent on disk
    pub const SIZE: usize = 12;

    /// Check if extent is empty/unused
    pub fn is_empty(&self) -> bool {
        self.ee_len == 0
    }

    /// Check if logical block is within this extent
    pub fn contains(&self, logical_block: u32) -> bool {
        logical_block >= self.ee_block && logical_block < self.ee_block + self.ee_len
    }

    /// Get physical block for logical block
    pub fn get_physical(&self, logical_block: u32) -> Option<u32> {
        if self.contains(logical_block) {
            Some(self.ee_start + (logical_block - self.ee_block))
        } else {
            None
        }
    }
}

/// Extent index block structure
#[derive(Debug, Clone)]
pub struct ExtentIndex {
    /// Number of files (for directories)
    pub nr_files: u32,
    /// Array of extents
    pub extents: Vec<Extent>,
}

impl ExtentIndex {
    /// Read extent index from raw block data
    pub fn from_bytes(data: &[u8]) -> Self {
        use byteorder::{LittleEndian, ReadBytesExt};
        use std::io::Cursor;

        let mut cursor = Cursor::new(data);
        let nr_files = cursor.read_u32::<LittleEndian>().unwrap_or(0);

        let mut extents = Vec::with_capacity(LOLELFFS_MAX_EXTENTS);
        for _ in 0..LOLELFFS_MAX_EXTENTS {
            let ee_block = cursor.read_u32::<LittleEndian>().unwrap_or(0);
            let ee_len = cursor.read_u32::<LittleEndian>().unwrap_or(0);
            let ee_start = cursor.read_u32::<LittleEndian>().unwrap_or(0);
            extents.push(Extent {
                ee_block,
                ee_len,
                ee_start,
            });
        }

        ExtentIndex { nr_files, extents }
    }

    /// Serialize extent index to bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        use byteorder::{LittleEndian, WriteBytesExt};

        let mut data = Vec::with_capacity(LOLELFFS_BLOCK_SIZE as usize);
        data.write_u32::<LittleEndian>(self.nr_files).unwrap();

        for i in 0..LOLELFFS_MAX_EXTENTS {
            let extent = self.extents.get(i).copied().unwrap_or_default();
            data.write_u32::<LittleEndian>(extent.ee_block).unwrap();
            data.write_u32::<LittleEndian>(extent.ee_len).unwrap();
            data.write_u32::<LittleEndian>(extent.ee_start).unwrap();
        }

        // Pad to block size
        data.resize(LOLELFFS_BLOCK_SIZE as usize, 0);
        data
    }

    /// Find extent containing logical block using binary search
    pub fn find_extent(&self, logical_block: u32) -> Option<&Extent> {
        // Find the first non-empty extent that could contain this block
        let mut left = 0;
        let mut right = self.extents.len();

        while left < right {
            let mid = left + (right - left) / 2;
            let extent = &self.extents[mid];

            if extent.is_empty() {
                right = mid;
            } else if extent.ee_block > logical_block {
                right = mid;
            } else if extent.ee_block + extent.ee_len <= logical_block {
                left = mid + 1;
            } else {
                return Some(extent);
            }
        }

        None
    }

    /// Get the total number of blocks used
    pub fn total_blocks(&self) -> u32 {
        self.extents.iter().map(|e| e.ee_len).sum()
    }

    /// Count used extents
    pub fn count_extents(&self) -> usize {
        self.extents.iter().take_while(|e| !e.is_empty()).count()
    }
}

/// Directory file entry (259 bytes)
#[derive(Debug, Clone)]
pub struct FileEntry {
    /// Inode number
    pub inode: u32,
    /// Filename (null-terminated)
    pub filename: String,
}

impl FileEntry {
    /// Size of file entry on disk
    pub const SIZE: usize = LOLELFFS_FILE_ENTRY_SIZE;

    /// Read file entry from raw data
    pub fn from_bytes(data: &[u8]) -> Option<Self> {
        if data.len() < Self::SIZE {
            return None;
        }

        use byteorder::{LittleEndian, ReadBytesExt};
        use std::io::Cursor;

        let mut cursor = Cursor::new(data);
        let inode = cursor.read_u32::<LittleEndian>().ok()?;

        // Read filename (null-terminated)
        let filename_bytes = &data[4..Self::SIZE];
        let filename = filename_bytes
            .iter()
            .take_while(|&&b| b != 0)
            .copied()
            .collect::<Vec<u8>>();
        let filename = String::from_utf8_lossy(&filename).to_string();

        if filename.is_empty() {
            return None;
        }

        Some(FileEntry { inode, filename })
    }

    /// Serialize file entry to bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        use byteorder::{LittleEndian, WriteBytesExt};

        let mut data = Vec::with_capacity(Self::SIZE);
        data.write_u32::<LittleEndian>(self.inode).unwrap();

        // Write filename with null termination
        let filename_bytes = self.filename.as_bytes();
        let copy_len = filename_bytes.len().min(LOLELFFS_MAX_FILENAME - 1);
        data.extend_from_slice(&filename_bytes[..copy_len]);

        // Pad to full filename size
        data.resize(Self::SIZE, 0);
        data
    }
}
