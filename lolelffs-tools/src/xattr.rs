//! Extended attribute operations for lolelffs

use crate::fs::LolelfFs;
use crate::types::*;
use anyhow::{bail, Result};

/// Parse xattr name to extract namespace and base name
pub fn parse_xattr_name(name: &str) -> Result<(XattrNamespace, String)> {
    if let Some(base) = name.strip_prefix("user.") {
        Ok((XattrNamespace::User, base.to_string()))
    } else if let Some(base) = name.strip_prefix("trusted.") {
        Ok((XattrNamespace::Trusted, base.to_string()))
    } else if let Some(base) = name.strip_prefix("system.") {
        Ok((XattrNamespace::System, base.to_string()))
    } else if let Some(base) = name.strip_prefix("security.") {
        Ok((XattrNamespace::Security, base.to_string()))
    } else {
        bail!("Invalid xattr name '{}': must start with user., trusted., system., or security.", name);
    }
}

/// Read xattr extent index block
pub fn read_xattr_index(fs: &mut LolelfFs, block_num: u32) -> Result<XattrIndex> {
    let block = fs.read_block(block_num)?;

    let total_size = u32::from_le_bytes([block[0], block[1], block[2], block[3]]);
    let count = u32::from_le_bytes([block[4], block[5], block[6], block[7]]);

    let mut extents = Vec::new();
    let mut offset = 8;

    for _ in 0..LOLELFFS_MAX_EXTENTS {
        if offset + 20 > block.len() {
            break;
        }

        let ee_block = u32::from_le_bytes([
            block[offset],
            block[offset + 1],
            block[offset + 2],
            block[offset + 3],
        ]);
        let ee_len = u32::from_le_bytes([
            block[offset + 4],
            block[offset + 5],
            block[offset + 6],
            block[offset + 7],
        ]);
        let ee_start = u32::from_le_bytes([
            block[offset + 8],
            block[offset + 9],
            block[offset + 10],
            block[offset + 11],
        ]);
        let ee_comp_algo = u16::from_le_bytes([
            block[offset + 12],
            block[offset + 13],
        ]);
        let ee_enc_algo = block[offset + 14];
        let ee_reserved = block[offset + 15];
        let ee_flags = u16::from_le_bytes([
            block[offset + 16],
            block[offset + 17],
        ]);
        let ee_reserved2 = u16::from_le_bytes([
            block[offset + 18],
            block[offset + 19],
        ]);
        let ee_meta = u32::from_le_bytes([
            block[offset + 20],
            block[offset + 21],
            block[offset + 22],
            block[offset + 23],
        ]);

        extents.push(Extent {
            ee_block,
            ee_len,
            ee_start,
            ee_comp_algo,
            ee_enc_algo,
            ee_reserved,
            ee_flags,
            ee_reserved2,
            ee_meta,
        });

        offset += 24;
    }

    Ok(XattrIndex {
        total_size,
        count,
        extents,
    })
}

/// Write xattr extent index block
pub fn write_xattr_index(fs: &mut LolelfFs, block_num: u32, index: &XattrIndex) -> Result<()> {
    let mut block = vec![0u8; LOLELFFS_BLOCK_SIZE as usize];

    // Write total_size and count
    block[0..4].copy_from_slice(&index.total_size.to_le_bytes());
    block[4..8].copy_from_slice(&index.count.to_le_bytes());

    // Write extents
    let mut offset = 8;
    for extent in &index.extents {
        if offset + 24 > block.len() {
            break;
        }

        block[offset..offset + 4].copy_from_slice(&extent.ee_block.to_le_bytes());
        block[offset + 4..offset + 8].copy_from_slice(&extent.ee_len.to_le_bytes());
        block[offset + 8..offset + 12].copy_from_slice(&extent.ee_start.to_le_bytes());
        block[offset + 12..offset + 14].copy_from_slice(&extent.ee_comp_algo.to_le_bytes());
        block[offset + 14] = extent.ee_enc_algo;
        block[offset + 15] = extent.ee_reserved;
        block[offset + 16..offset + 18].copy_from_slice(&extent.ee_flags.to_le_bytes());
        block[offset + 18..offset + 20].copy_from_slice(&extent.ee_reserved2.to_le_bytes());
        block[offset + 20..offset + 24].copy_from_slice(&extent.ee_meta.to_le_bytes());

        offset += 24;
    }

    fs.write_block(block_num, &block)
}

/// Read all xattr data from extents
pub fn read_xattr_data(fs: &mut LolelfFs, index: &XattrIndex) -> Result<Vec<u8>> {
    let mut data = Vec::with_capacity(index.total_size as usize);

    for extent in &index.extents {
        if extent.is_empty() {
            break;
        }

        for i in 0..extent.ee_len {
            let block_num = extent.ee_start + i;
            let block = fs.read_block(block_num)?;
            data.extend_from_slice(&block);
        }
    }

    // Truncate to actual size
    data.truncate(index.total_size as usize);
    Ok(data)
}

/// Parse xattr entries from raw data
pub fn parse_xattr_entries(data: &[u8]) -> Result<Vec<XattrEntry>> {
    let mut entries = Vec::new();
    let mut offset = 0;

    while offset + 12 <= data.len() {
        let name_len = data[offset];
        let name_index = data[offset + 1];
        let value_len = u16::from_le_bytes([data[offset + 2], data[offset + 3]]);
        let value_offset = u32::from_le_bytes([
            data[offset + 4],
            data[offset + 5],
            data[offset + 6],
            data[offset + 7],
        ]);

        // Check for end marker
        if name_len == 0 && value_len == 0 {
            break;
        }

        let header_offset = offset;
        offset += 12;

        // Read name (NUL-terminated)
        if offset + name_len as usize > data.len() {
            bail!("Corrupt xattr: name extends beyond data");
        }

        let name_bytes = &data[offset..offset + name_len as usize];
        let name = String::from_utf8(name_bytes.to_vec())
            .map_err(|e| anyhow::anyhow!("Invalid UTF-8 in xattr name: {}", e))?;

        offset += name_len as usize;

        // Skip NUL terminator
        if offset >= data.len() || data[offset] != 0 {
            bail!("Corrupt xattr: missing NUL terminator after name");
        }
        offset += 1;

        // Read value
        let value_abs_offset = header_offset + value_offset as usize;
        if value_abs_offset + value_len as usize > data.len() {
            bail!("Corrupt xattr: value extends beyond data");
        }

        let value = data[value_abs_offset..value_abs_offset + value_len as usize].to_vec();

        let namespace = match name_index {
            0 => XattrNamespace::User,
            1 => XattrNamespace::Trusted,
            2 => XattrNamespace::System,
            3 => XattrNamespace::Security,
            _ => bail!("Invalid xattr namespace index: {}", name_index),
        };

        entries.push(XattrEntry {
            name_len: name.len() as u8,
            name_index: namespace,
            value_len: value.len() as u16,
            value_offset: 0, // Will be calculated during serialization
            name,
            value,
        });
    }

    Ok(entries)
}

/// Serialize xattr entries to bytes
pub fn serialize_xattr_entries(entries: &[XattrEntry]) -> Result<Vec<u8>> {
    let mut data = Vec::new();

    for entry in entries {
        let name_index_u8 = match entry.name_index {
            XattrNamespace::User => 0u8,
            XattrNamespace::Trusted => 1u8,
            XattrNamespace::System => 2u8,
            XattrNamespace::Security => 3u8,
        };

        let name_len = entry.name.len() as u8;
        let value_len = entry.value.len() as u16;

        // Calculate value offset: header(12) + name + NUL
        let value_offset = 12 + name_len as u32 + 1;

        // Write header
        data.push(name_len);
        data.push(name_index_u8);
        data.extend_from_slice(&value_len.to_le_bytes());
        data.extend_from_slice(&value_offset.to_le_bytes());
        data.extend_from_slice(&[0u8; 4]); // reserved

        // Write name + NUL
        data.extend_from_slice(entry.name.as_bytes());
        data.push(0); // NUL terminator

        // Write value
        data.extend_from_slice(&entry.value);
    }

    Ok(data)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_xattr_name() {
        let (ns, name) = parse_xattr_name("user.foo").unwrap();
        assert!(matches!(ns, XattrNamespace::User));
        assert_eq!(name, "foo");

        let (ns, name) = parse_xattr_name("security.selinux").unwrap();
        assert!(matches!(ns, XattrNamespace::Security));
        assert_eq!(name, "selinux");

        assert!(parse_xattr_name("invalid").is_err());
    }

    #[test]
    fn test_serialize_parse_roundtrip() {
        let entries = vec![
            XattrEntry {
                name_len: 4,
                name_index: XattrNamespace::User,
                value_len: 6,
                value_offset: 0,
                name: "test".to_string(),
                value: b"value1".to_vec(),
            },
            XattrEntry {
                name_len: 7,
                name_index: XattrNamespace::Security,
                value_len: 36,
                value_offset: 0,
                name: "selinux".to_string(),
                value: b"unconfined_u:object_r:user_home_t:s0".to_vec(),
            },
        ];

        let serialized = serialize_xattr_entries(&entries).unwrap();
        let parsed = parse_xattr_entries(&serialized).unwrap();

        assert_eq!(entries.len(), parsed.len());
        for (orig, parsed) in entries.iter().zip(parsed.iter()) {
            assert_eq!(orig.name, parsed.name);
            assert_eq!(orig.value, parsed.value);
        }
    }
}
