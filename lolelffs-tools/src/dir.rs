//! Directory operations for lolelffs

use crate::fs::LolelfFs;
use crate::types::*;
use anyhow::{bail, Result};

/// Directory entry with full information
#[derive(Debug, Clone)]
pub struct DirEntry {
    pub inode_num: u32,
    pub filename: String,
    pub inode: Inode,
}

impl LolelfFs {
    /// List all entries in a directory
    pub fn list_dir(&mut self, dir_inode_num: u32) -> Result<Vec<DirEntry>> {
        let dir_inode = self.read_inode(dir_inode_num)?;

        if !dir_inode.is_dir() {
            bail!("Inode {} is not a directory", dir_inode_num);
        }

        if dir_inode.ei_block == 0 {
            return Ok(Vec::new());
        }

        let ei = self.read_extent_index(&dir_inode)?;
        let mut entries = Vec::new();

        // Iterate through all extents
        for extent in &ei.extents {
            if extent.is_empty() {
                break;
            }

            // Iterate through all blocks in extent
            for block_offset in 0..extent.ee_len {
                let block_num = extent.ee_start + block_offset;
                let block = self.read_block(block_num)?;

                // Iterate through all file entries in block
                for file_idx in 0..LOLELFFS_FILES_PER_BLOCK {
                    let offset = file_idx * FileEntry::SIZE;
                    let entry_data = &block[offset..offset + FileEntry::SIZE];

                    if let Some(entry) = FileEntry::from_bytes(entry_data) {
                        let inode = self.read_inode(entry.inode)?;
                        entries.push(DirEntry {
                            inode_num: entry.inode,
                            filename: entry.filename,
                            inode,
                        });
                    }
                }
            }
        }

        Ok(entries)
    }

    /// Look up a file in a directory by name
    pub fn lookup(&mut self, dir_inode_num: u32, name: &str) -> Result<Option<u32>> {
        let dir_inode = self.read_inode(dir_inode_num)?;

        if !dir_inode.is_dir() {
            bail!("Inode {} is not a directory", dir_inode_num);
        }

        if dir_inode.ei_block == 0 {
            return Ok(None);
        }

        let ei = self.read_extent_index(&dir_inode)?;

        // Search through all extents
        for extent in &ei.extents {
            if extent.is_empty() {
                break;
            }

            for block_offset in 0..extent.ee_len {
                let block_num = extent.ee_start + block_offset;
                let block = self.read_block(block_num)?;

                for file_idx in 0..LOLELFFS_FILES_PER_BLOCK {
                    let offset = file_idx * FileEntry::SIZE;
                    let entry_data = &block[offset..offset + FileEntry::SIZE];

                    if let Some(entry) = FileEntry::from_bytes(entry_data) {
                        if entry.filename == name {
                            return Ok(Some(entry.inode));
                        }
                    }
                }
            }
        }

        Ok(None)
    }

    /// Resolve a path to an inode number
    pub fn resolve_path(&mut self, path: &str) -> Result<u32> {
        let path = path.trim_matches('/');

        if path.is_empty() {
            return Ok(LOLELFFS_ROOT_INO);
        }

        let mut current_inode = LOLELFFS_ROOT_INO;

        for component in path.split('/') {
            if component.is_empty() || component == "." {
                continue;
            }

            if component == ".." {
                // For now, don't support parent directory traversal
                // This would require tracking parent inodes
                bail!("Parent directory traversal not supported");
            }

            match self.lookup(current_inode, component)? {
                Some(inode) => current_inode = inode,
                None => bail!("Path not found: {}", path),
            }
        }

        Ok(current_inode)
    }

    /// Add a file entry to a directory
    pub fn add_dir_entry(
        &mut self,
        dir_inode_num: u32,
        filename: &str,
        file_inode_num: u32,
    ) -> Result<()> {
        if filename.len() > LOLELFFS_MAX_FILENAME - 1 {
            bail!("Filename too long (max {} bytes)", LOLELFFS_MAX_FILENAME - 1);
        }

        let mut dir_inode = self.read_inode(dir_inode_num)?;

        if !dir_inode.is_dir() {
            bail!("Inode {} is not a directory", dir_inode_num);
        }

        // Check if file already exists
        if self.lookup(dir_inode_num, filename)?.is_some() {
            bail!("File '{}' already exists", filename);
        }

        let mut ei = if dir_inode.ei_block == 0 {
            // Allocate extent index block for new directory
            let ei_block = self.alloc_blocks(1)?;
            dir_inode.ei_block = ei_block;
            ExtentIndex {
                nr_files: 0,
                extents: vec![Extent::default(); LOLELFFS_MAX_EXTENTS],
            }
        } else {
            self.read_extent_index(&dir_inode)?
        };

        // Find a slot for the new entry
        let mut slot_found = false;
        let mut target_block = 0u32;
        let mut target_offset = 0usize;

        // Search for empty slot in existing blocks
        for (ext_idx, extent) in ei.extents.iter().enumerate() {
            if extent.is_empty() {
                if ext_idx == 0 || ei.extents[ext_idx - 1].is_empty() {
                    // Need to allocate first block
                    break;
                }
            }

            for block_offset in 0..extent.ee_len {
                let block_num = extent.ee_start + block_offset;
                let block = self.read_block(block_num)?;

                for file_idx in 0..LOLELFFS_FILES_PER_BLOCK {
                    let offset = file_idx * FileEntry::SIZE;
                    let entry_data = &block[offset..offset + FileEntry::SIZE];

                    // Check if slot is empty (inode 0 and no filename)
                    if entry_data[0..4] == [0, 0, 0, 0] && entry_data[4] == 0 {
                        target_block = block_num;
                        target_offset = offset;
                        slot_found = true;
                        break;
                    }
                }

                if slot_found {
                    break;
                }
            }

            if slot_found {
                break;
            }
        }

        // If no slot found, need to allocate new block
        if !slot_found {
            // Find extent with space or create new extent
            let mut extent_idx = None;
            let mut next_logical = 0u32;

            for (idx, extent) in ei.extents.iter().enumerate() {
                if extent.is_empty() {
                    extent_idx = Some(idx);
                    break;
                }
                next_logical = extent.ee_block + extent.ee_len;
            }

            let extent_idx = extent_idx.ok_or_else(|| anyhow::anyhow!("Directory full"))?;

            // Allocate a new block
            let new_block = self.alloc_blocks(1)?;

            // Update extent
            ei.extents[extent_idx] = Extent {
                ee_block: next_logical,
                ee_len: 1,
                ee_start: new_block,
            };

            // Initialize the new block
            let empty_block = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
            self.write_block(new_block, &empty_block)?;

            target_block = new_block;
            target_offset = 0;

            dir_inode.i_blocks += 1;
        }

        // Write the directory entry
        let entry = FileEntry {
            inode: file_inode_num,
            filename: filename.to_string(),
        };
        let entry_data = entry.to_bytes();

        let mut block = self.read_block(target_block)?;
        block[target_offset..target_offset + FileEntry::SIZE].copy_from_slice(&entry_data);
        self.write_block(target_block, &block)?;

        // Update extent index
        ei.nr_files += 1;
        self.write_extent_index(dir_inode.ei_block, &ei)?;

        // Update directory inode
        dir_inode.i_size += FileEntry::SIZE as u32;
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as u32;
        dir_inode.i_mtime = now;
        dir_inode.i_ctime = now;
        self.write_inode(dir_inode_num, &dir_inode)?;

        Ok(())
    }

    /// Remove a file entry from a directory
    pub fn remove_dir_entry(&mut self, dir_inode_num: u32, filename: &str) -> Result<u32> {
        let mut dir_inode = self.read_inode(dir_inode_num)?;

        if !dir_inode.is_dir() {
            bail!("Inode {} is not a directory", dir_inode_num);
        }

        if dir_inode.ei_block == 0 {
            bail!("File '{}' not found", filename);
        }

        let mut ei = self.read_extent_index(&dir_inode)?;
        let mut removed_inode = None;

        // Search for the entry
        'outer: for extent in &ei.extents {
            if extent.is_empty() {
                break;
            }

            for block_offset in 0..extent.ee_len {
                let block_num = extent.ee_start + block_offset;
                let mut block = self.read_block(block_num)?;

                for file_idx in 0..LOLELFFS_FILES_PER_BLOCK {
                    let offset = file_idx * FileEntry::SIZE;
                    let entry_data = &block[offset..offset + FileEntry::SIZE];

                    if let Some(entry) = FileEntry::from_bytes(entry_data) {
                        if entry.filename == filename {
                            removed_inode = Some(entry.inode);

                            // Clear the entry
                            for byte in &mut block[offset..offset + FileEntry::SIZE] {
                                *byte = 0;
                            }
                            self.write_block(block_num, &block)?;

                            break 'outer;
                        }
                    }
                }
            }
        }

        let removed_inode = removed_inode.ok_or_else(|| anyhow::anyhow!("File '{}' not found", filename))?;

        // Update extent index
        ei.nr_files = ei.nr_files.saturating_sub(1);
        self.write_extent_index(dir_inode.ei_block, &ei)?;

        // Update directory inode
        dir_inode.i_size = dir_inode.i_size.saturating_sub(FileEntry::SIZE as u32);
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as u32;
        dir_inode.i_mtime = now;
        dir_inode.i_ctime = now;
        self.write_inode(dir_inode_num, &dir_inode)?;

        Ok(removed_inode)
    }

    /// Create a new directory
    pub fn mkdir(&mut self, parent_inode_num: u32, name: &str) -> Result<u32> {
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
            i_mode: mode::S_IFDIR | 0o755,
            i_uid: 0,
            i_gid: 0,
            i_size: 0,
            i_ctime: now,
            i_atime: now,
            i_mtime: now,
            i_blocks: 0,
            i_nlink: 2, // . and parent's link
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

        // Increment parent's link count
        let mut parent_inode = self.read_inode(parent_inode_num)?;
        parent_inode.i_nlink += 1;
        self.write_inode(parent_inode_num, &parent_inode)?;

        Ok(new_inode_num)
    }

    /// Remove a directory (must be empty)
    pub fn rmdir(&mut self, parent_inode_num: u32, name: &str) -> Result<()> {
        // Look up the directory
        let dir_inode_num = self
            .lookup(parent_inode_num, name)?
            .ok_or_else(|| anyhow::anyhow!("Directory '{}' not found", name))?;

        let dir_inode = self.read_inode(dir_inode_num)?;

        if !dir_inode.is_dir() {
            bail!("'{}' is not a directory", name);
        }

        // Check if directory is empty
        let entries = self.list_dir(dir_inode_num)?;
        if !entries.is_empty() {
            bail!("Directory '{}' is not empty", name);
        }

        // Remove from parent
        self.remove_dir_entry(parent_inode_num, name)?;

        // Free extent index block
        if dir_inode.ei_block != 0 {
            self.free_blocks(dir_inode.ei_block, 1)?;
        }

        // Free any data blocks
        if dir_inode.ei_block != 0 {
            let ei = self.read_extent_index(&dir_inode)?;
            for extent in &ei.extents {
                if extent.is_empty() {
                    break;
                }
                self.free_blocks(extent.ee_start, extent.ee_len)?;
            }
        }

        // Free the inode
        self.free_inode(dir_inode_num)?;

        // Decrement parent's link count
        let mut parent_inode = self.read_inode(parent_inode_num)?;
        parent_inode.i_nlink = parent_inode.i_nlink.saturating_sub(1);
        self.write_inode(parent_inode_num, &parent_inode)?;

        Ok(())
    }
}
