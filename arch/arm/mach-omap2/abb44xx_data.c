/*
 * OMAP44xx Adaptive Body-Bias (ABB) data
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Mike Turquette <mturquette@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "abb.h"
#include "prm44xx.h"
#include "prm-regbits-44xx.h"

static const struct omap_abb_ops omap4_abb_ops = {
	.check_tranxdone	= &omap4_prm_abb_check_tranxdone,
	.clear_tranxdone	= &omap4_prm_abb_clear_tranxdone,
};

static const struct omap_abb_common omap4_abb_common = {
	.opp_sel_mask		= OMAP4430_OPP_SEL_MASK,
	.opp_sel_shift		= OMAP4430_OPP_SEL_SHIFT,
	.opp_change_mask	= OMAP4430_OPP_CHANGE_MASK,
	.sr2en_mask		= OMAP4430_SR2EN_MASK,
	.sr2en_shift		= OMAP4430_SR2EN_SHIFT,
	.active_fbb_sel_mask	= OMAP4430_ACTIVE_FBB_SEL_MASK,
	.active_fbb_sel_shift	= OMAP4430_ACTIVE_FBB_SEL_SHIFT,
	.sr2_wtcnt_value_mask	= OMAP4430_SR2_WTCNT_VALUE_MASK,
	.sr2_wtcnt_value_shift	= OMAP4430_SR2_WTCNT_VALUE_SHIFT,
	.settling_time		= 50,
	.cycle_rate		= 16,
	.ops			= &omap4_abb_ops,
};

struct omap_abb_instance omap4_abb_mpu = {
	.setup_offs		= OMAP4_PRM_LDO_ABB_MPU_SETUP_OFFSET,
	.ctrl_offs		= OMAP4_PRM_LDO_ABB_MPU_CTRL_OFFSET,
	.irqstatus_mpu_offs	= OMAP4_PRM_IRQSTATUS_MPU_2_OFFSET,
	.done_st_shift		= OMAP4430_ABB_MPU_DONE_ST_SHIFT,
	.done_st_mask		= OMAP4430_ABB_MPU_DONE_ST_MASK,
	.common			= &omap4_abb_common,
};

struct omap_abb_instance omap4_abb_iva = {
	.setup_offs		= OMAP4_PRM_LDO_ABB_IVA_SETUP_OFFSET,
	.ctrl_offs		= OMAP4_PRM_LDO_ABB_IVA_CTRL_OFFSET,
	.irqstatus_mpu_offs	= OMAP4_PRM_IRQSTATUS_MPU_OFFSET,
	.done_st_shift		= OMAP4430_ABB_IVA_DONE_ST_SHIFT,
	.done_st_mask		= OMAP4430_ABB_IVA_DONE_ST_MASK,
	.common			= &omap4_abb_common,
};
