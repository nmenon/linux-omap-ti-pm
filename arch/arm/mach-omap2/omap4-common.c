/*
 * OMAP4 specific common source file.
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Author:
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/cacheflush.h>

#include <mach/hardware.h>
#include <mach/omap4-common.h>
#include <mach/omap-wakeupgen.h>

#include "omap4-sar-layout.h"
#include "clockdomain.h"

#ifdef CONFIG_CACHE_L2X0
#define L2X0_POR_OFFSET_VALUE		0x9
static void __iomem *l2cache_base;
#endif

static void __iomem *gic_dist_base_addr;
static void __iomem *gic_cpu_base;
static void __iomem *sar_ram_base;
static struct clockdomain *l4_secure_clkdm;
static void *dram_barrier_base;

static void omap_bus_sync_noop(void)
{ }

struct omap_bus_post_fns omap_bus_post = {
	.sync = omap_bus_sync_noop,
};
EXPORT_SYMBOL(omap_bus_post);

void __iomem *omap4_get_gic_dist_base(void)
{
	return gic_dist_base_addr;
}

void __iomem *omap4_get_gic_cpu_base(void)
{
	return gic_cpu_base;
}

void *omap_get_dram_barrier_base(void)
{
	return dram_barrier_base;
}

void __init gic_init_irq(void)
{

	/* Static mapping, never released */
	gic_dist_base_addr = ioremap(OMAP44XX_GIC_DIST_BASE, SZ_4K);
	if (WARN_ON(!gic_dist_base_addr))
		return;

	/* Static mapping, never released */
	gic_cpu_base = ioremap(OMAP44XX_GIC_CPU_BASE, SZ_512);
	if (WARN_ON(!gic_cpu_base))
		return;

	omap_wakeupgen_init();

	gic_init(0, 29, gic_dist_base_addr, gic_cpu_base);
}

/*
 * FIXME: Remove this GIC APIs once common GIG library starts
 * supporting it.
 */
void gic_cpu_enable(void)
{
	__raw_writel(0xf0, gic_cpu_base + GIC_CPU_PRIMASK);
	__raw_writel(1, gic_cpu_base + GIC_CPU_CTRL);
}

void gic_cpu_disable(void)
{
	__raw_writel(0, gic_cpu_base + GIC_CPU_CTRL);
}

void gic_dist_enable(void)
{
	if (cpu_is_omap443x() ||
		!(__raw_readl(gic_dist_base_addr + GIC_DIST_CTRL) & 0x1))
			__raw_writel(0x1, gic_dist_base_addr + GIC_DIST_CTRL);
}
void gic_dist_disable(void)
{
	__raw_writel(0, gic_dist_base_addr + GIC_CPU_CTRL);
}

#ifdef CONFIG_CACHE_L2X0

void __iomem *omap4_get_l2cache_base(void)
{
	return l2cache_base;
}

static void omap4_l2x0_disable(void)
{
	/* Disable PL310 L2 Cache controller */
	omap_smc1(0x102, 0x0);
}

static void omap4_l2x0_set_debug(unsigned long val)
{
	/* Program PL310 L2 Cache controller debug register */
	omap_smc1(0x100, val);
}

static int __init omap_l2_cache_init(void)
{
	u32 aux_ctrl = 0;
	u32 por_ctrl = 0;
	u32 lockdown = 0;
	bool mpu_prefetch_disable_errata = false;

	/*
	 * To avoid code running on other OMAPs in
	 * multi-omap builds
	 */
	if (!cpu_is_omap44xx())
		return -ENODEV;

#ifdef CONFIG_OMAP_ALLOW_OSWR
	/* TODO: add revision info once verified */
	if (cpu_is_omap446x())
		mpu_prefetch_disable_errata = true;
#endif

	/* Static mapping, never released */
	l2cache_base = ioremap(OMAP44XX_L2CACHE_BASE, SZ_4K);
	if (WARN_ON(!l2cache_base))
		return -ENODEV;

	/*
	 * 16-way associativity, parity disabled
	 * Way size - 32KB (es1.0)
	 * Way size - 64KB (es2.0 +)
	 */
	aux_ctrl = readl_relaxed(l2cache_base + L2X0_AUX_CTRL);

	if (omap_rev() == OMAP4430_REV_ES1_0) {
		aux_ctrl |= 0x2 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT;
		goto skip_aux_por_api;
	}

	/*
	 * Drop instruction prefetch hint since it degrades the
	 * the performance.
	 */
	aux_ctrl |= ((0x3 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) |
		(1 << L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT) |
		(1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT));

	if (!mpu_prefetch_disable_errata)
		aux_ctrl |= (1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT);

	omap_smc1(0x109, aux_ctrl);

	/* Setup POR Control register */
	por_ctrl = readl_relaxed(l2cache_base + L2X0_PREFETCH_CTRL);

	/*
	 * Double linefill is available only on OMAP4460 L2X0.
	 * Undocumented bit 25 is set for better performance.
	 */
	if (cpu_is_omap446x())
		por_ctrl |= 1 << L2X0_PREFETCH_DOUBLE_LINEFILL_SHIFT;
	por_ctrl |= 1 << 25;
	if (!mpu_prefetch_disable_errata)
		por_ctrl |= 1 << L2X0_PREFETCH_DATA_PREFETCH_SHIFT;

	if (cpu_is_omap446x() || (omap_rev() >= OMAP4430_REV_ES2_2)) {
		por_ctrl |= L2X0_POR_OFFSET_VALUE;
		omap_smc1(0x113, por_ctrl);
	}

	/*
	 * FIXME: Temporary WA for OMAP4460 stability issue.
	 * Lock-down specific L2 cache ways which  makes effective
	 * L2 size as 512 KB instead of 1 MB
	 */
	if (cpu_is_omap446x()) {
		lockdown = 0xa5a5;
		writel_relaxed(lockdown, l2cache_base + L2X0_LOCKDOWN_WAY_D0);
		writel_relaxed(lockdown, l2cache_base + L2X0_LOCKDOWN_WAY_D1);
		writel_relaxed(lockdown, l2cache_base + L2X0_LOCKDOWN_WAY_I0);
		writel_relaxed(lockdown, l2cache_base + L2X0_LOCKDOWN_WAY_I1);
	}

skip_aux_por_api:
	/* Enable PL310 L2 Cache controller */
	omap_smc1(0x102, 0x1);

	l2x0_init(l2cache_base, aux_ctrl, L2X0_AUX_CTRL_MASK);

	/*
	 * Override default outer_cache.disable with a OMAP4
	 * specific one
	*/
	outer_cache.disable = omap4_l2x0_disable;
	outer_cache.set_debug = omap4_l2x0_set_debug;

	return 0;
}
early_initcall(omap_l2_cache_init);
#endif

static int __init omap_barriers_init(void)
{
	dma_addr_t dram_phys;

	if (!cpu_is_omap44xx())
		return -ENODEV;

	dram_barrier_base = dma_alloc_stronglyordered(NULL, SZ_4K,
				(dma_addr_t *)&dram_phys, GFP_KERNEL);
	if (!dram_barrier_base) {
		pr_err("%s: failed to allocate memory.\n", __func__);
		return -ENOMEM;
	}

	omap_bus_post.sync = omap_bus_sync;

	return 0;
}
core_initcall(omap_barriers_init);

void __iomem *omap4_get_sar_ram_base(void)
{
	return sar_ram_base;
}

/*
 * SAR RAM used to save and restore the HW
 * context in low power modes
 */
static int __init omap4_sar_ram_init(void)
{
	/*
	 * To avoid code running on other OMAPs in
	 * multi-omap builds
	 */
	if (!cpu_is_omap44xx())
		return -ENODEV;

	/* Static mapping, never released */
	sar_ram_base = ioremap(OMAP44XX_SAR_RAM_BASE, SZ_8K);
	if (WARN_ON(!sar_ram_base))
		return -ENODEV;

	l4_secure_clkdm = clkdm_lookup("l4_secure_clkdm");

	return 0;
}
early_initcall(omap4_sar_ram_init);


/*
 * omap4_sec_dispatcher: Routine to dispatch low power secure
 * service routines
 *
 * @idx: The HAL API index
 * @flag: The flag indicating criticality of operation
 * @nargs: Number of valid arguments out of four.
 * @arg1, arg2, arg3 args4: Parameters passed to secure API
 *
 * Return the error value on success/failure
 */
u32 omap4_secure_dispatcher(u32 idx, u32 flag, u32 nargs, u32 arg1, u32 arg2,
							 u32 arg3, u32 arg4)
{
	u32 ret;
	u32 param[5];

	param[0] = nargs;
	param[1] = arg1;
	param[2] = arg2;
	param[3] = arg3;
	param[4] = arg4;

	/*
	 * Put l4 secure to software wakeup  so that secure
	 * modules are accessible
	 */
	clkdm_wakeup(l4_secure_clkdm);

	/*
	 * Secure API needs physical address
	 * pointer for the parameters
	 */
	flush_cache_all();
	outer_clean_range(__pa(param), __pa(param + 5));

	ret = omap_smc2(idx, flag, __pa(param));

	/*
	 * Restore l4 secure to hardware superwised to allow
	 * secure modules idle
	 */
	clkdm_allow_idle(l4_secure_clkdm);

	return ret;
}
