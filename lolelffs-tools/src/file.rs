//! File operations for lolelffs

use crate::compress;
use crate::fs::LolelfFs;
use crate::types::*;
use anyhow::{bail, Result};

impl LolelfFs {
    /// Read file contents
    pub fn read_file(&mut self, inode_num: u32) -> Result<Vec<u8>> {
        let inode = self.read_inode(inode_num)?;

        if inode.is_dir() {
            bail!("Cannot read directory as file");
        }

        if inode.is_symlink() {
            // Return symlink target from i_data
            let target = inode
                .i_data
                .iter()
                .take_while(|&&b| b != 0)
                .copied()
                .collect();
            return Ok(target);
        }

        if inode.ei_block == 0 || inode.i_size == 0 {
            return Ok(Vec::new());
        }

        let ei = self.read_extent_index(&inode)?;
        let mut data = Vec::with_capacity(inode.i_size as usize);

        let num_blocks = inode.i_size.div_ceil(LOLELFFS_BLOCK_SIZE);

        for logical_block in 0..num_blocks {
            if let Some(extent) = ei.find_extent(logical_block) {
                if let Some(phys_block) = extent.get_physical(logical_block) {
                    let raw_block = self.read_block(phys_block)?;

                    // Step 1: Decrypt if needed (decrypt-then-decompress pipeline)
                    let decrypted_block = if extent.ee_enc_algo != LOLELFFS_ENC_NONE {
                        // Check if filesystem is unlocked
                        if !self.enc_unlocked {
                            bail!("Cannot read encrypted block: filesystem is locked");
                        }

                        crate::encrypt::decrypt_block(
                            extent.ee_enc_algo,
                            &self.enc_master_key,
                            logical_block as u64,
                            &raw_block,
                        )?
                    } else {
                        raw_block
                    };

                    // Step 2: Decompress if needed
                    let block = if extent.ee_comp_algo != LOLELFFS_COMP_NONE as u16 {
                        compress::decompress_block(
                            extent.ee_comp_algo as u8,
                            &decrypted_block,
                            LOLELFFS_BLOCK_SIZE as usize,
                        )?
                    } else {
                        decrypted_block
                    };

                    // Calculate how much data to read from this block
                    let block_start = logical_block * LOLELFFS_BLOCK_SIZE;
                    let block_end = (block_start + LOLELFFS_BLOCK_SIZE).min(inode.i_size);
                    let bytes_to_read = (block_end - block_start) as usize;

                    data.extend_from_slice(&block[..bytes_to_read]);
                }
            }
        }

        // Truncate to exact file size
        data.truncate(inode.i_size as usize);
        Ok(data)
    }

    /// Write data to a file
    pub fn write_file(&mut self, inode_num: u32, data: &[u8]) -> Result<()> {
        let mut inode = self.read_inode(inode_num)?;

        if inode.is_dir() {
            bail!("Cannot write to directory");
        }

        if inode.is_symlink() {
            bail!("Cannot write to symlink");
        }

        // Free existing blocks
        if inode.ei_block != 0 {
            let ei = self.read_extent_index(&inode)?;
            for extent in &ei.extents {
                if extent.is_empty() {
                    break;
                }
                self.free_blocks(extent.ee_start, extent.ee_len)?;
            }
        }

        // Handle empty file
        if data.is_empty() {
            if inode.ei_block != 0 {
                let ei = ExtentIndex {
                    nr_files: 0,
                    extents: vec![Extent::default(); LOLELFFS_MAX_EXTENTS],
                };
                self.write_extent_index(inode.ei_block, &ei)?;
            }

            inode.i_size = 0;
            inode.i_blocks = 0;
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_secs() as u32;
            inode.i_mtime = now;
            inode.i_ctime = now;
            self.write_inode(inode_num, &inode)?;
            return Ok(());
        }

        // Allocate extent index block if needed
        if inode.ei_block == 0 {
            let ei_block = self.alloc_blocks(1)?;
            inode.ei_block = ei_block;
        }

        // Calculate needed blocks
        let num_blocks = (data.len() as u32).div_ceil(LOLELFFS_BLOCK_SIZE);

        // Allocate blocks using extents
        let mut extents = Vec::new();
        let mut allocated = 0u32;
        let mut logical_block = 0u32;

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
                ee_block: logical_block,
                ee_len: extent_size,
                ee_start: start_block,
                ee_comp_algo: LOLELFFS_COMP_NONE as u16,
                ee_enc_algo: LOLELFFS_ENC_NONE,
                ee_reserved: 0,
                ee_flags: 0,
                ee_reserved2: 0,
                ee_meta: 0,
            });

            logical_block += extent_size;
            allocated += extent_size;
        }

        // Pad extents to LOLELFFS_MAX_EXTENTS
        while extents.len() < LOLELFFS_MAX_EXTENTS {
            extents.push(Extent::default());
        }

        // Write extent index
        let ei = ExtentIndex {
            nr_files: 0,
            extents,
        };
        self.write_extent_index(inode.ei_block, &ei)?;

        // Write data to blocks with optional compression and encryption
        let comp_algo = self.superblock.comp_default_algo as u8;
        let comp_enabled = self.superblock.comp_enabled != 0;
        let enc_algo = self.superblock.enc_default_algo as u8;
        let enc_enabled = self.superblock.enc_enabled != 0;
        let mut updated_extents = ei.extents.clone();

        for (idx, chunk) in data.chunks(LOLELFFS_BLOCK_SIZE as usize).enumerate() {
            let logical_block = idx as u32;

            if let Some((extent_idx, extent)) = ei.extents.iter().enumerate().find(|(_i, e)| {
                logical_block >= e.ee_block
                    && logical_block < e.ee_block + e.ee_len
                    && !e.is_empty()
            }) {
                if let Some(phys_block) = extent.get_physical(logical_block) {
                    // Prepare block data (pad to full block size)
                    let mut block = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
                    block[..chunk.len()].copy_from_slice(chunk);

                    // Step 1: Compress if enabled
                    let (work_buf, used_comp_algo) = if comp_enabled
                        && comp_algo != LOLELFFS_COMP_NONE
                        && chunk.len() == LOLELFFS_BLOCK_SIZE as usize
                    {
                        match crate::compress::compress_block(comp_algo, &block) {
                            Ok(Some(compressed)) => {
                                // Compression succeeded and saved space
                                let mut comp_block = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
                                comp_block[..compressed.len()].copy_from_slice(&compressed);
                                (comp_block, comp_algo)
                            }
                            _ => {
                                // Compression failed or didn't save space
                                (block.clone(), LOLELFFS_COMP_NONE)
                            }
                        }
                    } else {
                        (block.clone(), LOLELFFS_COMP_NONE)
                    };

                    // Step 2: Encrypt if enabled (compress-then-encrypt)
                    let (final_block, used_enc_algo) = if enc_enabled
                        && enc_algo != LOLELFFS_ENC_NONE
                    {
                        // Check if filesystem is unlocked
                        if !self.enc_unlocked {
                            bail!("Cannot write encrypted data: filesystem is locked");
                        }

                        match crate::encrypt::encrypt_block(
                            enc_algo,
                            &self.enc_master_key,
                            logical_block as u64,
                            &work_buf,
                        ) {
                            Ok(encrypted) => {
                                // For AES-XTS, encrypted size == block size
                                // For ChaCha20-Poly1305, add 16-byte tag
                                let mut enc_block = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
                                let copy_len = encrypted.len().min(LOLELFFS_BLOCK_SIZE as usize);
                                enc_block[..copy_len].copy_from_slice(&encrypted[..copy_len]);
                                (enc_block, enc_algo)
                            }
                            Err(e) => {
                                bail!("Encryption failed: {}", e);
                            }
                        }
                    } else {
                        (work_buf, LOLELFFS_ENC_NONE)
                    };

                    self.write_block(phys_block, &final_block)?;

                    // Update extent metadata
                    updated_extents[extent_idx].ee_comp_algo = used_comp_algo as u16;
                    updated_extents[extent_idx].ee_enc_algo = used_enc_algo;

                    // Set flags
                    let mut flags = 0u16;
                    if used_comp_algo != LOLELFFS_COMP_NONE {
                        flags |= LOLELFFS_EXT_COMPRESSED;
                    }
                    if used_enc_algo != LOLELFFS_ENC_NONE {
                        flags |= LOLELFFS_EXT_ENCRYPTED;
                    }
                    updated_extents[extent_idx].ee_flags = flags;
                }
            }
        }

        // Rewrite extent index with updated compression info
        let updated_ei = ExtentIndex {
            nr_files: 0,
            extents: updated_extents,
        };
        self.write_extent_index(inode.ei_block, &updated_ei)?;

        // Update inode
        inode.i_size = data.len() as u32;
        inode.i_blocks = num_blocks;
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as u32;
        inode.i_mtime = now;
        inode.i_ctime = now;
        self.write_inode(inode_num, &inode)?;

        Ok(())
    }

    /// Create a new regular file
    pub fn create_file(&mut self, parent_inode_num: u32, name: &str) -> Result<u32> {
        // Allocate new inode
        let new_inode_num = self.alloc_inode()?;

        // Allocate extent index block
        let ei_block = self.alloc_blocks(1)?;

        // Create the inode
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as u32;

        let new_inode = Inode {
            i_mode: mode::S_IFREG | 0o644,
            i_uid: 0,
            i_gid: 0,
            i_size: 0,
            i_ctime: now,
            i_atime: now,
            i_mtime: now,
            i_blocks: 0,
            i_nlink: 1,
            ei_block,
            xattr_block: 0, // No xattrs initially
            i_data: [0u8; 28],
        };
        self.write_inode(new_inode_num, &new_inode)?;

        // Initialize extent index block
        let ei = ExtentIndex {
            nr_files: 0,
            extents: vec![Extent::default(); LOLELFFS_MAX_EXTENTS],
        };
        self.write_extent_index(ei_block, &ei)?;

        // Add entry to parent directory
        if let Err(e) = self.add_dir_entry(parent_inode_num, name, new_inode_num) {
            // Rollback on failure
            self.free_inode(new_inode_num)?;
            self.free_blocks(ei_block, 1)?;
            return Err(e);
        }

        Ok(new_inode_num)
    }

    /// Remove a file (unlink)
    pub fn unlink(&mut self, parent_inode_num: u32, name: &str) -> Result<()> {
        // Look up the file
        let file_inode_num = self
            .lookup(parent_inode_num, name)?
            .ok_or_else(|| anyhow::anyhow!("File '{}' not found", name))?;

        let file_inode = self.read_inode(file_inode_num)?;

        if file_inode.is_dir() {
            bail!("Cannot unlink directory '{}', use rmdir instead", name);
        }

        // Remove from parent
        self.remove_dir_entry(parent_inode_num, name)?;

        // Decrement link count
        let mut file_inode = file_inode;
        file_inode.i_nlink = file_inode.i_nlink.saturating_sub(1);

        // If link count is 0, free the file's resources
        if file_inode.i_nlink == 0 {
            // Free data blocks
            if file_inode.ei_block != 0 {
                let ei = self.read_extent_index(&file_inode)?;
                for extent in &ei.extents {
                    if extent.is_empty() {
                        break;
                    }
                    self.free_blocks(extent.ee_start, extent.ee_len)?;
                }

                // Free extent index block
                self.free_blocks(file_inode.ei_block, 1)?;
            }

            // Free xattr blocks
            self.free_inode_xattrs(file_inode_num)?;

            // Free the inode
            self.free_inode(file_inode_num)?;
        } else {
            // Just update the link count
            self.write_inode(file_inode_num, &file_inode)?;
        }

        Ok(())
    }

    /// Create a symbolic link
    pub fn symlink(&mut self, parent_inode_num: u32, name: &str, target: &str) -> Result<u32> {
        if target.len() > 27 {
            bail!("Symlink target too long (max 27 bytes)");
        }

        // Allocate new inode
        let new_inode_num = self.alloc_inode()?;

        // Create the inode
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as u32;

        let mut i_data = [0u8; 28];
        i_data[..target.len()].copy_from_slice(target.as_bytes());

        let new_inode = Inode {
            i_mode: mode::S_IFLNK | 0o777,
            i_uid: 0,
            i_gid: 0,
            i_size: target.len() as u32,
            i_ctime: now,
            i_atime: now,
            i_mtime: now,
            i_blocks: 0,
            i_nlink: 1,
            ei_block: 0,    // Symlinks don't need extent index
            xattr_block: 0, // No xattrs initially
            i_data,
        };
        self.write_inode(new_inode_num, &new_inode)?;

        // Add entry to parent directory
        if let Err(e) = self.add_dir_entry(parent_inode_num, name, new_inode_num) {
            // Rollback on failure
            self.free_inode(new_inode_num)?;
            return Err(e);
        }

        Ok(new_inode_num)
    }

    /// Create a hard link
    pub fn link(&mut self, target_inode_num: u32, parent_inode_num: u32, name: &str) -> Result<()> {
        let mut target_inode = self.read_inode(target_inode_num)?;

        if target_inode.is_dir() {
            bail!("Cannot create hard link to directory");
        }

        // Increment link count
        target_inode.i_nlink += 1;
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as u32;
        target_inode.i_ctime = now;
        self.write_inode(target_inode_num, &target_inode)?;

        // Add entry to parent directory
        if let Err(e) = self.add_dir_entry(parent_inode_num, name, target_inode_num) {
            // Rollback link count on failure
            target_inode.i_nlink -= 1;
            self.write_inode(target_inode_num, &target_inode)?;
            return Err(e);
        }

        Ok(())
    }

    /// Truncate a file to specified size
    pub fn truncate(&mut self, inode_num: u32, size: u32) -> Result<()> {
        let inode = self.read_inode(inode_num)?;

        if inode.is_dir() {
            bail!("Cannot truncate directory");
        }

        if size == 0 {
            return self.write_file(inode_num, &[]);
        }

        if size >= inode.i_size {
            // Extending file - read current data and pad with zeros
            let mut data = self.read_file(inode_num)?;
            data.resize(size as usize, 0);
            self.write_file(inode_num, &data)
        } else {
            // Shrinking file - read and truncate
            let mut data = self.read_file(inode_num)?;
            data.truncate(size as usize);
            self.write_file(inode_num, &data)
        }
    }
}
