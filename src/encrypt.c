// SPDX-License-Identifier: GPL-2.0
/*
 * lolelffs - Encryption support
 *
 * Per-block encryption/decryption using kernel crypto API
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <crypto/skcipher.h>
#include <crypto/aead.h>
#include <crypto/hash.h>
#include "lolelffs.h"
#include "encrypt.h"

/* Encryption algorithm names for kernel crypto API */
static const char *enc_algo_names[] = {
	[LOLELFFS_ENC_NONE] = "none",
	[LOLELFFS_ENC_AES256_XTS] = "xts(aes)",
	[LOLELFFS_ENC_CHACHA20_POLY] = "rfc7539(chacha20,poly1305)",
};

/* Display names */
static const char *enc_algo_display_names[] = {
	[LOLELFFS_ENC_NONE] = "none",
	[LOLELFFS_ENC_AES256_XTS] = "aes-256-xts",
	[LOLELFFS_ENC_CHACHA20_POLY] = "chacha20-poly1305",
};

#define LOLELFFS_ENC_MAX_ALGO LOLELFFS_ENC_CHACHA20_POLY

/* Key sizes */
#define AES_XTS_KEY_SIZE 64  /* 2 x 256 bits for XTS mode */
#define CHACHA20_KEY_SIZE 32 /* 256 bits */
#define AES_IV_SIZE 16
#define CHACHA20_IV_SIZE 12

/* Authentication tag size for AEAD */
#define CHACHA20_POLY1305_TAG_SIZE 16

/* Encryption context */
struct lolelffs_enc_ctx {
	struct crypto_skcipher *skcipher; /* For AES-XTS */
	struct crypto_aead *aead;          /* For ChaCha20-Poly1305 */
	bool available;
};

static struct lolelffs_enc_ctx enc_ctx[LOLELFFS_ENC_MAX_ALGO + 1];
static DEFINE_MUTEX(enc_mutex);

/**
 * lolelffs_enc_supported - Check if algorithm is supported
 */
bool lolelffs_enc_supported(u8 algo)
{
	if (algo > LOLELFFS_ENC_MAX_ALGO)
		return false;

	if (algo == LOLELFFS_ENC_NONE)
		return true;

	return enc_ctx[algo].available;
}

/**
 * lolelffs_enc_get_name - Get algorithm name string
 */
const char *lolelffs_enc_get_name(u8 algo)
{
	if (algo > LOLELFFS_ENC_MAX_ALGO)
		return "unknown";

	return enc_algo_display_names[algo];
}

/**
 * lolelffs_enc_tag_size - Get authentication tag size
 */
size_t lolelffs_enc_tag_size(u8 algo)
{
	switch (algo) {
	case LOLELFFS_ENC_CHACHA20_POLY:
		return CHACHA20_POLY1305_TAG_SIZE;
	default:
		return 0;
	}
}

/**
 * derive_iv_from_block - Derive IV from block number
 */
static void derive_iv_from_block(u64 block_num, u8 *iv, size_t iv_size)
{
	/* Simple IV derivation: use block number as counter */
	memset(iv, 0, iv_size);
	memcpy(iv, &block_num, min(sizeof(block_num), iv_size));
}

/**
 * lolelffs_encrypt_aes_xts - Encrypt using AES-256-XTS
 */
static int lolelffs_encrypt_aes_xts(const u8 *key, u64 block_num,
				    const void *src, void *dst)
{
	struct skcipher_request *req;
	struct scatterlist sg_src, sg_dst;
	u8 iv[AES_IV_SIZE];
	int ret;
	DECLARE_CRYPTO_WAIT(wait);

	if (!enc_ctx[LOLELFFS_ENC_AES256_XTS].available)
		return -EOPNOTSUPP;

	/* Derive IV from block number */
	derive_iv_from_block(block_num, iv, AES_IV_SIZE);

	/* Allocate request */
	req = skcipher_request_alloc(enc_ctx[LOLELFFS_ENC_AES256_XTS].skcipher,
				     GFP_NOFS);
	if (!req)
		return -ENOMEM;

	/* Set up scatter-gather lists */
	sg_init_one(&sg_src, src, LOLELFFS_BLOCK_SIZE);
	sg_init_one(&sg_dst, dst, LOLELFFS_BLOCK_SIZE);

	/* Set up request */
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
					   CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &wait);
	skcipher_request_set_crypt(req, &sg_src, &sg_dst,
				   LOLELFFS_BLOCK_SIZE, iv);

	/* Perform encryption */
	ret = crypto_wait_req(crypto_skcipher_encrypt(req), &wait);

	skcipher_request_free(req);
	return ret;
}

/**
 * lolelffs_decrypt_aes_xts - Decrypt using AES-256-XTS
 */
static int lolelffs_decrypt_aes_xts(const u8 *key, u64 block_num,
				    const void *src, void *dst)
{
	struct skcipher_request *req;
	struct scatterlist sg_src, sg_dst;
	u8 iv[AES_IV_SIZE];
	int ret;
	DECLARE_CRYPTO_WAIT(wait);

	if (!enc_ctx[LOLELFFS_ENC_AES256_XTS].available)
		return -EOPNOTSUPP;

	/* Derive IV from block number */
	derive_iv_from_block(block_num, iv, AES_IV_SIZE);

	/* Allocate request */
	req = skcipher_request_alloc(enc_ctx[LOLELFFS_ENC_AES256_XTS].skcipher,
				     GFP_NOFS);
	if (!req)
		return -ENOMEM;

	/* Set up scatter-gather lists */
	sg_init_one(&sg_src, src, LOLELFFS_BLOCK_SIZE);
	sg_init_one(&sg_dst, dst, LOLELFFS_BLOCK_SIZE);

	/* Set up request */
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
					   CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &wait);
	skcipher_request_set_crypt(req, &sg_src, &sg_dst,
				   LOLELFFS_BLOCK_SIZE, iv);

	/* Perform decryption */
	ret = crypto_wait_req(crypto_skcipher_decrypt(req), &wait);

	skcipher_request_free(req);
	return ret;
}

/**
 * lolelffs_encrypt_chacha20_poly - Encrypt using ChaCha20-Poly1305
 */
static int lolelffs_encrypt_chacha20_poly(const u8 *key, u64 block_num,
					  const void *src, void *dst)
{
	struct aead_request *req;
	struct scatterlist sg_src, sg_dst;
	u8 iv[CHACHA20_IV_SIZE];
	int ret;
	DECLARE_CRYPTO_WAIT(wait);

	if (!enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].available)
		return -EOPNOTSUPP;

	/* Derive IV from block number */
	derive_iv_from_block(block_num, iv, CHACHA20_IV_SIZE);

	/* Allocate request */
	req = aead_request_alloc(enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead,
				 GFP_NOFS);
	if (!req)
		return -ENOMEM;

	/* Set up scatter-gather lists - dst includes tag */
	sg_init_one(&sg_src, src, LOLELFFS_BLOCK_SIZE);
	sg_init_one(&sg_dst, dst, LOLELFFS_BLOCK_SIZE + CHACHA20_POLY1305_TAG_SIZE);

	/* Set up request */
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				       CRYPTO_TFM_REQ_MAY_SLEEP,
				  crypto_req_done, &wait);
	aead_request_set_crypt(req, &sg_src, &sg_dst,
			       LOLELFFS_BLOCK_SIZE, iv);
	aead_request_set_ad(req, 0); /* No associated data */

	/* Perform encryption */
	ret = crypto_wait_req(crypto_aead_encrypt(req), &wait);

	aead_request_free(req);
	return ret;
}

/**
 * lolelffs_decrypt_chacha20_poly - Decrypt using ChaCha20-Poly1305
 */
static int lolelffs_decrypt_chacha20_poly(const u8 *key, u64 block_num,
					  const void *src, void *dst)
{
	struct aead_request *req;
	struct scatterlist sg_src, sg_dst;
	u8 iv[CHACHA20_IV_SIZE];
	int ret;
	DECLARE_CRYPTO_WAIT(wait);

	if (!enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].available)
		return -EOPNOTSUPP;

	/* Derive IV from block number */
	derive_iv_from_block(block_num, iv, CHACHA20_IV_SIZE);

	/* Allocate request */
	req = aead_request_alloc(enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead,
				 GFP_NOFS);
	if (!req)
		return -ENOMEM;

	/* Set up scatter-gather lists - src includes tag */
	sg_init_one(&sg_src, src, LOLELFFS_BLOCK_SIZE + CHACHA20_POLY1305_TAG_SIZE);
	sg_init_one(&sg_dst, dst, LOLELFFS_BLOCK_SIZE);

	/* Set up request */
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				       CRYPTO_TFM_REQ_MAY_SLEEP,
				  crypto_req_done, &wait);
	aead_request_set_crypt(req, &sg_src, &sg_dst,
			       LOLELFFS_BLOCK_SIZE + CHACHA20_POLY1305_TAG_SIZE,
			       iv);
	aead_request_set_ad(req, 0); /* No associated data */

	/* Perform decryption (includes authentication) */
	ret = crypto_wait_req(crypto_aead_decrypt(req), &wait);

	aead_request_free(req);
	return ret; /* Returns -EBADMSG if authentication fails */
}

/**
 * lolelffs_encrypt_block - Encrypt a block of data
 */
int lolelffs_encrypt_block(u8 algo, const u8 *key, u64 block_num,
			    const void *src, void *dst)
{
	int ret;

	if (algo == LOLELFFS_ENC_NONE || algo > LOLELFFS_ENC_MAX_ALGO)
		return -EINVAL;

	if (!enc_ctx[algo].available)
		return -EOPNOTSUPP;

	mutex_lock(&enc_mutex);

	switch (algo) {
	case LOLELFFS_ENC_AES256_XTS:
		ret = lolelffs_encrypt_aes_xts(key, block_num, src, dst);
		break;
	case LOLELFFS_ENC_CHACHA20_POLY:
		ret = lolelffs_encrypt_chacha20_poly(key, block_num, src, dst);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&enc_mutex);

	if (ret < 0)
		pr_debug("lolelffs: encryption failed (algo=%s): %d\n",
			 enc_algo_display_names[algo], ret);

	return ret;
}

/**
 * lolelffs_decrypt_block - Decrypt a block of data
 */
int lolelffs_decrypt_block(u8 algo, const u8 *key, u64 block_num,
			    const void *src, void *dst)
{
	int ret;

	if (algo == LOLELFFS_ENC_NONE || algo > LOLELFFS_ENC_MAX_ALGO)
		return -EINVAL;

	if (!enc_ctx[algo].available)
		return -EOPNOTSUPP;

	mutex_lock(&enc_mutex);

	switch (algo) {
	case LOLELFFS_ENC_AES256_XTS:
		ret = lolelffs_decrypt_aes_xts(key, block_num, src, dst);
		break;
	case LOLELFFS_ENC_CHACHA20_POLY:
		ret = lolelffs_decrypt_chacha20_poly(key, block_num, src, dst);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&enc_mutex);

	if (ret < 0)
		pr_err("lolelffs: decryption failed (algo=%s): %d\n",
		       enc_algo_display_names[algo], ret);

	return ret;
}

/**
 * lolelffs_derive_key - Derive encryption key from password
 *
 * Note: Currently implements PBKDF2-HMAC-SHA256. Argon2 support
 * requires kernel 5.17+ and is not yet implemented.
 */
int lolelffs_derive_key(u8 kdf_algo, const u8 *password, size_t password_len,
			 const u8 *salt, u32 iterations, u32 memory,
			 u32 parallelism, u8 *key_out)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	u8 counter[4];
	u8 temp[32]; /* SHA256 output */
	u32 i, j;
	int ret;

	/* Currently only PBKDF2 is supported */
	if (kdf_algo != LOLELFFS_KDF_PBKDF2) {
		pr_err("lolelffs: KDF algorithm %u not supported yet\n", kdf_algo);
		return -EOPNOTSUPP;
	}

	/* Allocate HMAC-SHA256 transform */
	tfm = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	/* Set the password as key for HMAC */
	ret = crypto_shash_setkey(tfm, password, password_len);
	if (ret < 0)
		goto out_free_tfm;

	/* Allocate descriptor */
	desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (!desc) {
		ret = -ENOMEM;
		goto out_free_tfm;
	}
	desc->tfm = tfm;

	/* PBKDF2: key_out = F(password, salt, iterations, 1) */
	/* F(password, salt, c, i) = U1 xor U2 xor ... xor Uc */
	/* U1 = PRF(password, salt || INT_32_BE(i)) */
	/* Uj = PRF(password, Uj-1) */

	memset(key_out, 0, 32);

	/* counter = 1 (big-endian) */
	counter[0] = 0;
	counter[1] = 0;
	counter[2] = 0;
	counter[3] = 1;

	/* U1 = HMAC(password, salt || counter) */
	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto out_free_desc;

	ret = crypto_shash_update(desc, salt, 32);
	if (ret < 0)
		goto out_free_desc;

	ret = crypto_shash_finup(desc, counter, 4, temp);
	if (ret < 0)
		goto out_free_desc;

	/* XOR U1 into result */
	for (j = 0; j < 32; j++)
		key_out[j] ^= temp[j];

	/* Iterate: Uj = HMAC(password, Uj-1) */
	for (i = 1; i < iterations; i++) {
		ret = crypto_shash_digest(desc, temp, 32, temp);
		if (ret < 0)
			goto out_free_desc;

		/* XOR Uj into result */
		for (j = 0; j < 32; j++)
			key_out[j] ^= temp[j];
	}

	ret = 0;

out_free_desc:
	kfree_sensitive(desc);
out_free_tfm:
	crypto_free_shash(tfm);

	/* Zero sensitive data */
	memzero_explicit(temp, sizeof(temp));

	return ret;
}

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
				 u8 *master_key_out)
{
	struct crypto_skcipher *tfm;
	struct skcipher_request *req;
	struct scatterlist sg;
	DECLARE_CRYPTO_WAIT(wait);
	u8 *buffer;
	int ret;

	/* Allocate AES cipher for ECB mode */
	tfm = crypto_alloc_skcipher("ecb(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("lolelffs: failed to allocate AES cipher: %ld\n", PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	/* Set the user key (32 bytes = 256 bits) */
	ret = crypto_skcipher_setkey(tfm, user_key, 32);
	if (ret < 0) {
		pr_err("lolelffs: failed to set AES key: %d\n", ret);
		goto out_free_tfm;
	}

	/* Allocate request structure */
	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto out_free_tfm;
	}

	/* Allocate buffer for in-place decryption (skcipher needs non-const) */
	buffer = kmalloc(32, GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto out_free_req;
	}

	/* Copy encrypted key to buffer */
	memcpy(buffer, encrypted_key, 32);

	/* Setup scatter-gather list for in-place decryption */
	sg_init_one(&sg, buffer, 32);

	/* Setup the request */
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
				       crypto_req_done, &wait);
	skcipher_request_set_crypt(req, &sg, &sg, 32, NULL);

	/* Perform decryption */
	ret = crypto_wait_req(crypto_skcipher_decrypt(req), &wait);
	if (ret < 0) {
		pr_err("lolelffs: master key decryption failed: %d\n", ret);
		goto out_free_buffer;
	}

	/* Copy decrypted key to output */
	memcpy(master_key_out, buffer, 32);
	ret = 0;

out_free_buffer:
	kfree_sensitive(buffer);
out_free_req:
	skcipher_request_free(req);
out_free_tfm:
	crypto_free_skcipher(tfm);
	return ret;
}

/**
 * lolelffs_enc_init - Initialize encryption subsystem
 */
int lolelffs_enc_init(void)
{
	int ret = 0;
	bool any_available = false;

	pr_info("lolelffs: initializing encryption support\n");

	/* Initialize AES-256-XTS */
	enc_ctx[LOLELFFS_ENC_AES256_XTS].skcipher =
		crypto_alloc_skcipher(enc_algo_names[LOLELFFS_ENC_AES256_XTS], 0, 0);

	if (IS_ERR(enc_ctx[LOLELFFS_ENC_AES256_XTS].skcipher)) {
		pr_warn("lolelffs: AES-256-XTS not available: %ld\n",
			PTR_ERR(enc_ctx[LOLELFFS_ENC_AES256_XTS].skcipher));
		enc_ctx[LOLELFFS_ENC_AES256_XTS].skcipher = NULL;
		enc_ctx[LOLELFFS_ENC_AES256_XTS].available = false;
	} else {
		enc_ctx[LOLELFFS_ENC_AES256_XTS].available = true;
		any_available = true;
		pr_info("lolelffs: AES-256-XTS encryption initialized\n");
	}

	/* Initialize ChaCha20-Poly1305 */
	enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead =
		crypto_alloc_aead(enc_algo_names[LOLELFFS_ENC_CHACHA20_POLY], 0, 0);

	if (IS_ERR(enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead)) {
		pr_warn("lolelffs: ChaCha20-Poly1305 not available: %ld\n",
			PTR_ERR(enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead));
		enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead = NULL;
		enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].available = false;
	} else {
		/* Set authentication tag length */
		ret = crypto_aead_setauthsize(enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead,
					      CHACHA20_POLY1305_TAG_SIZE);
		if (ret < 0) {
			pr_warn("lolelffs: ChaCha20-Poly1305 setauthsize failed: %d\n", ret);
			crypto_free_aead(enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead);
			enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead = NULL;
			enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].available = false;
		} else {
			enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].available = true;
			any_available = true;
			pr_info("lolelffs: ChaCha20-Poly1305 encryption initialized\n");
		}
	}

	if (!any_available) {
		pr_warn("lolelffs: no encryption algorithms available\n");
		/* This is not a fatal error - encryption is optional */
		return 0;
	}

	return 0;
}

/**
 * lolelffs_enc_exit - Cleanup encryption subsystem
 */
void lolelffs_enc_exit(void)
{
	pr_info("lolelffs: cleaning up encryption support\n");

	if (enc_ctx[LOLELFFS_ENC_AES256_XTS].skcipher) {
		crypto_free_skcipher(enc_ctx[LOLELFFS_ENC_AES256_XTS].skcipher);
		enc_ctx[LOLELFFS_ENC_AES256_XTS].skcipher = NULL;
		enc_ctx[LOLELFFS_ENC_AES256_XTS].available = false;
	}

	if (enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead) {
		crypto_free_aead(enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead);
		enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].aead = NULL;
		enc_ctx[LOLELFFS_ENC_CHACHA20_POLY].available = false;
	}
}
