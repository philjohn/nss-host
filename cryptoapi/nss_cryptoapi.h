/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 *
 */

#ifndef __NSS_CRYPTOAPI_H
#define __NSS_CRYPTOAPI_H

#define nss_cryptoapi_sg_has_frags(s) sg_next(s)

/**
 * nss_cryptoapi.h
 * 	Cryptoapi (Linux Crypto API framework) specific nss cfi header file
 */

/*
 *  @brief Cryptoapi sg virtual addresses
 * 	used during Encryption/Decryption operations.
 */
struct nss_cryptoapi_addr {
	uint8_t *src;
	uint8_t *dst;
	uint8_t *assoc;
	uint8_t *iv;
	uint8_t *start;
};

/**
 * @brief Framework specific handle this will be used to communicate framework
 *		specific data to Core specific data
 */
struct nss_cryptoapi {
	nss_crypto_handle_t crypto;		/**< crypto handle */
	struct dentry *root_dentry;
	struct dentry *stats_dentry;
};

struct nss_cryptoapi_ctx {
	uint64_t queued;
	uint64_t completed;
	uint32_t sid;
	unsigned int authsize;
	enum nss_crypto_cipher cip_alg;
	enum nss_crypto_auth auth_alg;
	enum nss_crypto_req_type op;
	struct dentry *session_dentry;
	atomic_t refcnt;
	uint16_t magic;
	uint8_t ctx_iv[AES_BLOCK_SIZE];
	uint16_t rsvd;
};

/**
 *  * @brief key lengths for supported algorithms
 **/
enum nss_cryptoapi_keylen {
	NSS_CRYPTOAPI_KEYLEN_AES128 = 16,
	NSS_CRYPTOAPI_KEYLEN_AES256 = 32,
	NSS_CRYPTOAPI_KEYLEN_3DES = 24,
};

#define NSS_CRYPTOAPI_DEBUGFS_NAME_SZ 64

#define CRYPTOAPI_MAX_KEY_SIZE 64
#define NSS_CRYPTOAPI_MAGIC 0x1fff

static inline void nss_cryptoapi_verify_magic(struct nss_cryptoapi_ctx *ctx)
{
	BUG_ON(unlikely(ctx->magic != NSS_CRYPTOAPI_MAGIC));
}

static inline void nss_cryptoapi_set_magic(struct nss_cryptoapi_ctx *ctx)
{
	ctx->magic = NSS_CRYPTOAPI_MAGIC;
}

static inline void nss_cryptoapi_clear_magic(struct nss_cryptoapi_ctx *ctx)
{
	ctx->magic = 0;
}

static inline bool nss_cryptoapi_is_decrypt(struct nss_cryptoapi_ctx *ctx)
{
	return ctx->op & NSS_CRYPTO_REQ_TYPE_DECRYPT;
}

static inline uint8_t *nss_cryptoapi_get_buf_addr(uint8_t *addr_0, uint8_t *addr_1)
{
	return (addr_0 < addr_1) ? addr_0 : addr_1;
}

static inline uint8_t nss_cryptoapi_get_skip(uint8_t *addr, uint8_t *start)
{
	return addr - start;
}

static inline uint32_t nss_cryptoapi_get_hmac_sz(struct aead_request *req)
{
	return crypto_aead_authsize(crypto_aead_reqtfm(req));
}

static inline uint32_t nss_cryptoapi_get_blocksize(struct aead_request *req)
{
	return crypto_aead_blocksize(crypto_aead_reqtfm(req));
}

static inline uint32_t nss_cryptoapi_get_iv_sz(struct aead_request *req)
{
	return crypto_aead_ivsize(crypto_aead_reqtfm(req));
}

/*
 * nss_cryptoapi_check_unalign()
 * 	Cryptoapi verify if length is aligned to boundary.
 */
static inline bool nss_cryptoapi_check_unalign(uint32_t len, uint32_t boundary)
{
	return !!(len & (boundary - 1));
}

/*
 * function prototypes
 */

/* Debug fs */
void nss_cryptoapi_debugfs_add_stats(struct dentry *parent, struct nss_cryptoapi_ctx *session_ctx);
void nss_cryptoapi_debugfs_add_session(struct nss_cryptoapi *gbl_ctx, struct nss_cryptoapi_ctx *session_ctx);
void nss_cryptoapi_debugfs_del_session(struct nss_cryptoapi_ctx *session_ctx);
void nss_cryptoapi_debugfs_init(struct nss_cryptoapi *gbl_ctx);
void nss_cryptoapi_debugfs_exit(struct nss_cryptoapi *gbl_ctx);

/* AEAD */
int nss_cryptoapi_aead_init(struct crypto_tfm *tfm);
void nss_cryptoapi_aead_exit(struct crypto_tfm *tfm);
int nss_cryptoapi_sha1_aes_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen);
int nss_cryptoapi_sha256_aes_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen);
int nss_cryptoapi_sha1_3des_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen);
int nss_cryptoapi_sha256_3des_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen);
int nss_cryptoapi_aead_setauthsize(struct crypto_aead *authenc, unsigned int authsize);
int nss_cryptoapi_sha1_aes_encrypt(struct aead_request *req);
int nss_cryptoapi_sha256_aes_encrypt(struct aead_request *req);
int nss_cryptoapi_sha1_3des_encrypt(struct aead_request *req);
int nss_cryptoapi_sha256_3des_encrypt(struct aead_request *req);
int nss_cryptoapi_sha1_aes_decrypt(struct aead_request *req);
int nss_cryptoapi_sha256_aes_decrypt(struct aead_request *req);
int nss_cryptoapi_sha1_3des_decrypt(struct aead_request *req);
int nss_cryptoapi_sha256_3des_decrypt(struct aead_request *req);
int nss_cryptoapi_sha1_aes_geniv_encrypt(struct aead_givcrypt_request *req);
int nss_cryptoapi_sha256_aes_geniv_encrypt(struct aead_givcrypt_request *req);
int nss_cryptoapi_sha1_3des_geniv_encrypt(struct aead_givcrypt_request *req);
int nss_cryptoapi_sha256_3des_geniv_encrypt(struct aead_givcrypt_request *req);

/* ablkcipher */
int nss_cryptoapi_ablkcipher_init(struct crypto_tfm *tfm);
void nss_cryptoapi_ablkcipher_exit(struct crypto_tfm *tfm);
int nss_cryptoapi_aes_cbc_setkey(struct crypto_ablkcipher *cipher, const u8 *key, unsigned int len);
int nss_cryptoapi_3des_cbc_setkey(struct crypto_ablkcipher *cipher, const u8 *key, unsigned int len);
int nss_cryptoapi_aes_cbc_encrypt(struct ablkcipher_request *req);
int nss_cryptoapi_aes_cbc_decrypt(struct ablkcipher_request *req);
int nss_cryptoapi_3des_cbc_encrypt(struct ablkcipher_request *req);
int nss_cryptoapi_3des_cbc_decrypt(struct ablkcipher_request *req);

#endif /* __NSS_CRYPTOAPI_H */

