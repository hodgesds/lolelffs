//! Filesystem operations for lolelffs

use crate::types::*;
use anyhow::{bail, Context, Result};
use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use std::fs::{File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;

/// Main filesystem handle
pub struct LolelfFs {
    file: File,
    pub superblock: Superblock,
}

impl LolelfFs {
    /// Open an existing lolelffs filesystem image
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self> {
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(path.as_ref())
            .with_context(|| format!("Failed to open {}", path.as_ref().display()))?;

        let superblock = Self::read_superblock(&mut file)?;

        if superblock.magic != LOLELFFS_MAGIC {
            bail!(
                "Invalid magic number: expected 0x{:08X}, got 0x{:08X}",
                LOLELFFS_MAGIC,
                superblock.magic
            );
        }

        Ok(LolelfFs { file, superblock })
    }

    /// Open filesystem in read-only mode
    pub fn open_readonly<P: AsRef<Path>>(path: P) -> Result<Self> {
        let mut file = File::open(path.as_ref())
            .with_context(|| format!("Failed to open {}", path.as_ref().display()))?;

        let superblock = Self::read_superblock(&mut file)?;

        if superblock.magic != LOLELFFS_MAGIC {
            bail!(
                "Invalid magic number: expected 0x{:08X}, got 0x{:08X}",
                LOLELFFS_MAGIC,
                superblock.magic
            );
        }

        Ok(LolelfFs { file, superblock })
    }

    /// Read superblock from file
    fn read_superblock(file: &mut File) -> Result<Superblock> {
        file.seek(SeekFrom::Start(0))?;

        let magic = file.read_u32::<LittleEndian>()?;
        let nr_blocks = file.read_u32::<LittleEndian>()?;
        let nr_inodes = file.read_u32::<LittleEndian>()?;
        let nr_istore_blocks = file.read_u32::<LittleEndian>()?;
        let nr_ifree_blocks = file.read_u32::<LittleEndian>()?;
        let nr_bfree_blocks = file.read_u32::<LittleEndian>()?;
        let nr_free_inodes = file.read_u32::<LittleEndian>()?;
        let nr_free_blocks = file.read_u32::<LittleEndian>()?;

        Ok(Superblock {
            magic,
            nr_blocks,
            nr_inodes,
            nr_istore_blocks,
            nr_ifree_blocks,
            nr_bfree_blocks,
            nr_free_inodes,
            nr_free_blocks,
        })
    }

    /// Write superblock to disk
    pub fn write_superblock(&mut self) -> Result<()> {
        self.file.seek(SeekFrom::Start(0))?;

        self.file.write_u32::<LittleEndian>(self.superblock.magic)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.nr_blocks)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.nr_inodes)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.nr_istore_blocks)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.nr_ifree_blocks)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.nr_bfree_blocks)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.nr_free_inodes)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.nr_free_blocks)?;

        self.file.flush()?;
        Ok(())
    }

    /// Read a block from the filesystem
    pub fn read_block(&mut self, block_num: u32) -> Result<Vec<u8>> {
        let offset = block_num as u64 * LOLELFFS_BLOCK_SIZE as u64;
        self.file.seek(SeekFrom::Start(offset))?;

        let mut data = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
        self.file.read_exact(&mut data)?;
        Ok(data)
    }

    /// Write a block to the filesystem
    pub fn write_block(&mut self, block_num: u32, data: &[u8]) -> Result<()> {
        if data.len() != LOLELFFS_BLOCK_SIZE as usize {
            bail!(
                "Block data must be {} bytes, got {}",
                LOLELFFS_BLOCK_SIZE,
                data.len()
            );
        }

        let offset = block_num as u64 * LOLELFFS_BLOCK_SIZE as u64;
        self.file.seek(SeekFrom::Start(offset))?;
        self.file.write_all(data)?;
        self.file.flush()?;
        Ok(())
    }

    /// Read an inode from the filesystem
    pub fn read_inode(&mut self, inode_num: u32) -> Result<Inode> {
        if inode_num >= self.superblock.nr_inodes {
            bail!(
                "Invalid inode number {} (max {})",
                inode_num,
                self.superblock.nr_inodes - 1
            );
        }

        let block_num =
            self.superblock.inode_store_start() + (inode_num / LOLELFFS_INODES_PER_BLOCK);
        let offset_in_block = (inode_num % LOLELFFS_INODES_PER_BLOCK) * Inode::SIZE as u32;

        let block = self.read_block(block_num)?;
        let inode_data = &block[offset_in_block as usize..offset_in_block as usize + Inode::SIZE];

        Self::parse_inode(inode_data)
    }

    /// Parse inode from raw bytes
    fn parse_inode(data: &[u8]) -> Result<Inode> {
        use std::io::Cursor;
        let mut cursor = Cursor::new(data);

        let i_mode = cursor.read_u32::<LittleEndian>()?;
        let i_uid = cursor.read_u32::<LittleEndian>()?;
        let i_gid = cursor.read_u32::<LittleEndian>()?;
        let i_size = cursor.read_u32::<LittleEndian>()?;
        let i_ctime = cursor.read_u32::<LittleEndian>()?;
        let i_atime = cursor.read_u32::<LittleEndian>()?;
        let i_mtime = cursor.read_u32::<LittleEndian>()?;
        let i_blocks = cursor.read_u32::<LittleEndian>()?;
        let i_nlink = cursor.read_u32::<LittleEndian>()?;
        let ei_block = cursor.read_u32::<LittleEndian>()?;

        let mut i_data = [0u8; 32];
        cursor.read_exact(&mut i_data)?;

        Ok(Inode {
            i_mode,
            i_uid,
            i_gid,
            i_size,
            i_ctime,
            i_atime,
            i_mtime,
            i_blocks,
            i_nlink,
            ei_block,
            i_data,
        })
    }

    /// Write an inode to the filesystem
    pub fn write_inode(&mut self, inode_num: u32, inode: &Inode) -> Result<()> {
        if inode_num >= self.superblock.nr_inodes {
            bail!(
                "Invalid inode number {} (max {})",
                inode_num,
                self.superblock.nr_inodes - 1
            );
        }

        let block_num =
            self.superblock.inode_store_start() + (inode_num / LOLELFFS_INODES_PER_BLOCK);
        let offset_in_block = (inode_num % LOLELFFS_INODES_PER_BLOCK) * Inode::SIZE as u32;

        // Read the block, modify the inode, write back
        let mut block = self.read_block(block_num)?;
        let inode_data = Self::serialize_inode(inode);
        block[offset_in_block as usize..offset_in_block as usize + Inode::SIZE]
            .copy_from_slice(&inode_data);
        self.write_block(block_num, &block)?;

        Ok(())
    }

    /// Serialize inode to bytes
    fn serialize_inode(inode: &Inode) -> Vec<u8> {
        let mut data = Vec::with_capacity(Inode::SIZE);
        data.write_u32::<LittleEndian>(inode.i_mode).unwrap();
        data.write_u32::<LittleEndian>(inode.i_uid).unwrap();
        data.write_u32::<LittleEndian>(inode.i_gid).unwrap();
        data.write_u32::<LittleEndian>(inode.i_size).unwrap();
        data.write_u32::<LittleEndian>(inode.i_ctime).unwrap();
        data.write_u32::<LittleEndian>(inode.i_atime).unwrap();
        data.write_u32::<LittleEndian>(inode.i_mtime).unwrap();
        data.write_u32::<LittleEndian>(inode.i_blocks).unwrap();
        data.write_u32::<LittleEndian>(inode.i_nlink).unwrap();
        data.write_u32::<LittleEndian>(inode.ei_block).unwrap();
        data.extend_from_slice(&inode.i_data);
        data
    }

    /// Read extent index block for an inode
    pub fn read_extent_index(&mut self, inode: &Inode) -> Result<ExtentIndex> {
        if inode.ei_block == 0 {
            bail!("Inode has no extent index block");
        }
        let block = self.read_block(inode.ei_block)?;
        Ok(ExtentIndex::from_bytes(&block))
    }

    /// Write extent index block
    pub fn write_extent_index(&mut self, block_num: u32, ei: &ExtentIndex) -> Result<()> {
        let data = ei.to_bytes();
        self.write_block(block_num, &data)
    }

    /// Get the physical block number for a logical block in a file
    pub fn get_physical_block(&mut self, inode: &Inode, logical_block: u32) -> Result<Option<u32>> {
        let ei = self.read_extent_index(inode)?;

        if let Some(extent) = ei.find_extent(logical_block) {
            Ok(extent.get_physical(logical_block))
        } else {
            Ok(None)
        }
    }

    /// Create a new filesystem on an image file
    pub fn create<P: AsRef<Path>>(path: P, size: u64) -> Result<Self> {
        let path = path.as_ref();

        // Create the file with the specified size
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(true)
            .open(path)?;
        file.set_len(size)?;

        let nr_blocks = (size / LOLELFFS_BLOCK_SIZE as u64) as u32;
        if nr_blocks < LOLELFFS_MIN_BLOCKS {
            bail!(
                "Filesystem too small: need at least {} blocks, got {}",
                LOLELFFS_MIN_BLOCKS,
                nr_blocks
            );
        }

        // Calculate filesystem layout
        let nr_inodes = ((nr_blocks / LOLELFFS_INODES_PER_BLOCK) + 1) * LOLELFFS_INODES_PER_BLOCK;
        let nr_istore_blocks = nr_inodes / LOLELFFS_INODES_PER_BLOCK;
        let nr_ifree_blocks = (nr_inodes + LOLELFFS_BITS_PER_BLOCK - 1) / LOLELFFS_BITS_PER_BLOCK;
        let nr_bfree_blocks = (nr_blocks + LOLELFFS_BITS_PER_BLOCK - 1) / LOLELFFS_BITS_PER_BLOCK;

        // Create superblock
        let superblock = Superblock {
            magic: LOLELFFS_MAGIC,
            nr_blocks,
            nr_inodes,
            nr_istore_blocks,
            nr_ifree_blocks,
            nr_bfree_blocks,
            nr_free_inodes: nr_inodes - 1, // Root inode is used
            nr_free_blocks: 0,             // Will be calculated
        };

        let mut fs = LolelfFs { file, superblock };

        // Initialize the filesystem
        fs.init_filesystem()?;

        Ok(fs)
    }

    /// Initialize filesystem structures
    fn init_filesystem(&mut self) -> Result<()> {
        // Write superblock
        self.write_superblock()?;

        // Initialize bitmaps
        let ifree_start = self.superblock.ifree_bitmap_start();
        let bfree_start = self.superblock.bfree_bitmap_start();
        let data_start = self.superblock.data_block_start();

        // Initialize inode free bitmap (mark inode 0 as used)
        let mut ifree_block = vec![0xFFu8; LOLELFFS_BLOCK_SIZE as usize];
        ifree_block[0] = 0xFE; // First inode (root) is used
        for i in 0..self.superblock.nr_ifree_blocks {
            if i == 0 {
                self.write_block(ifree_start + i, &ifree_block)?;
            } else {
                self.write_block(
                    ifree_start + i,
                    &vec![0xFFu8; LOLELFFS_BLOCK_SIZE as usize],
                )?;
            }
        }

        // Initialize block free bitmap (mark metadata blocks as used)
        let mut free_blocks = 0u32;
        for i in 0..self.superblock.nr_bfree_blocks {
            let mut block = vec![0xFFu8; LOLELFFS_BLOCK_SIZE as usize];
            let block_start = i * LOLELFFS_BITS_PER_BLOCK;

            for bit in 0..LOLELFFS_BITS_PER_BLOCK {
                let block_num = block_start + bit;
                if block_num >= self.superblock.nr_blocks {
                    // Mark non-existent blocks as used
                    let byte_idx = (bit / 8) as usize;
                    let bit_idx = bit % 8;
                    block[byte_idx] &= !(1 << bit_idx);
                } else if block_num < data_start + 1 {
                    // Mark metadata blocks and root dir extent block as used
                    let byte_idx = (bit / 8) as usize;
                    let bit_idx = bit % 8;
                    block[byte_idx] &= !(1 << bit_idx);
                } else {
                    free_blocks += 1;
                }
            }

            self.write_block(bfree_start + i, &block)?;
        }

        // Update free blocks count
        self.superblock.nr_free_blocks = free_blocks;
        self.write_superblock()?;

        // Create root inode
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as u32;

        let root_inode = Inode {
            i_mode: mode::S_IFDIR | 0o755,
            i_uid: 0,
            i_gid: 0,
            i_size: 0,
            i_ctime: now,
            i_atime: now,
            i_mtime: now,
            i_blocks: 0,
            i_nlink: 2, // . and itself
            ei_block: data_start,
            i_data: [0u8; 32],
        };
        self.write_inode(LOLELFFS_ROOT_INO, &root_inode)?;

        // Initialize root directory extent index block
        let root_ei = ExtentIndex {
            nr_files: 0,
            extents: vec![Extent::default(); LOLELFFS_MAX_EXTENTS],
        };
        self.write_extent_index(data_start, &root_ei)?;

        Ok(())
    }

    /// Get filesystem statistics
    pub fn statfs(&self) -> FsStats {
        FsStats {
            total_blocks: self.superblock.nr_blocks,
            free_blocks: self.superblock.nr_free_blocks,
            total_inodes: self.superblock.nr_inodes,
            free_inodes: self.superblock.nr_free_inodes,
            block_size: LOLELFFS_BLOCK_SIZE,
        }
    }
}

/// Filesystem statistics
#[derive(Debug, Clone)]
pub struct FsStats {
    pub total_blocks: u32,
    pub free_blocks: u32,
    pub total_inodes: u32,
    pub free_inodes: u32,
    pub block_size: u32,
}

impl FsStats {
    /// Get total size in bytes
    pub fn total_size(&self) -> u64 {
        self.total_blocks as u64 * self.block_size as u64
    }

    /// Get free size in bytes
    pub fn free_size(&self) -> u64 {
        self.free_blocks as u64 * self.block_size as u64
    }

    /// Get used size in bytes
    pub fn used_size(&self) -> u64 {
        self.total_size() - self.free_size()
    }
}
