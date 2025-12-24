/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lolelffs - Compression support
 *
 * Compression/decompression layer using kernel crypto API
 */

#ifndef LOLELFFS_COMPRESS_H
#define LOLELFFS_COMPRESS_H

#include <linux/types.h>

/**
 * lolelffs_compress_block - Compress a block of data
 * @algo: Compression algorithm ID (LOLELFFS_COMP_*)
 * @src: Source data buffer
 * @src_len: Source data length (should be LOLELFFS_BLOCK_SIZE)
 * @dst: Destination buffer for compressed data (should be LOLELFFS_BLOCK_SIZE)
 * @comp_size: Output parameter for compressed size
 *
 * Returns 0 on success, negative error code on failure.
 * If compression doesn't save space (comp_size >= src_len), returns -E2BIG.
 */
int lolelffs_compress_block(u8 algo, const void *src, size_t src_len,
			     void *dst, size_t *comp_size);

/**
 * lolelffs_decompress_block - Decompress a block of data
 * @algo: Compression algorithm ID (LOLELFFS_COMP_*)
 * @src: Compressed data buffer
 * @src_len: Compressed data length
 * @dst: Destination buffer for decompressed data (should be LOLELFFS_BLOCK_SIZE)
 * @dst_len: Size of destination buffer
 *
 * Returns 0 on success, negative error code on failure.
 */
int lolelffs_decompress_block(u8 algo, const void *src, size_t src_len,
			       void *dst, size_t dst_len);

/**
 * lolelffs_comp_init - Initialize compression subsystem
 *
 * Allocates compression contexts and initializes crypto transforms.
 * Must be called before any compression operations.
 *
 * Returns 0 on success, negative error code on failure.
 */
int lolelffs_comp_init(void);

/**
 * lolelffs_comp_exit - Cleanup compression subsystem
 *
 * Frees all compression resources.
 * Should be called during module unload.
 */
void lolelffs_comp_exit(void);

/**
 * lolelffs_comp_supported - Check if algorithm is supported
 * @algo: Compression algorithm ID
 *
 * Returns true if the algorithm is supported by the kernel.
 */
bool lolelffs_comp_supported(u8 algo);

/**
 * lolelffs_comp_get_name - Get algorithm name string
 * @algo: Compression algorithm ID
 *
 * Returns algorithm name string, or "unknown" if invalid.
 */
const char *lolelffs_comp_get_name(u8 algo);

#endif /* LOLELFFS_COMPRESS_H */
