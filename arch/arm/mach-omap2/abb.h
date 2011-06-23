/*
 * OMAP Adaptive Body-Bias structure and macro definitions
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Mike Turquette <mturquette@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_ABB_H
#define __ARCH_ARM_MACH_OMAP2_ABB_H

#include <linux/kernel.h>

#include "voltage.h"

#define NOMINAL_OPP		0
#define FAST_OPP		1

#define ABB_TRANXDONE_TIMEOUT	50

struct omap_abb_ops {
	u32 (*check_tranxdone)(u32 mask, u16 reg);
	void (*clear_tranxdone)(u32 bits, u16 reg);
};

struct omap_abb_common {
	u32 opp_sel_mask;
	u32 opp_change_mask;
	u32 sr2_wtcnt_value_mask;
	u32 sr2en_mask;
	u32 active_fbb_sel_mask;
	u8 opp_sel_shift;
	u8 sr2en_shift;
	u8 active_fbb_sel_shift;
	u8 sr2_wtcnt_value_shift;
	unsigned long settling_time;
	unsigned long cycle_rate;
	const struct omap_abb_ops *ops;
};

struct omap_abb_instance {
	u32 done_st_mask;
	u8 setup_offs;
	u8 ctrl_offs;
	u16 irqstatus_mpu_offs;
	u8 done_st_shift;
	bool enabled;
	const struct omap_abb_common *common;
};

extern struct omap_abb_instance omap36xx_abb_mpu;

extern struct omap_abb_instance omap4_abb_mpu;
extern struct omap_abb_instance omap4_abb_iva;

#endif
