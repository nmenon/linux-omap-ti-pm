/*
 * OMAP36xx Adaptive Body-Bias (ABB) data
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Mike Turquette <mturquette@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "abb.h"
#include "prm2xxx_3xxx.h"
#include "prm-regbits-34xx.h"

static const struct omap_abb_ops omap36xx_abb_ops = {
	.check_tranxdone	= &omap36xx_prm_abb_check_tranxdone,
	.clear_tranxdone	= &omap36xx_prm_abb_clear_tranxdone,
};

static const struct omap_abb_common omap36xx_abb_common = {
	.opp_sel_mask		= OMAP3630_OPP_SEL_MASK,
	.opp_sel_shift		= OMAP3630_OPP_SEL_SHIFT,
	.opp_change_mask	= OMAP3630_OPP_CHANGE_MASK,
	.sr2en_mask		= OMAP3630_SR2EN_MASK,
	.sr2en_shift		= OMAP3630_SR2EN_SHIFT,
	.active_fbb_sel_mask	= OMAP3630_ACTIVE_FBB_SEL_MASK,
	.active_fbb_sel_shift	= OMAP3630_ACTIVE_FBB_SEL_SHIFT,
	.sr2_wtcnt_value_mask	= OMAP3630_SR2_WTCNT_VALUE_MASK,
	.sr2_wtcnt_value_shift	= OMAP3630_SR2_WTCNT_VALUE_SHIFT,
	.settling_time		= 30,
	.cycle_rate		= 8,
	.ops			= &omap36xx_abb_ops,
};

struct omap_abb_instance omap36xx_abb_mpu = {
	.setup_offs		= OMAP3_PRM_LDO_ABB_SETUP_OFFSET,
	.ctrl_offs		= OMAP3_PRM_LDO_ABB_CTRL_OFFSET,
	.irqstatus_mpu_offs	= OMAP3_PRM_IRQSTATUS_MPU_OFFSET,
	.done_st_shift		= OMAP3630_ABB_LDO_TRANXDONE_ST_SHIFT,
	.done_st_mask		= OMAP3630_ABB_LDO_TRANXDONE_ST_MASK,
	.common			= &omap36xx_abb_common,
};
