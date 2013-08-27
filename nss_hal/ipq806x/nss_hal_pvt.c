/*
 **************************************************************************
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

/**
 * nss_hal_pvt.c
 *	NSS HAL private APIs.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include "nss_hal_pvt.h"
#include "nss_clocks.h"

/*
 * clk_reg_write_32()
 *	Write clock register
 */
static inline void clk_reg_write_32(void *addr, uint32_t val)
{
	writel(val, addr);
}

/*
 * clk_reg_read_32()
 *	Write clock register
 */
static inline uint32_t clk_reg_read_32(volatile void *addr)
{
	return readl(addr);
}

/*
 * nss_hal_pvt_pll_change
 *	Change the Pll between 11(400mhz) or 18(1066 or 1466)
 */
void nss_hal_pvt_pll_change(uint32_t pll)
{
	uint32_t ns_reg0;
	uint32_t ns_reg1;

	uint32_t pll11_mask = 0x3;
	uint32_t pll18_mask = 0x1;

	uint32_t pll_cl_mask = 0x7;


	printk("Picking PLL%d\n", pll);

	if (pll == 11) {
		ns_reg0 = clk_reg_read_32(UBI32_COREn_CLK_SRC0_NS(0));
		ns_reg1 = clk_reg_read_32(UBI32_COREn_CLK_SRC1_NS(1));

		ns_reg0 &= ~pll_cl_mask;
		ns_reg1 &= ~pll_cl_mask;

		ns_reg0 |= pll11_mask;
		ns_reg1 |= pll11_mask;

		clk_reg_write_32(UBI32_COREn_CLK_SRC0_NS(0), ns_reg0);
		clk_reg_write_32(UBI32_COREn_CLK_SRC0_NS(1), ns_reg1);
		

	} else if (pll == 18) {
		ns_reg0 = clk_reg_read_32(UBI32_COREn_CLK_SRC0_NS(0));
		ns_reg1 = clk_reg_read_32(UBI32_COREn_CLK_SRC1_NS(1));

		ns_reg0 &= ~pll_cl_mask;
		ns_reg1 &= ~pll_cl_mask;

		ns_reg0 |= pll18_mask;
		ns_reg1 |= pll18_mask;

		clk_reg_write_32(UBI32_COREn_CLK_SRC0_NS(0), ns_reg0);
		clk_reg_write_32(UBI32_COREn_CLK_SRC0_NS(1), ns_reg1);

	}

	return;
}

/*
 * nss_hal_pvt_divide_pll
 *	Divide PLL by int val
 */
uint32_t nss_hal_pvt_divide_pll(uint32_t core_id, uint32_t pll, uint32_t divider)
{
	uint32_t ns_mask 	= 0x00ff01ff;
	uint32_t ns_mask_1	= 0x00ff0001;
	uint32_t ns_mask_2	= 0x00fe0141;
	uint32_t ns_mask_5 	= 0x00fb0141;
	uint32_t ns_reg0;
	uint32_t ns_reg1;

	uint32_t md_mask 	= 0x00ff00ff;
	uint32_t md_mask_2	= 0x000100fd;
	uint32_t md_mask_5 	= 0x000100fa;
	uint32_t md_reg0;
	uint32_t md_reg1;

#if (NSS_DEBUG_LEVEL > 0)
	printk("NSSFB0_CLK_SRC_CTL  : %x\n", clk_reg_read_32(NSSFB0_CLK_SRC_CTL));
	printk("NSSFB1_CLK_SRC_CTL  : %x\n", clk_reg_read_32(NSSFB1_CLK_SRC_CTL));
	printk("NSSFB0_CLK_SRC0_NS  : %x\n", clk_reg_read_32(NSSFB0_CLK_SRC0_NS));
	printk("NSSFB0_CLK_SRC1_NS  : %x\n", clk_reg_read_32(NSSFB0_CLK_SRC1_NS));
	printk("NSSFB1_CLK_SRC0_NS  : %x\n", clk_reg_read_32(NSSFB1_CLK_SRC0_NS));
	printk("NSSFB1_CLK_SRC1_NS  : %x\n", clk_reg_read_32(NSSFB1_CLK_SRC1_NS));
	printk("PLL_ENA_NSS	    : %x\n", clk_reg_read_32(PLL_ENA_NSS));
	printk("\n");
	printk("PLL18_L_VAL  : %x\n", clk_reg_read_32(PLL18_L_VAL));
	printk("PLL18_M_VAL  : %x\n", clk_reg_read_32(PLL18_M_VAL));
	printk("PLL18_N_VAL  : %x\n", clk_reg_read_32(PLL18_N_VAL));
	printk("PLL18_CONFIG : %x\n", clk_reg_read_32(PLL18_CONFIG));
	printk("PLL18_TEST_CTL: %x\n", clk_reg_read_32(PLL18_TEST_CTL));
	printk("\n");
	printk("UBI32_COREn_CLK_SRC0_CTL Core 0: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC_CTL(0)));
	printk("UBI32_COREn_CLK_SRC0_CTL Core 1: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC_CTL(1)));
	printk("UBI32_COREn_CLK_SRC0_NS Core 0: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC0_NS(0)));
	printk("UBI32_COREn_CLK_SRC0_NS Core 1: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC0_NS(1)));
	printk("UBI32_COREn_CLK_SRC0_MD Core 0: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC0_MD(0)));
	printk("UBI32_COREn_CLK_SRC0_MD Core 1: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC0_MD(1)));
	printk("\n\n\n");
#endif

	md_reg0 = clk_reg_read_32(UBI32_COREn_CLK_SRC0_MD(0));
	md_reg1 = clk_reg_read_32(UBI32_COREn_CLK_SRC0_MD(1));
	ns_reg0 = clk_reg_read_32(UBI32_COREn_CLK_SRC0_NS(0));
	ns_reg1 = clk_reg_read_32(UBI32_COREn_CLK_SRC0_NS(1));

	/*
	 * Bypass
	 */
	if (divider == 1) {
		printk("Bypass PLL Output\n");

		/*
		 * Clear M and D ( Not2*D ) and Set Bits
		 */

		md_reg0 &= ~md_mask;
		md_reg1 &= ~md_mask;

		/*
		 * PLL Source/ Pre Divide/ Counter Mode/ Counter Reset/ Counter Enable/ N Value
		 */

		ns_reg0 &= ~ns_mask;
		ns_reg1 &= ~ns_mask;

		ns_reg0 |= ns_mask_1;
		ns_reg1 |= ns_mask_1;
	} else if (divider == 2) {

		printk("Divide PLL Output by 2\n");

		/*
		 * Clear M and D ( Not2*D ) and Set Bits
		 */

		md_reg0 &= ~md_mask;
		md_reg1 &= ~md_mask;

		md_reg0 |= md_mask_2;
		md_reg1 |= md_mask_2;

		/*
		 * PLL Source/ Pre Divide/ Counter Mode/ Counter Reset/ Counter Enable/ N Value
		 */

		ns_reg0 &= ~ns_mask;
		ns_reg1 &= ~ns_mask;

		ns_reg0 |= ns_mask_2;
		ns_reg1 |= ns_mask_2;
	} else if (divider == 5) {

		printk("Divide PLL Output by 5\n");

		/*
		 * Clear M and D ( Not2*D ) and Set Bits
		 */

		md_reg0 &= ~md_mask;
		md_reg1 &= ~md_mask;

		md_reg0 |= md_mask_5;
		md_reg1 |= md_mask_5;

		/*
		 * PLL Source/ Pre Divide/ Counter Mode/ Counter Reset/ Counter Enable/ N Value
		 */

		ns_reg0 &= ~ns_mask;
		ns_reg1 &= ~ns_mask;

		ns_reg0 |= ns_mask_5;
		ns_reg1 |= ns_mask_5;
	}

	nss_hal_pvt_pll_change(pll);

	clk_reg_write_32(UBI32_COREn_CLK_SRC0_MD(0), md_reg0);
	clk_reg_write_32(UBI32_COREn_CLK_SRC0_MD(1), md_reg1);
	clk_reg_write_32(UBI32_COREn_CLK_SRC0_NS(0), ns_reg0);
	clk_reg_write_32(UBI32_COREn_CLK_SRC0_NS(1), ns_reg1);

#if (NSS_DEBUG_LEVEL > 0)
	printk("UBI32_COREn_CLK_SRC0_CTL Core 0: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC_CTL(0)));
	printk("UBI32_COREn_CLK_SRC0_CTL Core 1: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC_CTL(1)));
	printk("UBI32_COREn_CLK_SRC0_NS Core 0: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC0_NS(0)));
	printk("UBI32_COREn_CLK_SRC0_NS Core 1: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC0_NS(1)));
	printk("UBI32_COREn_CLK_SRC0_MD Core 0: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC0_MD(0)));
	printk("UBI32_COREn_CLK_SRC0_MD Core 1: %x\n", clk_reg_read_32(UBI32_COREn_CLK_SRC0_MD(1)));
#endif

	return 1;
}

/*
 * nss_hal_pvt_enable_pll18()
 *	Enable PLL18
 */
uint32_t nss_hal_pvt_enable_pll18(uint32_t speed)
{
	uint32_t wait_cycles = 100;

	/*
	 * Prevent Compiler from commenting out the loop.
	 */
	volatile uint32_t value;
	volatile uint32_t mask = (1 << 2);

	/*
	 * Start with clean slate
	 */
	clk_reg_write_32(PLL18_MODE, 0);

	/*
	 * Effective VCO Frequency = 1100 MHz Post Divide 2
	 */
	if (speed == 1100) {
		clk_reg_write_32(PLL18_L_VAL, 0x4000042C);
		clk_reg_write_32(PLL18_M_VAL, 0x0);
		clk_reg_write_32(PLL18_N_VAL, 0x1);

		/*
		 * PLL configuration (as provided by HW team)
		 */
		clk_reg_write_32(PLL18_CONFIG, 0x01495625);
		clk_reg_write_32(PLL18_TEST_CTL, 0x00003080);
	} else if (speed == 1466) {
		/*
		 * Effective VCO Frequency = 1466 MHz Post Divide 2
		 */

		clk_reg_write_32(PLL18_L_VAL, 0x4000043A);
		clk_reg_write_32(PLL18_M_VAL, 0x10);
		clk_reg_write_32(PLL18_N_VAL, 0x19);

		/*
		 * PLL configuration (as provided by HW team)
		 */
		clk_reg_write_32(PLL18_CONFIG, 0x014B5625);
		clk_reg_write_32(PLL18_TEST_CTL, 0x00003080);
	}

	/*
	 * Enable PLL18 output (sequence provided by HW team)
	 */
	clk_reg_write_32(PLL18_MODE, 0x2);
	mdelay(1);
	clk_reg_write_32(PLL18_MODE, 0x6);
	clk_reg_write_32(PLL18_MODE, 0x7);

	/*
	 * Enable NSS Vote for PLL18.
	 */
	clk_reg_write_32(PLL_ENA_NSS, mask);
	do {
		value = clk_reg_read_32(PLL_LOCK_DET_STATUS);
		if (value & mask) {
			return PLL_LOCKED;
		}

		mdelay(1);
	} while (wait_cycles-- > 0);

	return PLL_NOT_LOCKED;
}


/*
 * __nss_hal_common_reset
 *	Do reset/clock configuration common to all cores
 */
void __nss_hal_common_reset(uint32_t *clk_src)
{
	uint32_t i;
	uint32_t pll18_status;

#if defined(NSS_ENABLE_CLK)

	/*
	 * NSS FPB CLOCK
	 */

	/*
	 * Enable clock root and Divider 0
	 * NOTE: Default value is good so no work here
	 */

	/*
	 * PLL0 (800 MHZ). SRC_SEL is 2 (3'b010)
	 * src_div selected is Div-6 (4'b0101).
	 *
	 * Effective frequency (Divider 0) = 133 MHz
	 */
	clk_reg_write_32(NSSFPB_CLK_SRC0_NS, 0x2a);

	/*
	 * Enable clock branch
	 */
	clk_reg_write_32(NSSFPB_CLK_CTL, 0x50);

	/*
	 * NSS FABRIC0 CLOCK
	 */

	/*
	 * Enable clock root and Divider 0
	 * NOTE: Default value is good so no work here
	 */

	/*
	 * PLL0 (800 MHZ) and div is set to 2.
	 * Effective frequency = 400 MHZ.
	 */
	clk_reg_write_32(NSSFB0_CLK_SRC0_NS, 0x0a);

	/*
	 * NSS Fabric0 Branch and dynamic clock gating enabled.
	 */
	clk_reg_write_32(NSSFB0_CLK_CTL, 0x50);

	/*
	 * Enable clock root and Divider 0
	 * NOTE: Default value is good so no work here
	 */

	/*
	 * PLL0 (800 MHZ) and div is set to 4.
	 * Effective frequency = 200 MHZ.
	 */
	clk_reg_write_32(NSSFB1_CLK_SRC0_NS, 0x1a);

	/*
	 * NSS Fabric1 Branch enable and fabric clock gating enabled.
	 */
	clk_reg_write_32(NSSFB1_CLK_CTL, 0x50);

	/*
	 * NSS TCM CLOCK
	*/

	/*
	 * Enable NSS TCM clock root source and select divider 0.
	 *
	 * NOTE: Default value is not good here
	 */
	clk_reg_write_32(NSSTCM_CLK_SRC_CTL, 0x2);

	/*
	 * PLL0 (800 MHZ) and div is set to 2.
	 * Effective frequency = 400 MHZ
	 */
	clk_reg_write_32(NSSTCM_CLK_SRC0_NS, 0xa);

	/*
	 * NSS TCM Branch enable and fabric clock gating enabled.
	 */
	clk_reg_write_32(NSSTCM_CLK_CTL, 0x50);

	/*
	 * Enable global NSS clock branches.
	 * NSS global Fab Branch enable and fabric clock gating enabled.
	 */
	clk_reg_write_32(NSSFAB_GLOBAL_BUS_NS, 0xf);

	/*
	 * Send reset interrupt to NSS
	 */
	clk_reg_write_32(NSS_RESET, 0x0);

	/*
	 * Enable PLL18
	 */
	pll18_status = nss_hal_pvt_enable_pll18();
	if (!pll18_status) {
		/*
		 * Select alternate good source (Src1/pll0)
		 */
		*clk_src = NSS_REGS_CLK_SRC_ALTERNATE;
		return;
	}

	/*
	 * Select default source (Src0/pll18)
	 */
	*clk_src = NSS_REGS_CLK_SRC_DEFAULT;
#endif
	pll18_status = nss_hal_pvt_enable_pll18(1100);

	if (!pll18_status) {
		/*
		 * Select alternate good source (Src1/pll0)
		 */
		printk("Enable PLL18 Failed, Using Alternate");
		*clk_src = NSS_REGS_CLK_SRC_ALTERNATE;
	} else {
		nss_hal_pvt_divide_pll(0, 18, 1);
	}

	/*
	 * Attach debug interface to TLMM
	 */
	nss_write_32((uint32_t)MSM_NSS_FPB_BASE, NSS_REGS_FPB_CSR_CFG_OFFSET, 0x360);

	/*
	 * Clear TCM memory
	 */
	for (i = 0; i < IPQ806X_NSS_TCM_SIZE; i += 4) {
		nss_write_32((uint32_t)MSM_NSS_TCM_BASE, i, 0);
	}

	return;
}

/*
 * __nss_hal_core_reset
 */
void __nss_hal_core_reset(uint32_t core_id, uint32_t map, uint32_t addr, uint32_t clk_src)
{
#if defined(NSS_ENABLE_CLOCK)
	/*
	 * Enable mpt clock
	 */
	clk_reg_write_32(UBI32_MPT0_CLK_CTL, 0x10);

	/*
	 * UBI coren clock root enable
	 */
	if (clk_src == NSS_REGS_CLK_SRC_DEFAULT) {
		/* select Src0 */
		clk_reg_write_32(UBI32_COREn_CLK_SRC_CTL(core_id), 0x02);
	} else {
		/* select Src1 */
		clk_reg_write_32(UBI32_COREn_CLK_SRC_CTL(core_id), 0x03);
	}

	/*
	 * Src0: Bypass M value configuration.
	 */

	/*
	 * Src1: M val is 0x01 and NOT_2D value is 0xfd, 400 MHz with PLL0.
	 */
	clk_reg_write_32(UBI32_COREn_CLK_SRC1_MD(core_id), 0x100fd);

	/*
	 * Bypass, pll18
	 * Effective frequency = 550 MHz
	 */
	clk_reg_write_32(UBI32_COREn_CLK_SRC0_NS(core_id), 0x00000001);

	/*
	 * Dual edge, pll0, NOT(N_M) = 0xfe.
	 * Effective frequency = 400 MHz
	 */
	clk_reg_write_32(UBI32_COREn_CLK_SRC1_NS(core_id), 0x00fe0142);

	/*
	 * UBI32 coren clock control branch.
	 */
	clk_reg_write_32(UBI32_COREn_CLK_FS(core_id), 0x4f);

	/*
	 * UBI32 coren clock control branch.
	 */
	clk_reg_write_32(UBI32_COREn_CLK_CTL(core_id), 0x10);
#endif
	/*
	 * Remove UBI32 reset clamp
	 */
	clk_reg_write_32(UBI32_COREn_RESET_CLAMP(core_id), 0xB);

	/*
	 * Busy wait for few cycles
	 */
	mdelay(1);

	/*
	 * Remove UBI32 core clamp
	 */
	clk_reg_write_32(UBI32_COREn_RESET_CLAMP(core_id), 0x3);

	mdelay(1);

	/*
	 * Remove UBI32 AHB reset
	 */
	clk_reg_write_32(UBI32_COREn_RESET_CLAMP(core_id), 0x1);

	mdelay(1);

	/*
	 * Remove UBI32 AXI reset
	 */
	clk_reg_write_32(UBI32_COREn_RESET_CLAMP(core_id), 0x0);

	mdelay(1);

	/*
	* Apply ubi32 core reset
	*/
	nss_write_32(map, NSS_REGS_RESET_CTRL_OFFSET, 1);

	/*
	 * Program address configuration
	 */
	nss_write_32(map, NSS_REGS_CORE_AMC_OFFSET, 1);
	nss_write_32(map, NSS_REGS_CORE_BAR_OFFSET, 0x3c000000);
	nss_write_32(map, NSS_REGS_CORE_BOOT_ADDR_OFFSET, addr);

	/*
	 * Crypto, GMAC and C2C interrupts are level sensitive
	 */
	nss_write_32(map, NSS_REGS_CORE_INT_STAT2_TYPE_OFFSET, 0xFFFF);
	nss_write_32(map, NSS_REGS_CORE_INT_STAT3_TYPE_OFFSET, 0x3FC000);

	/*
	 * Set IF check value
	 */
	 nss_write_32(map, NSS_REGS_CORE_IFETCH_RANGE_OFFSET, 0xBF004001);

	/*
	 * De-assert ubi32 core reset
	 */
	nss_write_32(map, NSS_REGS_RESET_CTRL_OFFSET, 0);
}
