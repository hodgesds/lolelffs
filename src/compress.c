// SPDX-License-Identifier: GPL-2.0
/*
 * lolelffs - Compression support
 *
 * Compression/decompression using kernel compression libraries
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/lz4.h>
#include <linux/zlib.h>
/*
 * ZSTD support status:
 * - Some kernels have CONFIG_ZSTD_COMPRESS but don't export ZSTD_compress/ZSTD_decompress
 * - Only context-based functions (zstd_compress_cctx) are exported
 * - TODO: Implement using context-based API for full ZSTD support
 * - For now, ZSTD is disabled to avoid link errors
 */
#define LOLELFFS_ENABLE_ZSTD 0

#if LOLELFFS_ENABLE_ZSTD && defined(CONFIG_ZSTD_COMPRESS)
#include <linux/zstd.h>
#define HAVE_ZSTD 1
#else
#define HAVE_ZSTD 0
#endif
#include "lolelffs.h"
#include "compress.h"

/* Compression algorithm names */
static const char *comp_algo_names[] = {
	[LOLELFFS_COMP_NONE] = "none",
	[LOLELFFS_COMP_LZ4] = "lz4",
	[LOLELFFS_COMP_ZLIB] = "zlib",
	[LOLELFFS_COMP_ZSTD] = "zstd",
};

#define LOLELFFS_COMP_MAX_ALGO LOLELFFS_COMP_ZSTD

/* Compression context */
struct lolelffs_comp_ctx {
	void *workspace;
	size_t workspace_size;
	bool available;
};

static struct lolelffs_comp_ctx comp_ctx[LOLELFFS_COMP_MAX_ALGO + 1];
static DEFINE_MUTEX(comp_mutex);

/**
 * lolelffs_comp_supported - Check if algorithm is supported
 */
bool lolelffs_comp_supported(u8 algo)
{
	if (algo > LOLELFFS_COMP_MAX_ALGO)
		return false;

	if (algo == LOLELFFS_COMP_NONE)
		return true;

	return comp_ctx[algo].available;
}

/**
 * lolelffs_comp_get_name - Get algorithm name string
 */
const char *lolelffs_comp_get_name(u8 algo)
{
	if (algo > LOLELFFS_COMP_MAX_ALGO)
		return "unknown";

	return comp_algo_names[algo];
}

/**
 * lolelffs_compress_lz4 - Compress using LZ4
 */
static int lolelffs_compress_lz4(const void *src, size_t src_len,
				  void *dst, size_t *comp_size)
{
	int ret;

	ret = LZ4_compress_default(src, dst, src_len, LOLELFFS_BLOCK_SIZE,
				    comp_ctx[LOLELFFS_COMP_LZ4].workspace);
	if (ret <= 0)
		return -EIO;

	*comp_size = ret;
	return 0;
}

/**
 * lolelffs_decompress_lz4 - Decompress using LZ4
 */
static int lolelffs_decompress_lz4(const void *src, size_t src_len,
				    void *dst, size_t dst_len)
{
	int ret;

	ret = LZ4_decompress_safe(src, dst, src_len, dst_len);
	if (ret < 0)
		return -EIO;

	if ((size_t)ret != dst_len)
		return -EIO;

	return 0;
}

/**
 * lolelffs_compress_zlib - Compress using zlib
 */
static int lolelffs_compress_zlib(const void *src, size_t src_len,
				   void *dst, size_t *comp_size)
{
	z_stream stream;
	int ret;

	stream.workspace = comp_ctx[LOLELFFS_COMP_ZLIB].workspace;
	stream.next_in = (u8 *)src;
	stream.avail_in = src_len;
	stream.next_out = dst;
	stream.avail_out = LOLELFFS_BLOCK_SIZE;
	stream.total_in = 0;
	stream.total_out = 0;

	ret = zlib_deflateInit(&stream, Z_DEFAULT_COMPRESSION);
	if (ret != Z_OK)
		return -EIO;

	ret = zlib_deflate(&stream, Z_FINISH);
	if (ret != Z_STREAM_END) {
		zlib_deflateEnd(&stream);
		return -EIO;
	}

	ret = zlib_deflateEnd(&stream);
	if (ret != Z_OK)
		return -EIO;

	*comp_size = stream.total_out;
	return 0;
}

/**
 * lolelffs_decompress_zlib - Decompress using zlib
 */
static int lolelffs_decompress_zlib(const void *src, size_t src_len,
				     void *dst, size_t dst_len)
{
	z_stream stream;
	int ret;

	stream.workspace = comp_ctx[LOLELFFS_COMP_ZLIB].workspace;
	stream.next_in = (u8 *)src;
	stream.avail_in = src_len;
	stream.next_out = dst;
	stream.avail_out = dst_len;
	stream.total_in = 0;
	stream.total_out = 0;

	ret = zlib_inflateInit(&stream);
	if (ret != Z_OK)
		return -EIO;

	ret = zlib_inflate(&stream, Z_FINISH);
	if (ret != Z_STREAM_END) {
		zlib_inflateEnd(&stream);
		return -EIO;
	}

	ret = zlib_inflateEnd(&stream);
	if (ret != Z_OK)
		return -EIO;

	if (stream.total_out != dst_len)
		return -EIO;

	return 0;
}

#if HAVE_ZSTD
/**
 * lolelffs_compress_zstd - Compress using zstd
 */
static int lolelffs_compress_zstd(const void *src, size_t src_len,
				   void *dst, size_t *comp_size)
{
	size_t ret;

	/* Use simple ZSTD_compress - no context needed */
	ret = ZSTD_compress(dst, LOLELFFS_BLOCK_SIZE, src, src_len, 3);

	if (ZSTD_isError(ret))
		return -EIO;

	*comp_size = ret;
	return 0;
}

/**
 * lolelffs_decompress_zstd - Decompress using zstd
 */
static int lolelffs_decompress_zstd(const void *src, size_t src_len,
				     void *dst, size_t dst_len)
{
	size_t ret;

	/* Use simple ZSTD_decompress - no context needed */
	ret = ZSTD_decompress(dst, dst_len, src, src_len);

	if (ZSTD_isError(ret))
		return -EIO;

	if (ret != dst_len)
		return -EIO;

	return 0;
}
#endif /* HAVE_ZSTD */

/**
 * lolelffs_compress_block - Compress a block of data
 */
int lolelffs_compress_block(u8 algo, const void *src, size_t src_len,
			     void *dst, size_t *comp_size)
{
	int ret;

	if (algo == LOLELFFS_COMP_NONE || algo > LOLELFFS_COMP_MAX_ALGO)
		return -EINVAL;

	if (!comp_ctx[algo].available)
		return -EOPNOTSUPP;

	mutex_lock(&comp_mutex);

	switch (algo) {
	case LOLELFFS_COMP_LZ4:
		ret = lolelffs_compress_lz4(src, src_len, dst, comp_size);
		break;
	case LOLELFFS_COMP_ZLIB:
		ret = lolelffs_compress_zlib(src, src_len, dst, comp_size);
		break;
#if HAVE_ZSTD
	case LOLELFFS_COMP_ZSTD:
		ret = lolelffs_compress_zstd(src, src_len, dst, comp_size);
		break;
#endif
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&comp_mutex);

	if (ret < 0) {
		pr_debug("lolelffs: compression failed (algo=%s): %d\n",
			 comp_algo_names[algo], ret);
		return ret;
	}

	/* Don't use compressed data if it doesn't save space */
	if (*comp_size >= src_len) {
		pr_debug("lolelffs: compression ineffective (%zu >= %zu)\n",
			 *comp_size, src_len);
		return -E2BIG;
	}

	return 0;
}

/**
 * lolelffs_decompress_block - Decompress a block of data
 */
int lolelffs_decompress_block(u8 algo, const void *src, size_t src_len,
			       void *dst, size_t dst_len)
{
	int ret;

	if (algo == LOLELFFS_COMP_NONE || algo > LOLELFFS_COMP_MAX_ALGO)
		return -EINVAL;

	if (!comp_ctx[algo].available)
		return -EOPNOTSUPP;

	mutex_lock(&comp_mutex);

	switch (algo) {
	case LOLELFFS_COMP_LZ4:
		ret = lolelffs_decompress_lz4(src, src_len, dst, dst_len);
		break;
	case LOLELFFS_COMP_ZLIB:
		ret = lolelffs_decompress_zlib(src, src_len, dst, dst_len);
		break;
#if HAVE_ZSTD
	case LOLELFFS_COMP_ZSTD:
		ret = lolelffs_decompress_zstd(src, src_len, dst, dst_len);
		break;
#endif
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&comp_mutex);

	if (ret < 0) {
		pr_err("lolelffs: decompression failed (algo=%s): %d\n",
		       comp_algo_names[algo], ret);
		return ret;
	}

	return 0;
}

/**
 * lolelffs_comp_init - Initialize compression subsystem
 */
int lolelffs_comp_init(void)
{
	int ret = 0;
	bool any_available = false;

	pr_info("lolelffs: initializing compression support\n");

	/* Initialize LZ4 */
	comp_ctx[LOLELFFS_COMP_LZ4].workspace_size = LZ4_MEM_COMPRESS;
	comp_ctx[LOLELFFS_COMP_LZ4].workspace = vmalloc(LZ4_MEM_COMPRESS);
	if (comp_ctx[LOLELFFS_COMP_LZ4].workspace) {
		comp_ctx[LOLELFFS_COMP_LZ4].available = true;
		any_available = true;
		pr_info("lolelffs: LZ4 compression initialized\n");
	} else {
		pr_warn("lolelffs: LZ4 workspace allocation failed\n");
		comp_ctx[LOLELFFS_COMP_LZ4].available = false;
	}

	/* Initialize zlib */
	comp_ctx[LOLELFFS_COMP_ZLIB].workspace_size = zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL);
	comp_ctx[LOLELFFS_COMP_ZLIB].workspace = vmalloc(comp_ctx[LOLELFFS_COMP_ZLIB].workspace_size);
	if (comp_ctx[LOLELFFS_COMP_ZLIB].workspace) {
		comp_ctx[LOLELFFS_COMP_ZLIB].available = true;
		any_available = true;
		pr_info("lolelffs: zlib compression initialized\n");
	} else {
		pr_warn("lolelffs: zlib workspace allocation failed\n");
		comp_ctx[LOLELFFS_COMP_ZLIB].available = false;
	}

#if HAVE_ZSTD
	/* Initialize zstd - uses simple API, no workspace needed */
	comp_ctx[LOLELFFS_COMP_ZSTD].workspace = NULL;
	comp_ctx[LOLELFFS_COMP_ZSTD].workspace_size = 0;
	comp_ctx[LOLELFFS_COMP_ZSTD].available = true;
	any_available = true;
	pr_info("lolelffs: zstd compression initialized\n");
#else
	comp_ctx[LOLELFFS_COMP_ZSTD].workspace = NULL;
	comp_ctx[LOLELFFS_COMP_ZSTD].workspace_size = 0;
	comp_ctx[LOLELFFS_COMP_ZSTD].available = false;
	pr_info("lolelffs: zstd compression not available (disabled)\n");
#endif

	if (!any_available) {
		pr_err("lolelffs: no compression algorithms available\n");
		return -ENOMEM;
	}

	return ret;
}

/**
 * lolelffs_comp_exit - Cleanup compression subsystem
 */
void lolelffs_comp_exit(void)
{
	int i;

	pr_info("lolelffs: cleaning up compression support\n");

	for (i = LOLELFFS_COMP_LZ4; i <= LOLELFFS_COMP_MAX_ALGO; i++) {
		if (comp_ctx[i].workspace) {
			vfree(comp_ctx[i].workspace);
			comp_ctx[i].workspace = NULL;
			comp_ctx[i].available = false;
		}
	}
}
