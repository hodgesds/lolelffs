//! Compression support for lolelffs
//!
//! Provides compression and decompression using LZ4, zlib, and zstd algorithms.
//! Matches the kernel module compression behavior.

use crate::types::*;
use anyhow::{bail, Result};
use flate2::write::{ZlibDecoder, ZlibEncoder};
use flate2::Compression;
use std::io::Write;

/// Compress a block using the specified algorithm
pub fn compress_block(algo: u8, data: &[u8]) -> Result<Option<Vec<u8>>> {
    if data.len() != LOLELFFS_BLOCK_SIZE as usize {
        bail!("Data must be exactly {} bytes", LOLELFFS_BLOCK_SIZE);
    }

    match algo {
        LOLELFFS_COMP_NONE => Ok(None),
        LOLELFFS_COMP_LZ4 => compress_lz4(data),
        LOLELFFS_COMP_ZLIB => compress_zlib(data),
        LOLELFFS_COMP_ZSTD => compress_zstd(data),
        _ => bail!("Unsupported compression algorithm: {}", algo),
    }
}

/// Decompress a block using the specified algorithm
pub fn decompress_block(algo: u8, compressed: &[u8], expected_size: usize) -> Result<Vec<u8>> {
    match algo {
        LOLELFFS_COMP_NONE => {
            if compressed.len() != expected_size {
                bail!(
                    "Uncompressed data size mismatch: {} != {}",
                    compressed.len(),
                    expected_size
                );
            }
            Ok(compressed.to_vec())
        }
        LOLELFFS_COMP_LZ4 => decompress_lz4(compressed, expected_size),
        LOLELFFS_COMP_ZLIB => decompress_zlib(compressed, expected_size),
        LOLELFFS_COMP_ZSTD => decompress_zstd(compressed, expected_size),
        _ => bail!("Unsupported compression algorithm: {}", algo),
    }
}

/// Compress using LZ4
fn compress_lz4(data: &[u8]) -> Result<Option<Vec<u8>>> {
    let compressed = lz4::block::compress(data, None, true)?;

    // Only use compression if it saves space
    if compressed.len() < data.len() {
        Ok(Some(compressed))
    } else {
        Ok(None)
    }
}

/// Decompress using LZ4
fn decompress_lz4(compressed: &[u8], expected_size: usize) -> Result<Vec<u8>> {
    let decompressed = lz4::block::decompress(compressed, Some(expected_size as i32))?;

    if decompressed.len() != expected_size {
        bail!(
            "Decompressed size mismatch: {} != {}",
            decompressed.len(),
            expected_size
        );
    }

    Ok(decompressed)
}

/// Compress using zlib
fn compress_zlib(data: &[u8]) -> Result<Option<Vec<u8>>> {
    let mut encoder = ZlibEncoder::new(Vec::new(), Compression::default());
    encoder.write_all(data)?;
    let compressed = encoder.finish()?;

    // Only use compression if it saves space
    if compressed.len() < data.len() {
        Ok(Some(compressed))
    } else {
        Ok(None)
    }
}

/// Decompress using zlib
fn decompress_zlib(compressed: &[u8], expected_size: usize) -> Result<Vec<u8>> {
    let mut decoder = ZlibDecoder::new(Vec::new());
    decoder.write_all(compressed)?;
    let decompressed = decoder.finish()?;

    if decompressed.len() != expected_size {
        bail!(
            "Decompressed size mismatch: {} != {}",
            decompressed.len(),
            expected_size
        );
    }

    Ok(decompressed)
}

/// Compress using zstd
fn compress_zstd(data: &[u8]) -> Result<Option<Vec<u8>>> {
    let compressed = zstd::encode_all(data, 3)?;

    // Only use compression if it saves space
    if compressed.len() < data.len() {
        Ok(Some(compressed))
    } else {
        Ok(None)
    }
}

/// Decompress using zstd
fn decompress_zstd(compressed: &[u8], expected_size: usize) -> Result<Vec<u8>> {
    let decompressed = zstd::decode_all(compressed)?;

    if decompressed.len() != expected_size {
        bail!(
            "Decompressed size mismatch: {} != {}",
            decompressed.len(),
            expected_size
        );
    }

    Ok(decompressed)
}

/// Get the name of a compression algorithm
pub fn get_algo_name(algo: u8) -> &'static str {
    match algo {
        LOLELFFS_COMP_NONE => "none",
        LOLELFFS_COMP_LZ4 => "lz4",
        LOLELFFS_COMP_ZLIB => "zlib",
        LOLELFFS_COMP_ZSTD => "zstd",
        _ => "unknown",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lz4_roundtrip() {
        let data = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
        let compressed = compress_block(LOLELFFS_COMP_LZ4, &data).unwrap();

        if let Some(comp_data) = compressed {
            let decompressed = decompress_block(LOLELFFS_COMP_LZ4, &comp_data, data.len()).unwrap();
            assert_eq!(data, decompressed);
        }
    }

    #[test]
    fn test_zlib_roundtrip() {
        let data = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
        let compressed = compress_block(LOLELFFS_COMP_ZLIB, &data).unwrap();

        if let Some(comp_data) = compressed {
            let decompressed = decompress_block(LOLELFFS_COMP_ZLIB, &comp_data, data.len()).unwrap();
            assert_eq!(data, decompressed);
        }
    }

    #[test]
    fn test_zstd_roundtrip() {
        let data = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];
        let compressed = compress_block(LOLELFFS_COMP_ZSTD, &data).unwrap();

        if let Some(comp_data) = compressed {
            let decompressed = decompress_block(LOLELFFS_COMP_ZSTD, &comp_data, data.len()).unwrap();
            assert_eq!(data, decompressed);
        }
    }
}
