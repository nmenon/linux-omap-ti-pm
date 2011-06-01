/*
 * omap4-common.h: OMAP4 specific common header file
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 * Author:
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef OMAP_ARCH_OMAP4_COMMON_H
#define OMAP_ARCH_OMAP4_COMMON_H

#include <asm/proc-fns.h>
/*
 * Secure low power context save/restore API index
 */
#define HAL_SAVESECURERAM_INDEX		0x1a
#define HAL_SAVEHW_INDEX		0x1b
#define HAL_SAVEALL_INDEX		0x1c
#define HAL_SAVEGIC_INDEX		0x1d
#define PPA_SERVICE_NS_SMP		0x25
/*
 * Secure HAL API flags
 */
#define FLAG_START_CRITICAL		0x4
#define FLAG_IRQFIQ_MASK		0x3
#define FLAG_IRQ_ENABLE			0x2
#define FLAG_FIQ_ENABLE			0x1
#define NO_FLAG				0x0
#ifndef __ASSEMBLER__

#ifdef CONFIG_CACHE_L2X0
extern void __iomem *omap4_get_l2cache_base(void);
#endif

#ifdef CONFIG_SMP
extern void __iomem *omap4_get_scu_base(void);
#else
static inline void __iomem *omap4_get_scu_base(void)
{
	return NULL;
}
#endif

extern void __iomem *omap4_get_gic_dist_base(void);
extern void __iomem *omap4_get_gic_cpu_base(void);
extern void __iomem *omap4_get_sar_ram_base(void);
extern dma_addr_t omap4_secure_ram_phys;
extern void __init gic_init_irq(void);
extern void gic_cpu_enable(void);
extern void gic_cpu_disable(void);
extern void gic_dist_enable(void);
extern void gic_dist_disable(void);
extern void omap_smc1(u32 fn, u32 arg);

/*
 * Read MPIDR: Multiprocessor affinity register
 */
static inline unsigned int hard_smp_processor_id(void)
{
	unsigned int cpunum;

	asm volatile (
	"mrc	 p15, 0, %0, c0, c0, 5\n"
		: "=r" (cpunum));
	return cpunum &= 0x0f;
}

#if defined(CONFIG_SMP)	&& defined(CONFIG_PM)
extern int omap4_mpuss_init(void);
extern int omap4_enter_lowpower(unsigned int cpu, unsigned int power_state);
extern void omap4_cpu_suspend(unsigned int cpu, unsigned int save_state);
extern void omap4_cpu_resume(void);
extern u32 omap_smc2(u32 id, u32 falg, u32 pargs);
extern u32 omap4_secure_dispatcher(u32 idx, u32 flag, u32 nargs,
				u32 arg1, u32 arg2, u32 arg3, u32 arg4);
#else

static inline int omap4_enter_lowpower(unsigned int cpu,
					unsigned int power_state)
{
	cpu_do_idle();
	return 0;
}

static inline int omap4_mpuss_init(void)
{
	return 0;
}

static inline void omap4_cpu_suspend(unsigned int cpu, unsigned int save_state)
{
}

static inline void omap4_cpu_resume(void)
{
}

#endif
#endif /* __ASSEMBLER__ */
#endif /* OMAP_ARCH_OMAP4_COMMON_H */
