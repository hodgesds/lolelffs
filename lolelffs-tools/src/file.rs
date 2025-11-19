//! File operations for lolelffs

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

        let num_blocks = (inode.i_size + LOLELFFS_BLOCK_SIZE - 1) / LOLELFFS_BLOCK_SIZE;

        for logical_block in 0..num_blocks {
            if let Some(extent) = ei.find_extent(logical_block) {
                if let Some(phys_block) = extent.get_physical(logical_block) {
                    let block = self.read_block(phys_block)?;

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
        let num_blocks = (data.len() as u32 + LOLELFFS_BLOCK_SIZE - 1) / LOLELFFS_BLOCK_SIZE;

        // Allocate blocks using extents
        let mut extents = Vec::new();
        let mut allocated = 0u32;
        let mut logical_block = 0u32;

        while allocated < num_blocks {
            let remaining = num_blocks - allocated;
            let extent_size = self
                .calc_optimal_extent_size(allocated)
                .min(remaining)
                .min(LOLELFFS_MAX_BLOCKS_PER_EXTENT);

            let start_block = self.alloc_blocks(extent_size)?;

            extents.push(Extent {
                ee_block: logical_block,
                ee_len: extent_size,
                ee_start: start_block,
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

        // Write data to blocks
        for (idx, chunk) in data.chunks(LOLELFFS_BLOCK_SIZE as usize).enumerate() {
            let logical_block = idx as u32;

            if let Some(extent) = ei.find_extent(logical_block) {
                if let Some(phys_block) = extent.get_physical(logical_block) {
                    let mut block = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
                    block[..chunk.len()].copy_from_slice(chunk);
                    self.write_block(phys_block, &block)?;
                }
            }
        }

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
            i_data: [0u8; 32],
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
        if target.len() > 31 {
            bail!("Symlink target too long (max 31 bytes)");
        }

        // Allocate new inode
        let new_inode_num = self.alloc_inode()?;

        // Create the inode
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as u32;

        let mut i_data = [0u8; 32];
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
            ei_block: 0, // Symlinks don't need extent index
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
