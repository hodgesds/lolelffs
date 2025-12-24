/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lolelffs - Encryption support
 *
 * Per-block encryption/decryption layer using kernel crypto API
 */

#ifndef LOLELFFS_ENCRYPT_H
#define LOLELFFS_ENCRYPT_H

#include <linux/types.h>

/**
 * lolelffs_encrypt_block - Encrypt a block of data
 * @algo: Encryption algorithm ID (LOLELFFS_ENC_*)
 * @key: Encryption key (32 bytes)
 * @iv: Initialization vector (16 bytes for AES, 12 for ChaCha20)
 * @block_num: Logical block number (used for IV derivation)
 * @src: Source data buffer (LOLELFFS_BLOCK_SIZE)
 * @dst: Destination buffer for encrypted data (LOLELFFS_BLOCK_SIZE + tag size)
 *
 * Returns 0 on success, negative error code on failure.
 */
int lolelffs_encrypt_block(u8 algo, const u8 *key, u64 block_num,
			    const void *src, void *dst);

/**
 * lolelffs_decrypt_block - Decrypt a block of data
 * @algo: Encryption algorithm ID (LOLELFFS_ENC_*)
 * @key: Decryption key (32 bytes)
 * @block_num: Logical block number (used for IV derivation)
 * @src: Encrypted data buffer
 * @dst: Destination buffer for decrypted data (LOLELFFS_BLOCK_SIZE)
 *
 * Returns 0 on success, negative error code on failure.
 * Returns -EBADMSG if authentication fails (for AEAD modes).
 */
int lolelffs_decrypt_block(u8 algo, const u8 *key, u64 block_num,
			    const void *src, void *dst);

/**
 * lolelffs_derive_key - Derive encryption key from password
 * @kdf_algo: KDF algorithm (LOLELFFS_KDF_*)
 * @password: User password
 * @password_len: Password length
 * @salt: Salt (32 bytes)
 * @iterations: Number of iterations
 * @memory: Memory cost in KB (for Argon2)
 * @parallelism: Parallelism factor (for Argon2)
 * @key_out: Output buffer for derived key (32 bytes)
 *
 * Returns 0 on success, negative error code on failure.
 */
int lolelffs_derive_key(u8 kdf_algo, const u8 *password, size_t password_len,
			 const u8 *salt, u32 iterations, u32 memory,
			 u32 parallelism, u8 *key_out);

/**
 * lolelffs_decrypt_master_key - Decrypt the filesystem master key
 * @encrypted_key: Encrypted master key from superblock (32 bytes)
 * @user_key: User-derived key from password (32 bytes)
 * @master_key_out: Output buffer for decrypted master key (32 bytes)
 *
 * Uses AES-256-ECB to decrypt the master key. The 32-byte master key
 * is decrypted as two 16-byte AES blocks.
 *
 * Returns 0 on success, negative error code on failure.
 */
int lolelffs_decrypt_master_key(const u8 *encrypted_key, const u8 *user_key,
				 u8 *master_key_out);

/**
 * lolelffs_enc_init - Initialize encryption subsystem
 *
 * Allocates encryption contexts and initializes crypto transforms.
 * Must be called before any encryption operations.
 *
 * Returns 0 on success, negative error code on failure.
 */
int lolelffs_enc_init(void);

/**
 * lolelffs_enc_exit - Cleanup encryption subsystem
 *
 * Frees all encryption resources and zeros sensitive memory.
 * Should be called during module unload.
 */
void lolelffs_enc_exit(void);

/**
 * lolelffs_enc_supported - Check if algorithm is supported
 * @algo: Encryption algorithm ID
 *
 * Returns true if the algorithm is supported by the kernel.
 */
bool lolelffs_enc_supported(u8 algo);

/**
 * lolelffs_enc_get_name - Get algorithm name string
 * @algo: Encryption algorithm ID
 *
 * Returns algorithm name string, or "unknown" if invalid.
 */
const char *lolelffs_enc_get_name(u8 algo);

/**
 * lolelffs_enc_tag_size - Get authentication tag size for algorithm
 * @algo: Encryption algorithm ID
 *
 * Returns tag size in bytes, or 0 for non-AEAD algorithms.
 */
size_t lolelffs_enc_tag_size(u8 algo);

#endif /* LOLELFFS_ENCRYPT_H */
