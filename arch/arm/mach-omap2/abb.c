/*
 * OMAP Adaptive Body-Bias core
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Mike Turquette <mturquette@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/delay.h>

#include "abb.h"
#include "voltage.h"

/**
 * omap_abb_set_opp() - setup abb on an OPP
 * @voltdm:	voltage domain
 * @abb_type:	type of ABB used
 *
 * setup ABB for a domain
 */
int omap_abb_set_opp(struct voltagedomain *voltdm, u8 abb_type)
{
	struct omap_abb_instance *abb = voltdm->abb;
	int ret, timeout;

	/* clear interrupt status */
	timeout = 0;
	while (timeout++ < ABB_TRANXDONE_TIMEOUT) {
		abb->common->ops->clear_tranxdone(abb->done_st_mask,
						  abb->irqstatus_mpu_offs);

		ret = abb->common->ops->check_tranxdone(abb->done_st_mask,
							abb->
							irqstatus_mpu_offs);
		if (!ret)
			break;

		udelay(1);
	}

	if (timeout >= ABB_TRANXDONE_TIMEOUT) {
		pr_warning("%s: vdd_%s ABB TRANXDONE timeout\n",
			   __func__, voltdm->name);
		return -ETIMEDOUT;
	}

	/* program next state of ABB ldo */
	voltdm->rmw(abb->common->opp_sel_mask,
		    abb_type << abb->common->opp_sel_shift, abb->ctrl_offs);

	/* initiate ABB ldo change */
	voltdm->rmw(abb->common->opp_change_mask,
		    abb->common->opp_change_mask, abb->ctrl_offs);

	/* clear interrupt status */
	timeout = 0;
	while (timeout++ < ABB_TRANXDONE_TIMEOUT) {
		abb->common->ops->clear_tranxdone(abb->done_st_mask,
						  abb->irqstatus_mpu_offs);

		ret = abb->common->ops->check_tranxdone(abb->done_st_mask,
							abb->
							irqstatus_mpu_offs);
		if (!ret)
			break;

		udelay(1);
	}

	if (timeout >= ABB_TRANXDONE_TIMEOUT) {
		pr_warning("%s: vdd_%s ABB TRANXDONE timeout\n",
			   __func__, voltdm->name);
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * omap_abb_enable() - Enable ABB
 * @voltdm:	voltage domain
 *
 * Enable ABB
 */
void omap_abb_enable(struct voltagedomain *voltdm)
{
	struct omap_abb_instance *abb = voltdm->abb;

	if (abb->enabled)
		return;

	abb->enabled = true;

	voltdm->rmw(abb->common->sr2en_mask, abb->common->sr2en_mask,
		    abb->setup_offs);
}

/**
 * omap_abb_disable() - Disable ABB
 * @voltdm:	voltage domain
 *
 * Currently unused, but may be needed for future module conversion
 */
void omap_abb_disable(struct voltagedomain *voltdm)
{
	struct omap_abb_instance *abb = voltdm->abb;

	if (!abb->enabled)
		return;

	abb->enabled = false;

	voltdm->rmw(abb->common->sr2en_mask, (0 << abb->common->sr2en_shift),
		    abb->setup_offs);
}

/**
 * omap_abb_init() - initialize an ABB instance
 * @voltdm:	voltage domain
 *
 * Initialize an ABB instance for Forward Body-Bias
 */
void __init omap_abb_init(struct voltagedomain *voltdm)
{
	struct omap_abb_instance *abb = voltdm->abb;
	u32 sys_clk_rate;
	u32 sr2_wt_cnt_val;
	u32 cycle_rate;
	u32 settling_time;

	if (IS_ERR_OR_NULL(abb))
		return;

	/*
	 * SR2_WTCNT_VALUE must be programmed with the expected settling time
	 * for ABB ldo transition.  This value depends on the cycle rate for
	 * the ABB IP (varies per OMAP family), and the system clock frequency
	 * (varies per board).  The formula is:
	 *
	 * SR2_WTCNT_VALUE = SettlingTime / (CycleRate / SystemClkRate))
	 * where SettlingTime is in micro-seconds and SystemClkRate is in MHz.
	 *
	 * To avoid dividing by zero multiply both CycleRate and SettlingTime
	 * by 10 such that the final result is the one we want.
	 */

	/* convert SYS_CLK rate to MHz & prevent divide by zero */
	sys_clk_rate = DIV_ROUND_CLOSEST(voltdm->sys_clk.rate, 1000000);
	cycle_rate = abb->common->cycle_rate * 10;
	settling_time = abb->common->settling_time * 10;

	/* calculate cycle rate */
	cycle_rate = DIV_ROUND_CLOSEST(cycle_rate, sys_clk_rate);

	/* calulate SR2_WTCNT_VALUE */
	sr2_wt_cnt_val = DIV_ROUND_CLOSEST(settling_time, cycle_rate);

	voltdm->rmw(abb->common->sr2_wtcnt_value_mask,
		    (sr2_wt_cnt_val << abb->common->sr2_wtcnt_value_shift),
		    abb->setup_offs);

	/* allow Forward Body-Bias */
	voltdm->rmw(abb->common->active_fbb_sel_mask,
		    abb->common->active_fbb_sel_mask, abb->setup_offs);

	/* enable the ldo */
	omap_abb_enable(voltdm);

	return;
}
