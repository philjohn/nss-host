/* Copyright (c) 2013, Qualcomm Atheros Inc.
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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/memory.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <mach/msm_iomap.h>
#include <mach/msm_nss_crypto.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <nss_crypto_hlos.h>
#include <nss_crypto_if.h>
#include <nss_crypto_hw.h>
#include <nss_crypto_ctrl.h>
#include <nss_crypto_data.h>


#define REG(off)	(MSM_CLK_CTL_BASE + (off))
#define REG_GCC(off)	(MSM_APCS_GCC_BASE + (off))

#define CRYPTO_RESET_ENG(n)	REG(0x3E00 + (n * 0x4))
#define CRYPTO_RESET_AHB	REG(0x3E10)

#define CRYPTO_RESET_ENG_0	REG(0x3E00)
#define CRYPTO_RESET_ENG_1	REG(0x3E04)
#define CRYPTO_RESET_ENG_2	REG(0x3E08)
#define CRYPTO_RESET_ENG_3	REG(0x3E0C)

extern struct nss_crypto_ctrl gbl_crypto_ctrl;

static int eng_count = 0;
static int res_idx = 0;
module_param(res_idx, int, 0);
MODULE_PARM_DESC(res_idx, "reserve indexes");

void nss_crypto_engine_init(uint32_t eng_count);
void nss_crypto_init(void);

/*
 * nss_crypto_bam_init()
 * 	initialize the BAM for the given engine
 */
static void nss_crypto_bam_init(uint8_t *bam_iobase)
{
	uint32_t cfg_bits;
	uint32_t ctrl_reg;

	ctrl_reg = ioread32(bam_iobase + CRYPTO_BAM_CTRL);

	ctrl_reg |= CRYPTO_BAM_CTRL_SW_RST;
	iowrite32(ctrl_reg, bam_iobase + CRYPTO_BAM_CTRL);

	ctrl_reg &= ~CRYPTO_BAM_CTRL_SW_RST;
	iowrite32(ctrl_reg, bam_iobase + CRYPTO_BAM_CTRL);

	ctrl_reg |= CRYPTO_BAM_CTRL_BAM_EN;
	iowrite32(ctrl_reg, bam_iobase + CRYPTO_BAM_CTRL);

	iowrite32(CRYPTO_BAM_DESC_CNT_TRSHLD_VAL, bam_iobase +  CRYPTO_BAM_DESC_CNT_TRSHLD);

	/* disabling this is recommended from H/W specification*/
	cfg_bits = ~((uint32_t)CRYPTO_BAM_CNFG_BITS_BAM_FULL_PIPE);
	iowrite32(cfg_bits, bam_iobase + CRYPTO_BAM_CNFG_BITS);
}

/*
 * nss_crypto_probe()
 * 	probe routine called per engine from MACH-MSM
 */
static int nss_crypto_probe(struct platform_device *pdev)
{
	struct nss_crypto_ctrl_eng *e_ctrl;
	struct nss_crypto_platform_data *res;
	int status = 0;

	nss_crypto_info("probing engine - %d\n", eng_count);
	nss_crypto_assert(eng_count < NSS_CRYPTO_ENGINES);

	e_ctrl = &gbl_crypto_ctrl.eng[eng_count];

	e_ctrl->dev = &pdev->dev;

	/* crypto engine resources */
	res = dev_get_platdata(e_ctrl->dev);
	nss_crypto_assert(res);

	e_ctrl->bam_ee = res->bam_ee;

	e_ctrl->cmd_base = res->crypto_pbase;
	e_ctrl->crypto_base = ioremap_nocache(res->crypto_pbase, res->crypto_pbase_sz);
	nss_crypto_assert(e_ctrl->crypto_base);

	e_ctrl->bam_pbase = res->bam_pbase;
	e_ctrl->bam_base = ioremap_nocache(res->bam_pbase, res->bam_pbase_sz);
	nss_crypto_assert(e_ctrl->bam_base);

	/*
	 * Link address of engine ctrl
	 */
	platform_set_drvdata(pdev, e_ctrl);

	/*
	 * intialize the BAM and the engine
	 */
	nss_crypto_bam_init(e_ctrl->bam_base);
	nss_crypto_engine_init(eng_count);

	eng_count++;
	gbl_crypto_ctrl.num_eng = eng_count;

	return status;
}

/*
 * nss_crypto_remove()
 * 	remove the crypto engine and deregister everything
 */
static int nss_crypto_remove(struct platform_device *pdev)
{
	struct nss_crypto_ctrl_eng *ctrl;

	ctrl = platform_get_drvdata(pdev);

	/**
	 * XXX: pipe deinit goes here
	 */
	return 0;
};

/*
 * platform device instance
 */
static struct platform_driver nss_crypto_drv = {
	.probe  	= nss_crypto_probe,
	.remove 	= nss_crypto_remove,
	.driver 	= {
		.owner  = THIS_MODULE,
		.name   = "nss-crypto",
	},
};

/*
 * nss_crypto_module_exit()
 * 	module exit for crypto driver
 */
static void __exit nss_crypto_module_exit(void)
{
	nss_crypto_info("module unloaded (IPQ806x)\n");

	platform_driver_unregister(&nss_crypto_drv);
}

/*
 * nss_crypto_module_init()
 * 	module init for crypto driver
 */
static int __init nss_crypto_module_init(void)
{
	uint32_t status = 0;

	nss_crypto_info("module loaded (platform - IPQ806x, build - %s:%s)\n", __DATE__, __TIME__);

	/* nss_crypto_debugfs = debugfs_create_dir("nss_crypto", NULL); */

	/*
	 * bring the crypto out of reset
	 */
	iowrite32(0, CRYPTO_RESET_ENG(0));
	iowrite32(0, CRYPTO_RESET_ENG(1));
	iowrite32(0, CRYPTO_RESET_ENG(2));
	iowrite32(0, CRYPTO_RESET_ENG(3));

	iowrite32(0, CRYPTO_RESET_AHB);

	nss_crypto_init();

	/*
	 * reserve the index if certain pipe pairs are locked out for
	 * trust zone use
	 */
	gbl_crypto_ctrl.idx_bitmap = res_idx ? ((0x1 << res_idx) - 1) : 0;

	status = platform_driver_register(&nss_crypto_drv);
	nss_crypto_assert(status == 0);

	return 0;
}

module_init(nss_crypto_module_init);
module_exit(nss_crypto_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("QCA NSS Crypto driver");
MODULE_AUTHOR("Qualcomm Atheros Inc");
