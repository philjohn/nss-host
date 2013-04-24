#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#include <nss_crypto_if.h>
#include <nss_crypto_hlos.h>


#define __DBGFS_READ			0444
#define __DBGFS_RW			0666
#define __HASH_KEY_LEN			NSS_CRYPTO_MAX_KEYLEN_SHA1
#define __CIPHER_KEY_LEN		NSS_CRYPTO_MAX_KEYLEN_AES
#define __ENCR_MEMCMP_SZ		64
/* #define __ENCR_MEMCMP_SZ		256 */
#define __HASH_MEMCMP_SZ		20
#define __ENCR_MAX_DATA_SZ		1536
#define __CRYPTO_RESULTS_SZ		128
#define NSS_CRYPTO_BENCH_DATA_ALIGN 	64

#define NSS_CRYPTO_BENCH_ASSERT(expr) do { \
	if (!(expr)) {	\
		printk("Assertion at - %d, %s\n", __LINE__, #expr); \
		panic("system is going down\n"); \
	} \
} while(0)


static DECLARE_WAIT_QUEUE_HEAD(tx_comp);
static DECLARE_WAIT_QUEUE_HEAD(tx_start);
static struct task_struct *tx_thread;

static struct timeval init_time;
static struct timeval comp_time;
static spinlock_t op_lock;
static nss_crypto_handle_t crypto_hdl;

static struct kmem_cache *crypto_op_zone;

static struct dentry *droot;

static const uint8_t *help = "bench mcmp flush start";

static uint32_t tx_reqs;

static uint8_t auth_key[__HASH_KEY_LEN]= {
	0x01, 0x02, 0x03, 0x04,
	0x05, 0x06, 0x07, 0x08,
	0x09, 0x0a, 0x0b, 0x0c,
	0x0d, 0x0e, 0x0f, 0x01,
	0x02, 0x03, 0x04, 0x05
};

static uint8_t cipher_key[__CIPHER_KEY_LEN] = {
	0x60, 0x3d, 0xeb, 0x10,
	0x15, 0xca, 0x71, 0xbe,
	0x2b, 0x73, 0xae, 0xf0,
	0x85, 0x7d, 0x77, 0x81,
	0x1f, 0x35, 0x2c, 0x07,
	0x3b, 0x61, 0x08, 0xd7,
	0x2d, 0x98, 0x10, 0xa3,
	0x09, 0x14, 0xdf, 0xf4
};

static uint8_t cipher_iv[NSS_CRYPTO_MAX_IVLEN_AES] = {
	0x00, 0x01, 0x02, 0x03,
	0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f
};


#if (__ENCR_MEMCMP_SZ == 64)

static uint8_t plain_text[__ENCR_MEMCMP_SZ] = {
	0x6b, 0xc1, 0xbe, 0xe2,
	0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11,
	0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57,
	0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac,
	0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46,
	0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19,
	0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45,
	0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b,
	0xe6, 0x6c, 0x37, 0x10
};

static uint8_t encr_text[__ENCR_MEMCMP_SZ] = {
	0xf5, 0x8c, 0x4c, 0x04,
	0xd6, 0xe5, 0xf1, 0xba,
	0x77, 0x9e, 0xab, 0xfb,
	0x5f, 0x7b, 0xfb, 0xd6,
        0x9c, 0xfc, 0x4e, 0x96,
	0x7e, 0xdb, 0x80, 0x8d,
	0x67, 0x9f, 0x77, 0x7b,
	0xc6, 0x70, 0x2c, 0x7d,
        0x39, 0xf2, 0x33, 0x69,
	0xa9, 0xd9, 0xba, 0xcf,
	0xa5, 0x30, 0xe2, 0x63,
	0x04, 0x23, 0x14, 0x61,
        0xb2, 0xeb, 0x05, 0xe2,
	0xc3, 0x9b, 0xe9, 0xfc,
	0xda, 0x6c, 0x19, 0x07,
	0x8c, 0x6a, 0x9d, 0x1b
};
static uint8_t sha1_hash[__HASH_MEMCMP_SZ] = {
	0xc9, 0xdd, 0x94, 0xfb,
	0xc8, 0x9f, 0x81, 0x12,
	0x68, 0x1b, 0x8f, 0xfb,
	0xb5, 0xfd, 0x27, 0x69,
	0x76, 0xa1, 0x2e, 0x99
};

#elif (__ENCR_MEMCMP_SZ == 256)
static uint8_t plain_text[__ENCR_MEMCMP_SZ] = {0};

static uint8_t encr_text[__ENCR_MEMCMP_SZ] = {
	0xb7, 0xbf, 0x3a, 0x5d,
	0xf4, 0x39, 0x89, 0xdd,
	0x97, 0xf0, 0xfa, 0x97,
	0xeb, 0xce, 0x2f, 0x4a,
	0xe1, 0xc6, 0x56, 0x30,
	0x5e, 0xd1, 0xa7, 0xa6,
	0x56, 0x38, 0x05, 0x74,
	0x6f, 0xe0, 0x3e, 0xdc,
	0x41, 0x63, 0x5b, 0xe6,
	0x25, 0xb4, 0x8a, 0xfc,
	0x16, 0x66, 0xdd, 0x42,
	0xa0, 0x9d, 0x96, 0xe7,
	0xf7, 0xb9, 0x30, 0x58,
	0xb8, 0xbc, 0xe0, 0xff,
	0xfe, 0xa4, 0x1b, 0xf0,
	0x01, 0x2c, 0xd3, 0x94,
	0x21, 0xdf, 0xa2, 0xcf,
	0x15, 0x47, 0x29, 0x33,
	0x64, 0x9f, 0x5d, 0xd1,
	0x3d, 0xde, 0x5a, 0xc0,
	0xa9, 0xe0, 0x7b, 0xce,
	0xc9, 0x4a, 0xb5, 0xb9,
	0x20, 0x61, 0xab, 0xf1,
	0x4a, 0x57, 0xb8, 0xfe,
	0xf1, 0xd5, 0xd3, 0xae,
	0xa9, 0x4a, 0x05, 0x5a,
	0xde, 0x50, 0x1f, 0x8c,
	0x5d, 0x37, 0x4e, 0x68,
	0xb7, 0xd6, 0x8d, 0xb4,
	0xae, 0xd4, 0x47, 0x0e,
	0xac, 0x03, 0xc2, 0x18,
	0x9b, 0x78, 0xea, 0xca,
	0xe8, 0xdf, 0x42, 0xda,
	0x66, 0x9e, 0x01, 0xbb,
	0x4b, 0x89, 0x2a, 0xb8,
	0x31, 0x87, 0x1a, 0x88,
	0xc0, 0xfd, 0x91, 0x08,
	0x01, 0x03, 0x7d, 0x89,
	0x8c, 0xc1, 0x7a, 0x8d,
	0x72, 0x73, 0xd2, 0x93,
	0x26, 0x07, 0x78, 0x03,
	0x96, 0x87, 0xb5, 0x38,
	0x19, 0x96, 0x9c, 0x06,
	0xa1, 0xaf, 0x8f, 0x9d,
	0xd8, 0xca, 0x42, 0x80,
	0x6c, 0x8d, 0xe6, 0xd7,
	0xa8, 0x5d, 0xbb, 0x82,
	0x03, 0x7a, 0x2b, 0x29,
	0xdb, 0x07, 0x5f, 0xd4,
	0x80, 0x6f, 0xee, 0xc5,
	0x68, 0x30, 0x69, 0xbf,
	0xe8, 0x48, 0x80, 0xd8,
	0x27, 0x75, 0x70, 0x6a,
	0x40, 0xf9, 0x98, 0x73,
	0xa8, 0x1c, 0xae, 0x6d,
	0xbb, 0x8a, 0xbd, 0x0e,
	0xf5, 0xc2, 0xc3, 0x83,
	0x3a, 0xd7, 0x99, 0xcd,
	0xec, 0x6c, 0xec, 0x4b,
	0xab, 0x4c, 0x8a, 0x87,
	0xce, 0xeb, 0x4c, 0x37,
	0x74, 0xab, 0xdb, 0x47,
	0x60, 0x0f, 0x02, 0x39,
	0xbf, 0x3c, 0xef, 0x9c,
};

static uint8_t sha1_hash[__HASH_MEMCMP_SZ] = {
	0xf1, 0x71, 0x4b, 0xb9,
	0xeb, 0x76, 0x21, 0x47,
	0x9e, 0xa0, 0x90, 0x7f,
};

#else
#error "incorrect ENCR_MCMP_SZ"
#endif
static int32_t session_idx;
static uint32_t prep = 0;
static uint8_t pattern_data[__ENCR_MAX_DATA_SZ] = {0};
static uint8_t *data_ptr;

/*
 * Prototypes
 */
static void crypto_bench_done(struct nss_crypto_buf *buf);
static int  crypto_bench_tx(void *arg);

struct crypto_op {
	struct list_head node;

	struct nss_crypto_buf *buf;

	uint8_t *payload;
	uint32_t payload_len;

	uint8_t *data_vaddr;
	uint32_t data_paddr;
	uint32_t data_len;

	uint32_t cipher_skip;
	uint32_t auth_skip;

	uint32_t cipher_len;
	uint32_t auth_len;

	uint8_t *hash_vaddr;
	uint32_t hash_paddr;

	uint8_t *iv_vaddr;
	uint32_t iv_paddr;

};


LIST_HEAD(op_head);

/*
 * Debug interface symbols
 */
enum crypto_bench_type {
	TYPE_BENCH = 0,
	TYPE_MCMP  = 1,
	TYPE_CAL   = 1,
	TYPE_MAX
};

struct crypto_bench_param {
	uint32_t print_mode;	/**< enable prints(=1) or disable prints(=0) */
	uint32_t bench_mode;	/**< run mode bench */
	uint32_t mcmp_mode;	/**< run mode memory compare */

	uint8_t  pattern;	/**< pattern to fill */

	uint32_t bam_len;	/**< size of the data buffer */
	uint32_t cipher_len;	/**< size of cipher operation */
	uint32_t auth_len;	/**< size of auth operation */
	uint32_t hash_len;	/**< size of hash to use for mcmp */
	uint32_t key_len;	/**< cipher key length */

	uint32_t cipher_op;	/**< encrypt(op=1) or decrypt(op=0) */
	uint32_t auth_op;	/**< auth(op=1) or none(op=0) */

	uint32_t num_reqs;	/**< number of requests in "1" pass */
	uint32_t num_loops;	/**< number of loops of num_reqs */
	uint32_t cpu_id;	/**< CPU to run the test from */
	uint32_t mbps;		/**< current throughput */

	uint32_t bam_align;	/**< start data from align boundary */
	uint32_t cipher_skip;	/**< start cipher after skipping */
	uint32_t auth_skip;	/**< start cipher after skipping */

	uint32_t mcmp_encr;	/**< encryption failures in mcmp */
	uint32_t mcmp_hash;	/**< hash match failures in mcmp */
};

/*
 * Add default values here, rest will be zero'ed out
 */
static struct crypto_bench_param def_param = {
	.pattern = 0x33,
	.bam_len = 256,
	.cipher_op = 1,
	.auth_op = 1,
	.bam_align = 4,
	.num_reqs = 10000,
	.hash_len = NSS_CRYPTO_MAX_HASHLEN_SHA1,
	.key_len = 16,
};

static struct crypto_bench_param param;

#if defined (CONFIG_NSS_CRYPTO_TOOL_DBG)
static void crypto_bench_dump_addr(uint8_t *addr, uint32_t len, uint8_t *str)
{
	int i;

	printk("%s:\n", str);

	for (i = 0; i < len; i++) {
		printk("0x%02x,%s", addr[i], (i % 4) ? " " : "\n");
	}
	printk("\n");
}
#else
#define crypto_bench_dump_addr(addr, len, str)
#endif

#define crypto_bench_print(level, fmt, args...)	do { \
	if (param.print_mode >= NSS_CRYPTO_BENCH_PRN_LVL_##level) { \
		printk(fmt, ##args); \
	} \
} while(0)

#define crypto_bench_error(fmt, args...)	crypto_bench_print(ERR, fmt, args)
#define crypto_bench_info(fmt, args...)		crypto_bench_print(INFO, fmt, args)
#define crypto_bench_debug(fmt, args...)	crypto_bench_print(DBG, fmt, args)

static inline uint32_t crypto_bench_align(uint32_t val, uint32_t align)
{
	uint32_t offset;

	offset = val % align;

	return val - offset + align;
}
#define check_n_set(val, def_val)	\
	if ((val) == 0) {	\
		(val) = (def_val);	\
	}

static void crypto_bench_init_param(enum crypto_bench_type type)
{
	switch (type) {
	case TYPE_BENCH:
		param.bench_mode = 1;
		param.mcmp_mode = 0;

		memset(&pattern_data[0], param.pattern, param.bam_len);
		data_ptr = &pattern_data[0];

		break;

	case TYPE_MCMP:
		param.bench_mode = 0;
		param.mcmp_mode = 1;

		param.auth_op = 1;
		param.cipher_op = 1;

		param.bam_len = __ENCR_MEMCMP_SZ;
		param.hash_len = 12;
		param.key_len = __CIPHER_KEY_LEN;

		/* conditional defaults */
		check_n_set(param.auth_len, param.bam_len);

		data_ptr = &plain_text[0];

		break;

	default:
		NSS_CRYPTO_BENCH_ASSERT(type < TYPE_MAX);
	}


	if (param.cpu_id > CONFIG_NR_CPUS) {
		param.cpu_id = 0;
	}

	if (param.bam_len > __ENCR_MAX_DATA_SZ) {
		param.bam_len = __ENCR_MAX_DATA_SZ;
	}

	check_n_set(param.bam_align, NSS_CRYPTO_BENCH_DATA_ALIGN);
}

/*
 * NOTE: Allocating extra 128 bytes to acccomodate result dump
 */
static int32_t crypto_bench_prep_op(void)
{
	struct nss_crypto_key c_key = {0};
	struct nss_crypto_key a_key = {0};
	struct crypto_op *op = NULL;
	uint32_t iv_hash_len;
	int i = 0;

	if (prep) {
		return -1;
	}

	prep = 1;

	c_key.algo	= NSS_CRYPTO_CIPHER_AES;
	c_key.key 	= &cipher_key[0];
	c_key.key_len   = param.key_len;

	if (param.auth_op) {
		a_key.algo    = NSS_CRYPTO_AUTH_SHA1_HMAC;
		a_key.key     = &auth_key[0];
		a_key.key_len = __HASH_KEY_LEN;
	}

	session_idx = nss_crypto_session_alloc(crypto_hdl, &c_key, &a_key);
	NSS_CRYPTO_BENCH_ASSERT(session_idx >= 0);

	printk("preparing crypto bench\n");

	iv_hash_len = NSS_CRYPTO_MAX_IVLEN_AES + __CRYPTO_RESULTS_SZ + sizeof(uint32_t);

	printk("session = %d\n", session_idx);

	for (i = 0; i < param.num_reqs; i++) {

		op = kmem_cache_alloc(crypto_op_zone, GFP_KERNEL);
		NSS_CRYPTO_BENCH_ASSERT(op != NULL);

		memset(op, 0x0, sizeof(struct crypto_op));

		op->data_len = param.bam_len;
		op->cipher_len = param.cipher_len;
		op->auth_len = param.auth_len;

		op->cipher_skip = param.cipher_skip;
		op->auth_skip = param.auth_skip;

		op->payload = kmalloc(op->data_len + param.bam_align, GFP_DMA);
		NSS_CRYPTO_BENCH_ASSERT(op->payload != NULL);

		op->payload_len = op->data_len + param.bam_align;

		op->data_vaddr = (uint8_t *)crypto_bench_align((uint32_t)op->payload, param.bam_align);

		memcpy(op->data_vaddr, data_ptr, op->data_len);

		op->iv_vaddr = kmalloc(iv_hash_len, GFP_DMA);
		NSS_CRYPTO_BENCH_ASSERT(op->iv_vaddr != NULL);

		op->iv_vaddr = (uint8_t *)crypto_bench_align((uint32_t)op->iv_vaddr, sizeof(uint32_t));
		memcpy(op->iv_vaddr, cipher_iv, NSS_CRYPTO_MAX_IVLEN_AES);

		op->hash_vaddr = op->iv_vaddr + NSS_CRYPTO_MAX_IVLEN_AES;
		op->hash_paddr = dma_map_single(NULL, op->hash_vaddr, param.hash_len, DMA_TO_DEVICE);

		list_add_tail(&op->node, &op_head);
	}

	tx_thread = kthread_create(crypto_bench_tx, (void *) &op_head, "crypto_bench");

	kthread_bind(tx_thread, param.cpu_id);

	return 0;
}

static void crypto_bench_flush(void)
{
	struct crypto_op *op;

	prep = 0;

	memcpy(&param, &def_param, sizeof(struct crypto_bench_param));

	while (!list_empty(&op_head)) {
		op = list_first_entry(&op_head, struct crypto_op, node);

		list_del(&op->node);

		kfree(op->payload);

		kmem_cache_free(crypto_op_zone, op);
	}

	nss_crypto_session_free(crypto_hdl, session_idx);

	session_idx = 0;

	kthread_stop(tx_thread);

	param.num_loops = 0;
}

static void crypto_bench_prep_buf(struct crypto_op *op)
{
	struct nss_crypto_buf *buf;

	buf = nss_crypto_buf_alloc(crypto_hdl);
	NSS_CRYPTO_BENCH_ASSERT(buf != NULL);

	buf->cb_ctx = (uint32_t)op;
	buf->cb_fn  = crypto_bench_done;

	buf->req_type |= (param.cipher_op ? NSS_CRYPTO_BUF_REQ_ENCRYPT : NSS_CRYPTO_BUF_REQ_DECRYPT);
	buf->req_type |= (param.auth_op ? NSS_CRYPTO_BUF_REQ_AUTH : 0);

	buf->session_idx = session_idx;

	buf->iv_offset = (uint32_t)op->iv_vaddr;
	buf->data      = op->data_vaddr;
	buf->data_len  = op->data_len;

	buf->cipher_len  = op->cipher_len;
	buf->cipher_skip = op->cipher_skip;

	buf->hash_len    = param.hash_len;
	buf->hash_offset = op->hash_paddr;

	if (param.auth_op) {
		buf->auth_len    = op->auth_len;
		buf->auth_skip   = op->auth_skip;
	}

	op->buf = buf;

	/*buf->ctx[0] = (uint32_t) op->data; *//*XXX*/
}

void crypto_bench_mcmp(void)
{
	struct crypto_op *op;
	struct list_head *ptr;
	uint32_t encr_res, hash_res;

	list_for_each(ptr, &op_head) {
		op = list_entry(ptr, struct crypto_op, node);

		encr_res = memcmp(op->data_vaddr, encr_text, op->data_len);
		param.mcmp_encr = param.mcmp_encr + !!(encr_res);

		if (param.auth_op) {
			hash_res = memcmp(op->hash_vaddr, sha1_hash, param.hash_len);
			param.mcmp_hash = param.mcmp_hash + !!(hash_res);
		}

		memcpy(op->data_vaddr, data_ptr, op->data_len);
	}
}

static int crypto_bench_tx(void *arg)
{
	uint32_t init_usecs, comp_usecs, delta_usecs, mbits;
	struct crypto_op *op;
	struct list_head *ptr;


	for (;;) {

		/* Nothing to do */
		wait_event_interruptible(tx_start, (param.num_loops > 0) || kthread_should_stop());

		if (kthread_should_stop()) {
			break;
		}

		tx_reqs = 0;

		printk("#");

		/* get start time */
		do_gettimeofday(&init_time);

		/**
		 * Request submission
		 */

		list_for_each(ptr, &op_head) {

			op = list_entry(ptr, struct crypto_op, node);

			crypto_bench_prep_buf(op);

			nss_crypto_transform_payload(crypto_hdl, op->buf);
		}

		wait_event_interruptible(tx_comp, (tx_reqs == param.num_reqs));

		/**
		 * Calculate time and output the Mbps
		 */

		init_usecs  = (init_time.tv_sec * 1000 * 1000) + init_time.tv_usec;
		comp_usecs  = (comp_time.tv_sec * 1000 * 1000) + comp_time.tv_usec;
		delta_usecs = comp_usecs - init_usecs;

		mbits   = (tx_reqs * param.bam_len * 8);
		param.mbps = mbits / delta_usecs;

		printk("bench: completed (reqs = %d, size = %d, time = %d, mbps = %d",
				tx_reqs, param.bam_len, delta_usecs, param.mbps);

		if (param.mcmp_mode) {
			crypto_bench_mcmp();
			printk(", encr_fail = %d, hash_fail = %d", param.mcmp_encr, param.mcmp_hash);
		}
		printk(")\n");
	}

	return 0;
}


/*
 * Context should be ATOMIC
 */
static void crypto_bench_done(struct nss_crypto_buf *buf)
{
	struct crypto_op *op;

	op = (struct crypto_op *)buf->cb_ctx;

	tx_reqs++;

	nss_crypto_buf_free(crypto_hdl, op->buf);

	if (param.auth_op) {
		dma_unmap_single(NULL, op->hash_paddr, param.hash_len, DMA_FROM_DEVICE);
	}

	if (tx_reqs == param.num_reqs) {

		do_gettimeofday(&comp_time);

		wake_up_interruptible(&tx_comp);

		param.num_loops--;
	}
}


static ssize_t crypto_bench_cmd_read(struct file *fp, char __user *ubuf, size_t cnt, loff_t *pos)
{
	return simple_read_from_buffer(ubuf, cnt, pos, help, strlen(help));
}


static ssize_t crypto_bench_cmd_write(struct file *fp, const char __user *ubuf, size_t cnt, loff_t *pos)
{
	uint8_t buf[64] = {0};


	cnt = simple_write_to_buffer(buf, sizeof(buf), pos, ubuf, cnt);

	if (!strncmp(buf, "start", strlen("start"))) {	/* start */
		wake_up_process(tx_thread);
	} else if (!strncmp(buf, "flush", strlen("flush"))) {	/* flush */
		crypto_bench_flush();
	} else if (!strncmp(buf, "bench", strlen("bench"))) {	/* bench */
		crypto_bench_init_param(TYPE_BENCH);
		crypto_bench_prep_op();
	} else if (!strncmp(buf, "mcmp", strlen("mcmp"))) {	/* mcmp */
		crypto_bench_init_param(TYPE_MCMP);
		crypto_bench_prep_op();
	} else if (!strncmp(buf, "cal", strlen("cal"))) {	/* mcmp */
		crypto_bench_init_param(TYPE_CAL);
		crypto_bench_prep_op();
	} else {
		printk("<bench>: invalid cmd\n");
	}

	return cnt;
}

static const struct file_operations cmd_ops = {
	.read = crypto_bench_cmd_read,
	.write = crypto_bench_cmd_write,
};

nss_crypto_user_ctx_t crypto_bench_attach(nss_crypto_handle_t crypto)
{
	spin_lock_init(&op_lock);

	crypto_op_zone = kmem_cache_create("crypto_bench", sizeof(struct crypto_op), 0, SLAB_HWCACHE_ALIGN, NULL);

	crypto_hdl  = crypto;

	/* R/W, Hex */
	debugfs_create_x8("pattern", __DBGFS_RW, droot, &param.pattern);
	debugfs_create_x32("encr", __DBGFS_RW, droot, &param.cipher_op);
	debugfs_create_x32("auth", __DBGFS_RW, droot, &param.auth_op);

	/* R/W U32 */
	debugfs_create_u32("cpu_id", __DBGFS_RW, droot, &param.cpu_id);
	debugfs_create_u32("reqs", __DBGFS_RW, droot, &param.num_reqs);
	debugfs_create_u32("loops", __DBGFS_RW, droot, &param.num_loops);
	debugfs_create_u32("print", __DBGFS_RW, droot, &param.print_mode);

	debugfs_create_u32("bam_len", __DBGFS_RW, droot, &param.bam_len);
	debugfs_create_u32("cipher_len", __DBGFS_RW, droot, &param.cipher_len);
	debugfs_create_u32("auth_len", __DBGFS_RW, droot, &param.auth_len);
	debugfs_create_u32("hash_len", __DBGFS_RW, droot, &param.hash_len);
	debugfs_create_u32("key_len", __DBGFS_RW, droot, &param.key_len);

	debugfs_create_u32("bam_align", __DBGFS_RW, droot, &param.bam_align);
	debugfs_create_u32("cipher_skip", __DBGFS_RW, droot, &param.cipher_skip);
	debugfs_create_u32("auth_skip", __DBGFS_RW, droot, &param.auth_skip);

	/* R/W buffer */
	debugfs_create_file("cmd", __DBGFS_RW, droot, &op_head, &cmd_ops);


	/* Read, U32 */
	debugfs_create_u32("mbps", __DBGFS_READ, droot, &param.mbps);
	debugfs_create_u32("bench", __DBGFS_READ, droot, &param.bench_mode);
	debugfs_create_u32("mcmp", __DBGFS_READ, droot, &param.mcmp_mode);
	debugfs_create_u32("hash_fail", __DBGFS_READ, droot, &param.mcmp_hash);
	debugfs_create_u32("encr_fail", __DBGFS_READ, droot, &param.mcmp_encr);
	debugfs_create_u32("comp", __DBGFS_READ, droot, &tx_reqs);

	return (nss_crypto_user_ctx_t)&op_head;
}

void crypto_bench_detach(nss_crypto_user_ctx_t ctx)
{
	crypto_bench_flush();
	kmem_cache_destroy(crypto_op_zone);
}

int crypto_bench_init(void)
{
	printk("Crypto bench loaded\n");

	droot = debugfs_create_dir("crypto_bench", NULL);

	nss_crypto_register_user(crypto_bench_attach, crypto_bench_detach);

	return 0;
}

void __exit
crypto_bench_exit(void)
{
	printk("Crypto bench unloaded\n");
	nss_crypto_unregister_user(crypto_hdl);
}

module_init(crypto_bench_init);
module_exit(crypto_bench_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("QCA NSS Crypto driver");
MODULE_AUTHOR("Qualcomm Atheros Inc");
