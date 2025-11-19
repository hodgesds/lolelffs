//! Bitmap operations for inode and block allocation

use crate::fs::LolelfFs;
use crate::types::*;
use anyhow::{bail, Result};

impl LolelfFs {
    /// Allocate a free inode
    pub fn alloc_inode(&mut self) -> Result<u32> {
        if self.superblock.nr_free_inodes == 0 {
            bail!("No free inodes available");
        }

        let ifree_start = self.superblock.ifree_bitmap_start();

        for block_idx in 0..self.superblock.nr_ifree_blocks {
            let mut block = self.read_block(ifree_start + block_idx)?;

            for byte_idx in 0..LOLELFFS_BLOCK_SIZE as usize {
                if block[byte_idx] != 0 {
                    // Find the first set bit
                    for bit_idx in 0..8 {
                        if block[byte_idx] & (1 << bit_idx) != 0 {
                            let inode_num = block_idx * LOLELFFS_BITS_PER_BLOCK
                                + byte_idx as u32 * 8
                                + bit_idx;

                            if inode_num >= self.superblock.nr_inodes {
                                continue;
                            }

                            // Clear the bit
                            block[byte_idx] &= !(1 << bit_idx);
                            self.write_block(ifree_start + block_idx, &block)?;

                            // Update superblock
                            self.superblock.nr_free_inodes -= 1;
                            self.write_superblock()?;

                            return Ok(inode_num);
                        }
                    }
                }
            }
        }

        bail!("No free inodes found in bitmap");
    }

    /// Free an inode
    pub fn free_inode(&mut self, inode_num: u32) -> Result<()> {
        if inode_num >= self.superblock.nr_inodes {
            bail!("Invalid inode number {}", inode_num);
        }

        let ifree_start = self.superblock.ifree_bitmap_start();
        let block_idx = inode_num / LOLELFFS_BITS_PER_BLOCK;
        let bit_idx = inode_num % LOLELFFS_BITS_PER_BLOCK;
        let byte_idx = (bit_idx / 8) as usize;
        let bit_offset = bit_idx % 8;

        let mut block = self.read_block(ifree_start + block_idx)?;

        // Set the bit
        block[byte_idx] |= 1 << bit_offset;
        self.write_block(ifree_start + block_idx, &block)?;

        // Update superblock
        self.superblock.nr_free_inodes += 1;
        self.write_superblock()?;

        Ok(())
    }

    /// Allocate consecutive free blocks
    pub fn alloc_blocks(&mut self, count: u32) -> Result<u32> {
        if count == 0 {
            bail!("Cannot allocate 0 blocks");
        }

        if count > self.superblock.nr_free_blocks {
            bail!(
                "Not enough free blocks: need {}, have {}",
                count,
                self.superblock.nr_free_blocks
            );
        }

        let bfree_start = self.superblock.bfree_bitmap_start();
        let data_start = self.superblock.data_block_start();

        // Search for consecutive free blocks
        let mut start_block = None;
        let mut consecutive = 0u32;

        'outer: for block_num in data_start..self.superblock.nr_blocks {
            let block_idx = block_num / LOLELFFS_BITS_PER_BLOCK;
            let bit_idx = block_num % LOLELFFS_BITS_PER_BLOCK;
            let byte_idx = (bit_idx / 8) as usize;
            let bit_offset = bit_idx % 8;

            let block = self.read_block(bfree_start + block_idx)?;

            if block[byte_idx] & (1 << bit_offset) != 0 {
                // Block is free
                if consecutive == 0 {
                    start_block = Some(block_num);
                }
                consecutive += 1;

                if consecutive >= count {
                    break 'outer;
                }
            } else {
                // Block is used, reset
                start_block = None;
                consecutive = 0;
            }
        }

        if consecutive < count {
            bail!(
                "Could not find {} consecutive free blocks",
                count
            );
        }

        let start = start_block.unwrap();

        // Mark the blocks as used
        for i in 0..count {
            let block_num = start + i;
            let block_idx = block_num / LOLELFFS_BITS_PER_BLOCK;
            let bit_idx = block_num % LOLELFFS_BITS_PER_BLOCK;
            let byte_idx = (bit_idx / 8) as usize;
            let bit_offset = bit_idx % 8;

            let mut block = self.read_block(bfree_start + block_idx)?;
            block[byte_idx] &= !(1 << bit_offset);
            self.write_block(bfree_start + block_idx, &block)?;
        }

        // Update superblock
        self.superblock.nr_free_blocks -= count;
        self.write_superblock()?;

        Ok(start)
    }

    /// Free blocks
    pub fn free_blocks(&mut self, start: u32, count: u32) -> Result<()> {
        if count == 0 {
            return Ok(());
        }

        let bfree_start = self.superblock.bfree_bitmap_start();

        for i in 0..count {
            let block_num = start + i;
            if block_num >= self.superblock.nr_blocks {
                bail!("Invalid block number {}", block_num);
            }

            let block_idx = block_num / LOLELFFS_BITS_PER_BLOCK;
            let bit_idx = block_num % LOLELFFS_BITS_PER_BLOCK;
            let byte_idx = (bit_idx / 8) as usize;
            let bit_offset = bit_idx % 8;

            let mut block = self.read_block(bfree_start + block_idx)?;
            block[byte_idx] |= 1 << bit_offset;
            self.write_block(bfree_start + block_idx, &block)?;
        }

        // Update superblock
        self.superblock.nr_free_blocks += count;
        self.write_superblock()?;

        Ok(())
    }

    /// Check if a block is free
    pub fn is_block_free(&mut self, block_num: u32) -> Result<bool> {
        if block_num >= self.superblock.nr_blocks {
            bail!("Invalid block number {}", block_num);
        }

        let bfree_start = self.superblock.bfree_bitmap_start();
        let block_idx = block_num / LOLELFFS_BITS_PER_BLOCK;
        let bit_idx = block_num % LOLELFFS_BITS_PER_BLOCK;
        let byte_idx = (bit_idx / 8) as usize;
        let bit_offset = bit_idx % 8;

        let block = self.read_block(bfree_start + block_idx)?;
        Ok(block[byte_idx] & (1 << bit_offset) != 0)
    }

    /// Check if an inode is free
    pub fn is_inode_free(&mut self, inode_num: u32) -> Result<bool> {
        if inode_num >= self.superblock.nr_inodes {
            bail!("Invalid inode number {}", inode_num);
        }

        let ifree_start = self.superblock.ifree_bitmap_start();
        let block_idx = inode_num / LOLELFFS_BITS_PER_BLOCK;
        let bit_idx = inode_num % LOLELFFS_BITS_PER_BLOCK;
        let byte_idx = (bit_idx / 8) as usize;
        let bit_offset = bit_idx % 8;

        let block = self.read_block(ifree_start + block_idx)?;
        Ok(block[byte_idx] & (1 << bit_offset) != 0)
    }

    /// Calculate optimal extent size based on file size
    pub fn calc_optimal_extent_size(&self, current_blocks: u32) -> u32 {
        if current_blocks < 8 {
            2
        } else if current_blocks < 32 {
            4
        } else {
            LOLELFFS_MAX_BLOCKS_PER_EXTENT
        }
    }
}
