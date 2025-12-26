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
    pub enc_unlocked: bool,
    pub enc_master_key: [u8; 32],
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

        if superblock.version != LOLELFFS_VERSION {
            bail!(
                "Unsupported filesystem version: expected {}, got {}",
                LOLELFFS_VERSION,
                superblock.version
            );
        }

        Ok(LolelfFs {
            file,
            superblock,
            enc_unlocked: false,
            enc_master_key: [0; 32],
        })
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

        if superblock.version != LOLELFFS_VERSION {
            bail!(
                "Unsupported filesystem version: expected {}, got {}",
                LOLELFFS_VERSION,
                superblock.version
            );
        }

        Ok(LolelfFs {
            file,
            superblock,
            enc_unlocked: false,
            enc_master_key: [0; 32],
        })
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
        let version = file.read_u32::<LittleEndian>()?;
        let comp_default_algo = file.read_u32::<LittleEndian>()?;
        let comp_enabled = file.read_u32::<LittleEndian>()?;
        let comp_min_block_size = file.read_u32::<LittleEndian>()?;
        let comp_features = file.read_u32::<LittleEndian>()?;
        let max_extent_blocks = file.read_u32::<LittleEndian>()?;
        let max_extent_blocks_large = file.read_u32::<LittleEndian>()?;
        let enc_enabled = file.read_u32::<LittleEndian>()?;
        let enc_default_algo = file.read_u32::<LittleEndian>()?;
        let enc_kdf_algo = file.read_u32::<LittleEndian>()?;
        let enc_kdf_iterations = file.read_u32::<LittleEndian>()?;
        let enc_kdf_memory = file.read_u32::<LittleEndian>()?;
        let enc_kdf_parallelism = file.read_u32::<LittleEndian>()?;
        let mut enc_salt = [0u8; 32];
        file.read_exact(&mut enc_salt)?;
        let mut enc_master_key = [0u8; 32];
        file.read_exact(&mut enc_master_key)?;
        let enc_features = file.read_u32::<LittleEndian>()?;
        let mut reserved = [0u32; 3];
        for item in &mut reserved {
            *item = file.read_u32::<LittleEndian>()?;
        }

        Ok(Superblock {
            magic,
            nr_blocks,
            nr_inodes,
            nr_istore_blocks,
            nr_ifree_blocks,
            nr_bfree_blocks,
            nr_free_inodes,
            nr_free_blocks,
            version,
            comp_default_algo,
            comp_enabled,
            comp_min_block_size,
            comp_features,
            max_extent_blocks,
            max_extent_blocks_large,
            enc_enabled,
            enc_default_algo,
            enc_kdf_algo,
            enc_kdf_iterations,
            enc_kdf_memory,
            enc_kdf_parallelism,
            enc_salt,
            enc_master_key,
            enc_features,
            reserved,
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
        self.file
            .write_u32::<LittleEndian>(self.superblock.version)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.comp_default_algo)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.comp_enabled)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.comp_min_block_size)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.comp_features)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.max_extent_blocks)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.max_extent_blocks_large)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.enc_enabled)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.enc_default_algo)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.enc_kdf_algo)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.enc_kdf_iterations)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.enc_kdf_memory)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.enc_kdf_parallelism)?;
        self.file.write_all(&self.superblock.enc_salt)?;
        self.file.write_all(&self.superblock.enc_master_key)?;
        self.file
            .write_u32::<LittleEndian>(self.superblock.enc_features)?;
        for &r in &self.superblock.reserved {
            self.file.write_u32::<LittleEndian>(r)?;
        }

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
        let xattr_block = cursor.read_u32::<LittleEndian>()?;

        let mut i_data = [0u8; 28];
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
            xattr_block,
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
        data.write_u32::<LittleEndian>(inode.xattr_block).unwrap();
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
    /// Create a new filesystem (without encryption)
    pub fn create<P: AsRef<Path>>(path: P, size: u64) -> Result<Self> {
        Self::create_with_encryption(path, size, None)
    }

    /// Create a new filesystem with optional encryption
    /// enc_config: Option<(password: String, algo: u8, iterations: u32)>
    pub fn create_with_encryption<P: AsRef<Path>>(
        path: P,
        size: u64,
        enc_config: Option<(String, u8, u32)>,
    ) -> Result<Self> {
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
        let nr_ifree_blocks = nr_inodes.div_ceil(LOLELFFS_BITS_PER_BLOCK);
        let nr_bfree_blocks = nr_blocks.div_ceil(LOLELFFS_BITS_PER_BLOCK);

        // Handle encryption configuration
        let (
            enc_enabled,
            enc_algo,
            enc_kdf_algo,
            enc_kdf_iterations,
            enc_salt,
            enc_master_key,
            master_key_plain,
        ) = if let Some((password, algo, iterations)) = enc_config {
            // Generate random salt and master key
            let salt = crate::encrypt::generate_salt();
            let master_key = crate::encrypt::generate_master_key();

            // Derive user key from password
            let user_key =
                crate::encrypt::derive_key_pbkdf2(password.as_bytes(), &salt, iterations);

            // Encrypt master key
            let encrypted_master_key = crate::encrypt::encrypt_master_key(&master_key, &user_key)?;

            (
                1,
                algo as u32,
                LOLELFFS_KDF_PBKDF2 as u32,
                iterations,
                salt,
                encrypted_master_key,
                master_key,
            )
        } else {
            (
                0,
                LOLELFFS_ENC_NONE as u32,
                LOLELFFS_KDF_ARGON2ID as u32,
                3,
                [0; 32],
                [0; 32],
                [0; 32],
            )
        };

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
            version: LOLELFFS_VERSION,
            comp_default_algo: LOLELFFS_COMP_LZ4 as u32,
            comp_enabled: 1, // Compression enabled by default
            comp_min_block_size: 128,
            comp_features: LOLELFFS_FEATURE_LARGE_EXTENTS,
            max_extent_blocks: LOLELFFS_MAX_BLOCKS_PER_EXTENT,
            max_extent_blocks_large: LOLELFFS_MAX_BLOCKS_PER_EXTENT_LARGE,
            enc_enabled,
            enc_default_algo: enc_algo,
            enc_kdf_algo,
            enc_kdf_iterations,
            enc_kdf_memory: 65536,  // Not used for PBKDF2
            enc_kdf_parallelism: 4, // Not used for PBKDF2
            enc_salt,
            enc_master_key,
            enc_features: 0,
            reserved: [0; 3],
        };

        let mut fs = LolelfFs {
            file,
            superblock,
            enc_unlocked: enc_enabled != 0, // If encrypted, start unlocked
            enc_master_key: master_key_plain,
        };

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
                self.write_block(ifree_start + i, &vec![0xFFu8; LOLELFFS_BLOCK_SIZE as usize])?;
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
            xattr_block: 0, // No xattrs on root initially
            i_data: [0u8; 28],
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

    /// Get an extended attribute value
    pub fn get_xattr(&mut self, inode_num: u32, name: &str) -> Result<Vec<u8>> {
        let inode = self.read_inode(inode_num)?;

        if inode.xattr_block == 0 {
            bail!("No extended attributes set on inode {}", inode_num);
        }

        let (namespace, base_name) = crate::xattr::parse_xattr_name(name)?;
        let index = crate::xattr::read_xattr_index(self, inode.xattr_block)?;
        let data = crate::xattr::read_xattr_data(self, &index)?;
        let entries = crate::xattr::parse_xattr_entries(&data)?;

        for entry in entries {
            if entry.name_index == namespace && entry.name == base_name {
                return Ok(entry.value);
            }
        }

        bail!("Extended attribute '{}' not found", name);
    }

    /// Set an extended attribute
    pub fn set_xattr(&mut self, inode_num: u32, name: &str, value: &[u8]) -> Result<()> {
        let mut inode = self.read_inode(inode_num)?;
        let (namespace, base_name) = crate::xattr::parse_xattr_name(name)?;

        // Read existing entries if any
        let mut entries = if inode.xattr_block != 0 {
            let index = crate::xattr::read_xattr_index(self, inode.xattr_block)?;
            let data = crate::xattr::read_xattr_data(self, &index)?;

            // Free old xattr data blocks
            for extent in &index.extents {
                if extent.is_empty() {
                    break;
                }
                self.free_blocks(extent.ee_start, extent.ee_len)?;
            }

            crate::xattr::parse_xattr_entries(&data)?
        } else {
            Vec::new()
        };

        // Update or add the entry
        let mut found = false;
        for entry in &mut entries {
            if entry.name_index == namespace && entry.name == base_name {
                entry.value = value.to_vec();
                entry.value_len = value.len() as u16;
                found = true;
                break;
            }
        }

        if !found {
            entries.push(XattrEntry {
                name_len: base_name.len() as u8,
                name_index: namespace,
                value_len: value.len() as u16,
                value_offset: 0,
                name: base_name,
                value: value.to_vec(),
            });
        }

        // Serialize entries
        let data = crate::xattr::serialize_xattr_entries(&entries)?;

        // Allocate extent index block if needed
        if inode.xattr_block == 0 {
            inode.xattr_block = self.alloc_blocks(1)?;
        }

        // Calculate number of blocks needed
        let num_blocks = (data.len() as u32).div_ceil(LOLELFFS_BLOCK_SIZE);

        // Allocate blocks using extents
        let mut extents = Vec::new();
        let mut allocated = 0u32;

        while allocated < num_blocks {
            let remaining = num_blocks - allocated;

            // Determine if we need metadata for this extent
            let needs_metadata = false; // Currently always false - no per-block metadata

            let max_extent_size = if needs_metadata {
                LOLELFFS_MAX_BLOCKS_PER_EXTENT
            } else {
                let large = self.superblock.max_extent_blocks_large;
                if large == 0 || large > LOLELFFS_MAX_BLOCKS_PER_EXTENT_LARGE {
                    LOLELFFS_MAX_BLOCKS_PER_EXTENT_LARGE
                } else {
                    large
                }
            };

            let extent_size = self
                .calc_optimal_extent_size(allocated, needs_metadata)
                .min(remaining)
                .min(max_extent_size);

            let start_block = self.alloc_blocks(extent_size)?;

            extents.push(Extent {
                ee_block: allocated,
                ee_len: extent_size,
                ee_start: start_block,
                ee_comp_algo: LOLELFFS_COMP_NONE as u16,
                ee_enc_algo: LOLELFFS_ENC_NONE,
                ee_reserved: 0,
                ee_flags: 0,
                ee_reserved2: 0,
                ee_meta: 0,
            });

            allocated += extent_size;
        }

        // Pad extents to LOLELFFS_MAX_EXTENTS
        while extents.len() < LOLELFFS_MAX_EXTENTS {
            extents.push(Extent::default());
        }

        // Write xattr index
        let index = XattrIndex {
            total_size: data.len() as u32,
            count: entries.len() as u32,
            extents,
        };
        crate::xattr::write_xattr_index(self, inode.xattr_block, &index)?;

        // Write data to blocks
        for (idx, chunk) in data.chunks(LOLELFFS_BLOCK_SIZE as usize).enumerate() {
            let logical_block = idx as u32;

            if let Some(extent) = index.extents.iter().find(|e| {
                !e.is_empty()
                    && logical_block >= e.ee_block
                    && logical_block < e.ee_block + e.ee_len
            }) {
                let phys_block = extent.ee_start + (logical_block - extent.ee_block);
                let mut block = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
                block[..chunk.len()].copy_from_slice(chunk);
                self.write_block(phys_block, &block)?;
            }
        }

        // Update inode
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as u32;
        inode.i_ctime = now;
        self.write_inode(inode_num, &inode)?;

        Ok(())
    }

    /// List all extended attribute names
    pub fn list_xattrs(&mut self, inode_num: u32) -> Result<Vec<String>> {
        let inode = self.read_inode(inode_num)?;

        if inode.xattr_block == 0 {
            return Ok(Vec::new());
        }

        let index = crate::xattr::read_xattr_index(self, inode.xattr_block)?;
        let data = crate::xattr::read_xattr_data(self, &index)?;
        let entries = crate::xattr::parse_xattr_entries(&data)?;

        let names = entries
            .iter()
            .map(|e| {
                let prefix = match e.name_index {
                    XattrNamespace::User => "user.",
                    XattrNamespace::Trusted => "trusted.",
                    XattrNamespace::System => "system.",
                    XattrNamespace::Security => "security.",
                };
                format!("{}{}", prefix, e.name)
            })
            .collect();

        Ok(names)
    }

    /// Remove an extended attribute
    pub fn remove_xattr(&mut self, inode_num: u32, name: &str) -> Result<()> {
        let mut inode = self.read_inode(inode_num)?;

        if inode.xattr_block == 0 {
            bail!("No extended attributes set on inode {}", inode_num);
        }

        let (namespace, base_name) = crate::xattr::parse_xattr_name(name)?;
        let index = crate::xattr::read_xattr_index(self, inode.xattr_block)?;
        let data = crate::xattr::read_xattr_data(self, &index)?;
        let mut entries = crate::xattr::parse_xattr_entries(&data)?;

        // Find and remove the entry
        let initial_len = entries.len();
        entries.retain(|e| !(e.name_index == namespace && e.name == base_name));

        if entries.len() == initial_len {
            bail!("Extended attribute '{}' not found", name);
        }

        // Free old xattr data blocks
        for extent in &index.extents {
            if extent.is_empty() {
                break;
            }
            self.free_blocks(extent.ee_start, extent.ee_len)?;
        }

        // If no entries left, free the xattr block
        if entries.is_empty() {
            self.free_blocks(inode.xattr_block, 1)?;
            inode.xattr_block = 0;
        } else {
            // Serialize remaining entries and write them back
            let data = crate::xattr::serialize_xattr_entries(&entries)?;
            let num_blocks = (data.len() as u32).div_ceil(LOLELFFS_BLOCK_SIZE);

            // Allocate blocks using extents
            let mut extents = Vec::new();
            let mut allocated = 0u32;

            while allocated < num_blocks {
                let remaining = num_blocks - allocated;

                // Determine if we need metadata for this extent
                let needs_metadata = false; // Currently always false - no per-block metadata

                let max_extent_size = if needs_metadata {
                    LOLELFFS_MAX_BLOCKS_PER_EXTENT
                } else {
                    let large = self.superblock.max_extent_blocks_large;
                    if large == 0 || large > LOLELFFS_MAX_BLOCKS_PER_EXTENT_LARGE {
                        LOLELFFS_MAX_BLOCKS_PER_EXTENT_LARGE
                    } else {
                        large
                    }
                };

                let extent_size = self
                    .calc_optimal_extent_size(allocated, needs_metadata)
                    .min(remaining)
                    .min(max_extent_size);

                let start_block = self.alloc_blocks(extent_size)?;

                extents.push(Extent {
                    ee_block: allocated,
                    ee_len: extent_size,
                    ee_start: start_block,
                    ee_comp_algo: LOLELFFS_COMP_NONE as u16,
                    ee_enc_algo: LOLELFFS_ENC_NONE,
                    ee_reserved: 0,
                    ee_flags: 0,
                    ee_reserved2: 0,
                    ee_meta: 0,
                });

                allocated += extent_size;
            }

            // Pad extents
            while extents.len() < LOLELFFS_MAX_EXTENTS {
                extents.push(Extent::default());
            }

            // Write xattr index
            let new_index = XattrIndex {
                total_size: data.len() as u32,
                count: entries.len() as u32,
                extents,
            };
            crate::xattr::write_xattr_index(self, inode.xattr_block, &new_index)?;

            // Write data to blocks
            for (idx, chunk) in data.chunks(LOLELFFS_BLOCK_SIZE as usize).enumerate() {
                let logical_block = idx as u32;

                if let Some(extent) = new_index.extents.iter().find(|e| {
                    !e.is_empty()
                        && logical_block >= e.ee_block
                        && logical_block < e.ee_block + e.ee_len
                }) {
                    let phys_block = extent.ee_start + (logical_block - extent.ee_block);
                    let mut block = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
                    block[..chunk.len()].copy_from_slice(chunk);
                    self.write_block(phys_block, &block)?;
                }
            }
        }

        // Update inode
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as u32;
        inode.i_ctime = now;
        self.write_inode(inode_num, &inode)?;

        Ok(())
    }

    /// Free xattr blocks for an inode (called during inode deletion)
    pub fn free_inode_xattrs(&mut self, inode_num: u32) -> Result<()> {
        let inode = self.read_inode(inode_num)?;

        if inode.xattr_block == 0 {
            return Ok(());
        }

        // Read xattr index
        let index = crate::xattr::read_xattr_index(self, inode.xattr_block)?;

        // Free all xattr data blocks
        for extent in &index.extents {
            if extent.is_empty() {
                break;
            }
            self.free_blocks(extent.ee_start, extent.ee_len)?;
        }

        // Free xattr index block
        self.free_blocks(inode.xattr_block, 1)?;

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

    /// Unlock encrypted filesystem with password
    pub fn unlock(&mut self, password: &str) -> Result<()> {
        // Check if encryption is enabled
        if self.superblock.enc_enabled == 0 {
            bail!("Filesystem is not encrypted");
        }

        // Check if already unlocked
        if self.enc_unlocked {
            return Ok(());
        }

        // Derive user key from password using the same parameters as creation
        let user_key = crate::encrypt::derive_key_pbkdf2(
            password.as_bytes(),
            &self.superblock.enc_salt,
            self.superblock.enc_kdf_iterations,
        );

        // Decrypt master key
        let master_key =
            crate::encrypt::decrypt_master_key(&self.superblock.enc_master_key, &user_key)?;

        // Store the decrypted master key
        self.enc_master_key = master_key;
        self.enc_unlocked = true;

        Ok(())
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
