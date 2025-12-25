//! Encryption support for lolelffs
//!
//! Provides per-block encryption and decryption using AES-256-XTS and ChaCha20-Poly1305.
//! Matches the kernel module encryption behavior.

use crate::types::*;
use aes::Aes256;
use anyhow::{bail, Result};
use chacha20poly1305::{
    aead::{Aead, KeyInit},
    ChaCha20Poly1305, Nonce,
};
use pbkdf2::pbkdf2_hmac;
use rand::RngCore;
use sha2::{Digest, Sha256};
use xts_mode::Xts128;

/// Encrypt a block using AES-256-XTS
pub fn encrypt_aes_xts(key: &[u8; 32], block_num: u64, plaintext: &[u8]) -> Result<Vec<u8>> {
    if plaintext.len() != LOLELFFS_BLOCK_SIZE as usize {
        bail!("Plaintext must be exactly {} bytes", LOLELFFS_BLOCK_SIZE);
    }

    // XTS mode requires two separate keys
    // Derive second key from first using SHA256
    let second_key = Sha256::digest(key);

    // Create two AES-256 cipher instances
    let cipher1 = Aes256::new_from_slice(key)
        .map_err(|_| anyhow::anyhow!("Failed to create first AES cipher"))?;
    let cipher2 = Aes256::new_from_slice(&second_key)
        .map_err(|_| anyhow::anyhow!("Failed to create second AES cipher"))?;

    // Create XTS cipher with both instances
    let cipher = Xts128::<Aes256>::new(cipher1, cipher2);

    // Use block number as tweak (sector number)
    let tweak = block_num as u128;

    // Encrypt in place
    let mut ciphertext = plaintext.to_vec();
    cipher.encrypt_area(
        &mut ciphertext,
        LOLELFFS_BLOCK_SIZE as usize,
        tweak,
        |t: u128| (t + 1).to_le_bytes(),
    );

    Ok(ciphertext)
}

/// Decrypt a block using AES-256-XTS
pub fn decrypt_aes_xts(key: &[u8; 32], block_num: u64, ciphertext: &[u8]) -> Result<Vec<u8>> {
    if ciphertext.len() != LOLELFFS_BLOCK_SIZE as usize {
        bail!("Ciphertext must be exactly {} bytes", LOLELFFS_BLOCK_SIZE);
    }

    // XTS mode requires two separate keys
    // Derive second key from first using SHA256
    let second_key = Sha256::digest(key);

    // Create two AES-256 cipher instances
    let cipher1 = Aes256::new_from_slice(key)
        .map_err(|_| anyhow::anyhow!("Failed to create first AES cipher"))?;
    let cipher2 = Aes256::new_from_slice(&second_key)
        .map_err(|_| anyhow::anyhow!("Failed to create second AES cipher"))?;

    // Create XTS cipher with both instances
    let cipher = Xts128::<Aes256>::new(cipher1, cipher2);

    // Use block number as tweak (sector number)
    let tweak = block_num as u128;

    // Decrypt in place
    let mut plaintext = ciphertext.to_vec();
    cipher.decrypt_area(
        &mut plaintext,
        LOLELFFS_BLOCK_SIZE as usize,
        tweak,
        |t: u128| (t + 1).to_le_bytes(),
    );

    Ok(plaintext)
}

/// Encrypt a block using ChaCha20-Poly1305
pub fn encrypt_chacha20_poly1305(
    key: &[u8; 32],
    block_num: u64,
    plaintext: &[u8],
) -> Result<Vec<u8>> {
    if plaintext.len() != LOLELFFS_BLOCK_SIZE as usize {
        bail!("Plaintext must be exactly {} bytes", LOLELFFS_BLOCK_SIZE);
    }

    // Create cipher
    let cipher = ChaCha20Poly1305::new(key.into());

    // Derive nonce from block number (12 bytes)
    let mut nonce_bytes = [0u8; 12];
    nonce_bytes[..8].copy_from_slice(&block_num.to_le_bytes());
    let nonce = Nonce::from_slice(&nonce_bytes);

    // Encrypt with authentication
    let ciphertext = cipher
        .encrypt(nonce, plaintext)
        .map_err(|_| anyhow::anyhow!("ChaCha20-Poly1305 encryption failed"))?;

    Ok(ciphertext)
}

/// Decrypt a block using ChaCha20-Poly1305
pub fn decrypt_chacha20_poly1305(
    key: &[u8; 32],
    block_num: u64,
    ciphertext: &[u8],
) -> Result<Vec<u8>> {
    // Ciphertext includes 16-byte authentication tag
    if ciphertext.len() != LOLELFFS_BLOCK_SIZE as usize + 16 {
        bail!(
            "Ciphertext must be exactly {} bytes (data + tag)",
            LOLELFFS_BLOCK_SIZE + 16
        );
    }

    // Create cipher
    let cipher = ChaCha20Poly1305::new(key.into());

    // Derive nonce from block number (12 bytes)
    let mut nonce_bytes = [0u8; 12];
    nonce_bytes[..8].copy_from_slice(&block_num.to_le_bytes());
    let nonce = Nonce::from_slice(&nonce_bytes);

    // Decrypt with authentication verification
    let plaintext = cipher.decrypt(nonce, ciphertext).map_err(|_| {
        anyhow::anyhow!("ChaCha20-Poly1305 decryption failed (authentication failed)")
    })?;

    Ok(plaintext)
}

/// Encrypt a block using the specified algorithm
pub fn encrypt_block(
    algo: u8,
    key: &[u8; 32],
    block_num: u64,
    plaintext: &[u8],
) -> Result<Vec<u8>> {
    match algo {
        LOLELFFS_ENC_NONE => bail!("Cannot encrypt with NONE algorithm"),
        LOLELFFS_ENC_AES256_XTS => encrypt_aes_xts(key, block_num, plaintext),
        LOLELFFS_ENC_CHACHA20_POLY => encrypt_chacha20_poly1305(key, block_num, plaintext),
        _ => bail!("Unsupported encryption algorithm: {}", algo),
    }
}

/// Decrypt a block using the specified algorithm
pub fn decrypt_block(
    algo: u8,
    key: &[u8; 32],
    block_num: u64,
    ciphertext: &[u8],
) -> Result<Vec<u8>> {
    match algo {
        LOLELFFS_ENC_NONE => bail!("Cannot decrypt with NONE algorithm"),
        LOLELFFS_ENC_AES256_XTS => decrypt_aes_xts(key, block_num, ciphertext),
        LOLELFFS_ENC_CHACHA20_POLY => decrypt_chacha20_poly1305(key, block_num, ciphertext),
        _ => bail!("Unsupported encryption algorithm: {}", algo),
    }
}

/// Derive a key from a password using PBKDF2-HMAC-SHA256
pub fn derive_key_pbkdf2(password: &[u8], salt: &[u8; 32], iterations: u32) -> [u8; 32] {
    let mut key = [0u8; 32];
    pbkdf2_hmac::<Sha256>(password, salt, iterations, &mut key);
    key
}

/// Generate a random salt
pub fn generate_salt() -> [u8; 32] {
    let mut salt = [0u8; 32];
    rand::thread_rng().fill_bytes(&mut salt);
    salt
}

/// Get encryption algorithm name
pub fn get_algo_name(algo: u8) -> &'static str {
    match algo {
        LOLELFFS_ENC_NONE => "none",
        LOLELFFS_ENC_AES256_XTS => "aes-256-xts",
        LOLELFFS_ENC_CHACHA20_POLY => "chacha20-poly1305",
        _ => "unknown",
    }
}

/// Get authentication tag size for algorithm
pub fn get_tag_size(algo: u8) -> usize {
    match algo {
        LOLELFFS_ENC_CHACHA20_POLY => 16,
        _ => 0,
    }
}

/// Generate a random master key
pub fn generate_master_key() -> [u8; 32] {
    let mut key = [0u8; 32];
    rand::thread_rng().fill_bytes(&mut key);
    key
}

/// Encrypt master key with user-derived key (AES-256 ECB for single block)
pub fn encrypt_master_key(master_key: &[u8; 32], user_key: &[u8; 32]) -> Result<[u8; 32]> {
    use aes::cipher::{BlockEncrypt, KeyInit};

    // Use AES-256 in ECB mode for single 32-byte block (two AES blocks)
    let cipher = Aes256::new(user_key.into());

    let mut encrypted = [0u8; 32];

    // Encrypt first 16 bytes
    let mut block1 = aes::Block::clone_from_slice(&master_key[..16]);
    cipher.encrypt_block(&mut block1);
    encrypted[..16].copy_from_slice(&block1);

    // Encrypt second 16 bytes
    let mut block2 = aes::Block::clone_from_slice(&master_key[16..]);
    cipher.encrypt_block(&mut block2);
    encrypted[16..].copy_from_slice(&block2);

    Ok(encrypted)
}

/// Decrypt master key with user-derived key
pub fn decrypt_master_key(encrypted_key: &[u8; 32], user_key: &[u8; 32]) -> Result<[u8; 32]> {
    use aes::cipher::{BlockDecrypt, KeyInit};

    // Use AES-256 in ECB mode for single 32-byte block (two AES blocks)
    let cipher = Aes256::new(user_key.into());

    let mut decrypted = [0u8; 32];

    // Decrypt first 16 bytes
    let mut block1 = aes::Block::clone_from_slice(&encrypted_key[..16]);
    cipher.decrypt_block(&mut block1);
    decrypted[..16].copy_from_slice(&block1);

    // Decrypt second 16 bytes
    let mut block2 = aes::Block::clone_from_slice(&encrypted_key[16..]);
    cipher.decrypt_block(&mut block2);
    decrypted[16..].copy_from_slice(&block2);

    Ok(decrypted)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_aes_xts_roundtrip() {
        let key = [42u8; 32];
        let block_num = 123;
        let plaintext = vec![0xAAu8; LOLELFFS_BLOCK_SIZE as usize];

        let ciphertext = encrypt_aes_xts(&key, block_num, &plaintext).unwrap();
        assert_eq!(ciphertext.len(), LOLELFFS_BLOCK_SIZE as usize);
        assert_ne!(ciphertext, plaintext); // Should be different

        let decrypted = decrypt_aes_xts(&key, block_num, &ciphertext).unwrap();
        assert_eq!(decrypted, plaintext);
    }

    #[test]
    fn test_chacha20_poly1305_roundtrip() {
        let key = [42u8; 32];
        let block_num = 456;
        let plaintext = vec![0xBBu8; LOLELFFS_BLOCK_SIZE as usize];

        let ciphertext = encrypt_chacha20_poly1305(&key, block_num, &plaintext).unwrap();
        assert_eq!(ciphertext.len(), LOLELFFS_BLOCK_SIZE as usize + 16); // +16 for tag
        assert_ne!(&ciphertext[..LOLELFFS_BLOCK_SIZE as usize], &plaintext[..]); // Should be different

        let decrypted = decrypt_chacha20_poly1305(&key, block_num, &ciphertext).unwrap();
        assert_eq!(decrypted, plaintext);
    }

    #[test]
    fn test_chacha20_poly1305_authentication() {
        let key = [42u8; 32];
        let block_num = 789;
        let plaintext = vec![0xCCu8; LOLELFFS_BLOCK_SIZE as usize];

        let mut ciphertext = encrypt_chacha20_poly1305(&key, block_num, &plaintext).unwrap();

        // Corrupt the ciphertext
        ciphertext[100] ^= 1;

        // Decryption should fail due to authentication
        let result = decrypt_chacha20_poly1305(&key, block_num, &ciphertext);
        assert!(result.is_err());
    }

    #[test]
    fn test_pbkdf2_derivation() {
        let password = b"test_password";
        let salt = [0x42u8; 32];
        let iterations = 10000;

        let key1 = derive_key_pbkdf2(password, &salt, iterations);
        let key2 = derive_key_pbkdf2(password, &salt, iterations);

        // Same input should produce same key
        assert_eq!(key1, key2);

        // Different password should produce different key
        let key3 = derive_key_pbkdf2(b"different_password", &salt, iterations);
        assert_ne!(key1, key3);
    }
}
