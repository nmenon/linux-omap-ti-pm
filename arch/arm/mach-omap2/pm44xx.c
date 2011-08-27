/*
 * OMAP4 Power Management Routines
 *
 * Copyright (C) 2010-2011 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 * Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>

#include <asm/hardware/gic.h>
#include <mach/omap4-common.h>
#include <plat/omap_hsi.h>
#include <plat/common.h>
#include <plat/temperature_sensor.h>
#include <plat/usb.h>
#include <plat/prcm.h>
#include <plat/omap-pm.h>
#include <plat/gpmc.h>

#include "powerdomain.h"
#include "clockdomain.h"
#include "pm.h"
#include "prm-regbits-44xx.h"
#include "prcm44xx.h"
#include "prm44xx.h"
#include "prminst44xx.h"
#include "clock.h"
#include "cm2_44xx.h"
#include "cm1_44xx.h"
#include "cm44xx.h"
#include "cm-regbits-44xx.h"
#include "cminst44xx.h"
#include "prcm-debug.h"

#include "smartreflex.h"
#include "dvfs.h"
#include "voltage.h"
#include "vc.h"

struct power_state {
	struct powerdomain *pwrdm;
	u32 next_state;
#ifdef CONFIG_SUSPEND
	u32 saved_state;
	u32 saved_logic_state;
#endif
	struct list_head node;
};

static LIST_HEAD(pwrst_list);
static struct powerdomain *mpu_pwrdm, *cpu0_pwrdm;
static struct powerdomain *core_pwrdm, *per_pwrdm;

static struct voltagedomain *mpu_voltdm, *iva_voltdm, *core_voltdm;

#define MAX_IOPAD_LATCH_TIME 1000
void omap4_trigger_ioctrl(void)
{
	int i = 0;

	/* Enable GLOBAL_WUEN */
	if (!omap4_cminst_read_inst_reg_bits(OMAP4430_PRM_PARTITION,
			OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_IO_PMCTRL_OFFSET,
			OMAP4430_GLOBAL_WUEN_MASK))
		omap4_prminst_rmw_inst_reg_bits(OMAP4430_GLOBAL_WUEN_MASK,
			OMAP4430_GLOBAL_WUEN_MASK, OMAP4430_PRM_PARTITION,
			OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_IO_PMCTRL_OFFSET);

	/* Trigger WUCLKIN enable */
	omap4_prminst_rmw_inst_reg_bits(OMAP4430_WUCLK_CTRL_MASK, OMAP4430_WUCLK_CTRL_MASK,
		OMAP4430_PRM_PARTITION, OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_IO_PMCTRL_OFFSET);
	omap_test_timeout(
		((omap4_prminst_read_inst_reg(OMAP4430_PRM_PARTITION, OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_IO_PMCTRL_OFFSET)
		>> OMAP4430_WUCLK_STATUS_SHIFT) == 1),
		MAX_IOPAD_LATCH_TIME, i);
	/* Trigger WUCLKIN disable */
	omap4_prminst_rmw_inst_reg_bits(OMAP4430_WUCLK_CTRL_MASK, 0x0,
		OMAP4430_PRM_PARTITION, OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_IO_PMCTRL_OFFSET);
	return;
}

#ifdef CONFIG_SUSPEND
/* This is a common low power function called from suspend and
 * cpuidle
 */

void omap4_enter_sleep(unsigned int cpu, unsigned int power_state, bool suspend)
{
	int cpu0_next_state = PWRDM_POWER_ON;
	int per_next_state = PWRDM_POWER_ON;
	int core_next_state = PWRDM_POWER_ON;
	int mpu_next_state = PWRDM_POWER_ON;

	pwrdm_clear_all_prev_pwrst(cpu0_pwrdm);
	pwrdm_clear_all_prev_pwrst(mpu_pwrdm);
	pwrdm_clear_all_prev_pwrst(core_pwrdm);
	pwrdm_clear_all_prev_pwrst(per_pwrdm);
	omap4_device_clear_prev_off_state();

	cpu0_next_state = pwrdm_read_next_pwrst(cpu0_pwrdm);
	per_next_state = pwrdm_read_next_pwrst(per_pwrdm);
	core_next_state = pwrdm_read_next_pwrst(core_pwrdm);
	mpu_next_state = pwrdm_read_next_pwrst(mpu_pwrdm);

	if (mpu_next_state < PWRDM_POWER_INACTIVE) {
		if (omap_dvfs_is_scaling(mpu_voltdm)) {
			mpu_next_state = PWRDM_POWER_INACTIVE;
			pwrdm_set_next_pwrst(mpu_pwrdm, mpu_next_state);
		} else {
			if (!suspend)
				omap_sr_disable_reset_volt(mpu_voltdm);
			omap_vc_set_auto_trans(mpu_voltdm,
				OMAP_VC_CHANNEL_AUTO_TRANSITION_RETENTION);
		}
	}

	if (core_next_state < PWRDM_POWER_ON) {
		/*
		 * Note: IVA can hit RET outside of cpuidle and hence this is
		 * not the right optimal place to enable IVA AUTO RET. But since
		 * enabling AUTO RET requires SR to disabled, its done here for
		 * now. Needs a relook to see if this can be optimized.
		 */
		if (omap_dvfs_is_scaling(core_voltdm) ||
		    omap_dvfs_is_scaling(iva_voltdm)) {
			core_next_state = PWRDM_POWER_ON;
			pwrdm_set_next_pwrst(core_pwrdm, core_next_state);
		} else {
			if (!suspend) {
				omap_sr_disable_reset_volt(iva_voltdm);
				omap_sr_disable_reset_volt(core_voltdm);
			}
			omap_vc_set_auto_trans(core_voltdm,
				OMAP_VC_CHANNEL_AUTO_TRANSITION_RETENTION);
			omap_vc_set_auto_trans(iva_voltdm,
				OMAP_VC_CHANNEL_AUTO_TRANSITION_RETENTION);

			omap_temp_sensor_prepare_idle();
		}
	}

	if (omap4_device_next_state_off()) {
		omap2_gpio_prepare_for_idle(true);
		omap_gpmc_save_context();
	}

	if (suspend && cpu_is_omap44xx())
		omap4_pm_suspend_save_regs();

	if (omap4_device_next_state_off()) {
		/* Save the device context to SAR RAM */
		if (omap4_sar_save())
			goto abort_device_off;
		omap4_sar_overwrite();
		omap4_cm_prepare_off();
		omap4_dpll_prepare_off();
	}

	omap4_enter_lowpower(cpu, power_state);

	if (omap4_device_prev_state_off()) {
		omap4_dpll_resume_off();
		omap4_cm_resume_off();
#ifdef CONFIG_PM_DEBUG
		omap4_device_off_counter++;
#endif
	}

abort_device_off:
	if (core_next_state < PWRDM_POWER_ON) {
		/* See note above */
		omap_vc_set_auto_trans(core_voltdm,
				OMAP_VC_CHANNEL_AUTO_TRANSITION_DISABLE);
		omap_vc_set_auto_trans(iva_voltdm,
				OMAP_VC_CHANNEL_AUTO_TRANSITION_DISABLE);

		omap_temp_sensor_resume_idle();
		if (!suspend) {
			omap_sr_enable(iva_voltdm);
			omap_sr_enable(core_voltdm);
		}
	}

	if (omap4_device_prev_state_off()) {
		omap_gpmc_restore_context();
		omap2_gpio_resume_after_idle();
	}

	if (mpu_next_state < PWRDM_POWER_INACTIVE) {
		omap_vc_set_auto_trans(mpu_voltdm,
				OMAP_VC_CHANNEL_AUTO_TRANSITION_DISABLE);
		if (!suspend)
			omap_sr_enable(mpu_voltdm);
	}

	return;
}

#ifdef CONFIG_PM_DEBUG
#define GPIO_BANKS		6
#define MODULEMODE_DISABLED	0x0
#define MODULEMODE_AUTO		0x1

static void _print_wakeirq(int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (irq == OMAP44XX_IRQ_LOCALTIMER)
		pr_info("Resume caused by IRQ %d, localtimer\n", irq);
	else if (!desc || !desc->action || !desc->action->name)
		pr_info("Resume caused by IRQ %d\n", irq);
	else
		pr_info("Resume caused by IRQ %d, %s\n", irq,
			desc->action->name);
}

static void _print_gpio_wakeirq(int irq)
{
	int bank = irq - OMAP44XX_IRQ_GPIO1;
	int bit;
	int gpioirq;
	int restoremod = 0;
	int timeout = 10;
	u32 wken, irqst, gpio;
	u32 clkctrl;
	long unsigned int wkirqs;
	void *gpio_base[GPIO_BANKS] = {
		OMAP2_L4_IO_ADDRESS(0x4a310000),
		OMAP2_L4_IO_ADDRESS(0x48055000),
		OMAP2_L4_IO_ADDRESS(0x48057000),
		OMAP2_L4_IO_ADDRESS(0x48059000),
		OMAP2_L4_IO_ADDRESS(0x4805b000),
		OMAP2_L4_IO_ADDRESS(0x4805d000),
	};
	void *gpio_clkctrl[GPIO_BANKS] = {
		OMAP4430_CM_WKUP_GPIO1_CLKCTRL,
		OMAP4430_CM_L4PER_GPIO2_CLKCTRL,
		OMAP4430_CM_L4PER_GPIO3_CLKCTRL,
		OMAP4430_CM_L4PER_GPIO4_CLKCTRL,
		OMAP4430_CM_L4PER_GPIO5_CLKCTRL,
		OMAP4430_CM_L4PER_GPIO6_CLKCTRL,
	};

	/*
	 * GPIO1 is in CD_WKUP.
	 * GPIO2-6 are in CD_l4_PER.
	 *
	 * Both of these clock domains are static dependencies of
	 * the MPUSS clock domain (CD_CORTEXA9) and are guaranteed
	 * to be already enabled (_CLKSTCTRL.CLKTRCTRL = HW_AUTO).
	 *
	 * Ensure the GPIO module is enabled (_CLKCTRL.MODULEMODE =
	 * h/w managed).  If not, will set it back to disabled when
	 * done.
	 */

	clkctrl = __raw_readl(gpio_clkctrl[bank]);

	if ((clkctrl & OMAP4430_MODULEMODE_MASK) !=
	    MODULEMODE_AUTO << OMAP4430_MODULEMODE_SHIFT) {
		restoremod = 1;
		__raw_writel((clkctrl & ~(OMAP4430_MODULEMODE_MASK)) |
			     MODULEMODE_AUTO << OMAP4430_MODULEMODE_SHIFT,
			     gpio_clkctrl[bank]);

		while ((__raw_readl(gpio_clkctrl[bank]) &
			OMAP4430_MODULEMODE_MASK) !=
		       MODULEMODE_AUTO << OMAP4430_MODULEMODE_SHIFT &&
		       --timeout)
			udelay(5);

		if (!timeout)
			goto punt;
	}

	wken = __raw_readl(gpio_base[bank] + OMAP4_GPIO_IRQWAKEN0);
	irqst = __raw_readl(gpio_base[bank] + OMAP4_GPIO_IRQSTATUS0);
	wkirqs = irqst & wken;

	if (!wkirqs)
		wkirqs = irqst;

	if (!wkirqs)
		goto punt;

	for_each_set_bit(bit, &wkirqs, 32) {
		gpio = bit + bank * 32;
		gpioirq = gpio_to_irq(gpio);

		if (gpioirq < 0)
			pr_info("Resume caused by GPIO %d\n", (int)gpio);
		else
			_print_wakeirq(gpioirq);
	}

	goto out;

punt:
	pr_info("Resume caused by IRQ %d, unknown GPIO%d interrupt\n", irq,
		bank + 1);

out:
	if (restoremod)
		__raw_writel(clkctrl, gpio_clkctrl[bank]);
}

#define CONTROL_PADCONF_WAKEUPEVENT_0	0x4a1001d8
#define CONTROL_WKUP_PADCONF_WAKEUPEVENT_0	0x4a31E07C

static void _print_prcm_wakeirq(int irq)
{
	int i, bit;
	int iopad_wake_found = 0;
	u32 prcm_irqs1, prcm_irqs2;
	long unsigned int wkup_pad_event;

	prcm_irqs1 = omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST,
					     OMAP4_PRM_IRQSTATUS_MPU_OFFSET);
	prcm_irqs1 &= omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST,
					     OMAP4_PRM_IRQENABLE_MPU_OFFSET);
	prcm_irqs2 = omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST,
					     OMAP4_PRM_IRQSTATUS_MPU_2_OFFSET);
	prcm_irqs2 &= omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST,
					      OMAP4_PRM_IRQENABLE_MPU_2_OFFSET);

	if (prcm_irqs1 & OMAP4430_IO_ST_MASK) {
		for (i = 0; i <= 6; i++) {
			long unsigned int wkevt =
				omap_readl(CONTROL_PADCONF_WAKEUPEVENT_0 + i*4);

			for_each_set_bit(bit, &wkevt, 32) {
				pr_info("Resume caused by I/O pad: CONTROL_PADCONF_WAKEUPEVENT_%d[%d]\n",
					i, bit);
				iopad_wake_found = 1;
			}
		}
		wkup_pad_event = omap_readl(CONTROL_WKUP_PADCONF_WAKEUPEVENT_0);
		for_each_set_bit(bit, &wkup_pad_event, 25) {
			pr_info("Resume caused by wakeup I/O pad: CONTROL_WKUP_PADCONF_WAKEUPEVENT_0[%d]\n", bit);
			iopad_wake_found = 1;
		}
	}

	if (prcm_irqs1 & ~OMAP4430_IO_ST_MASK || !iopad_wake_found ||
	    prcm_irqs2)
		pr_info("Resume caused by IRQ %d, prcm: 0x%x 0x%x\n", irq,
			prcm_irqs1, prcm_irqs2);
}

static void omap4_print_wakeirq(void)
{
	int irq;

	irq = gic_cpu_read(GIC_CPU_HIGHPRI) & 0x3ff;

	if ((irq == 1022) || (irq == 1023)) {
		pr_info("GIC returns spurious interrupt for resume IRQ\n");
		return;
	}

	if (irq >= OMAP44XX_IRQ_GPIO1 &&
	    irq <= OMAP44XX_IRQ_GPIO1 + GPIO_BANKS - 1)
		_print_gpio_wakeirq(irq);
	else if (irq == OMAP44XX_IRQ_PRCM)
		_print_prcm_wakeirq(irq);
	else
		_print_wakeirq(irq);
}
#else
static void omap4_print_wakeirq(void)
{
}
#endif

/**
 * get_achievable_state() - Provide achievable state
 * @available_states:	what states are available
 * @req_min_state:	what state is the minimum we'd like to hit
 *
 * Power domains have varied capabilities. When attempting a low power
 * state such as OFF/RET, a specific min requested state may not be
 * supported on the power domain, in which case, the next higher power
 * state which is supported is returned. This is because a combination
 * of system power states where the parent PD's state is not in line
 * with expectation can result in system instabilities.
 */
static inline u8 get_achievable_state(u8 available_states, u8 req_min_state)
{
	u8 mask = 0xFF << req_min_state;

	if (available_states & mask)
		return __ffs(available_states & mask);
	return PWRDM_POWER_ON;
}

/**
 * omap4_configure_pwdm_suspend() - Program powerdomain on suspend
 * @is_off_mode: is this an OFF mode transition?
 *
 * Program all powerdomain to required power domain state: This logic
 * Takes the requested mode -OFF/RET translates it to logic and power
 * states. This then walks down the power domain states to program
 * each domain to the state requested. if the requested state is not
 * available, it will check for the higher state.
 */
static void omap4_configure_pwdm_suspend(bool is_off_mode)
{
	struct power_state *pwrst;
	u32 state;
	u32 logic_state, als;

#ifdef CONFIG_OMAP_ALLOW_OSWR
	if (is_off_mode) {
		state = PWRDM_POWER_OFF;
		logic_state = PWRDM_POWER_OFF;
	} else {
		state = PWRDM_POWER_RET;
		logic_state = PWRDM_POWER_OFF;
	}
#else
	state = PWRDM_POWER_RET;
	logic_state = PWRDM_POWER_RET;
#endif

	list_for_each_entry(pwrst, &pwrst_list, node) {
		if ((!strcmp(pwrst->pwrdm->name, "cpu0_pwrdm")) ||
			(!strcmp(pwrst->pwrdm->name, "cpu1_pwrdm")))
				continue;
		/*
		 * Write only to registers which are writable! Don't touch
		 * read-only/reserved registers. If pwrdm->pwrsts_logic_ret or
		 * pwrdm->pwrsts are 0, consider those power domains containing
		 * readonly/reserved registers which cannot be controlled by
		 * software.
		 */
		if (pwrst->pwrdm->pwrsts_logic_ret) {
			als =
			   get_achievable_state(pwrst->pwrdm->pwrsts_logic_ret,
					logic_state);
			pwrdm_set_logic_retst(pwrst->pwrdm, als);
		}
		if (pwrst->pwrdm->pwrsts) {
			pwrst->next_state =
			   get_achievable_state(pwrst->pwrdm->pwrsts, state);
			omap_set_pwrdm_state(pwrst->pwrdm, pwrst->next_state);
		}
	}
}

static int omap4_pm_suspend(void)
{
	struct power_state *pwrst;
	int state, ret = 0;

	/* Wakeup timer from suspend */
	if (wakeup_timer_seconds || wakeup_timer_milliseconds)
		omap2_pm_wakeup_on_timer(wakeup_timer_seconds,
					 wakeup_timer_milliseconds);

	/* Save current powerdomain state */
	list_for_each_entry(pwrst, &pwrst_list, node) {
		pwrst->saved_state = pwrdm_read_next_pwrst(pwrst->pwrdm);
		pwrst->saved_logic_state = pwrdm_read_logic_retst(pwrst->pwrdm);
	}

	omap4_configure_pwdm_suspend(off_mode_enabled);

	/* Enable Device OFF */
	if (off_mode_enabled)
		omap4_device_set_state_off(1);

	/*
	 * For MPUSS to hit power domain retention(CSWR or OSWR),
	 * CPU0 and CPU1 power domain needs to be in OFF or DORMANT
	 * state. For MPUSS to reach off-mode. CPU0 and CPU1 power domain
	 * should be in off state.
	 * Only master CPU followes suspend path. All other CPUs follow
	 * cpu-hotplug path in system wide suspend. On OMAP4, CPU power
	 * domain CSWR is not supported by hardware.
	 * More details can be found in OMAP4430 TRM section 4.3.4.2.
	 */
	omap4_enter_sleep(0, PWRDM_POWER_OFF, true);
	omap4_print_wakeirq();
	prcmdebug_dump(PRCMDEBUG_LASTSLEEP);

	/* Disable Device OFF state*/
	if (off_mode_enabled)
		omap4_device_set_state_off(0);

	/* Restore next powerdomain state */
	list_for_each_entry(pwrst, &pwrst_list, node) {
		state = pwrdm_read_prev_pwrst(pwrst->pwrdm);
		if (state > pwrst->next_state) {
			pr_info("Powerdomain (%s) didn't enter "
			       "target state %d\n",
			       pwrst->pwrdm->name, pwrst->next_state);
			ret = -1;
		}
		omap_set_pwrdm_state(pwrst->pwrdm, pwrst->saved_state);
		pwrdm_set_logic_retst(pwrst->pwrdm, pwrst->saved_logic_state);
	}
	if (ret)
		pr_err("Could not enter target state in pm_suspend\n");
	else
		pr_err("Successfully put all powerdomains to target state\n");

	return 0;
}

static int omap4_pm_enter(suspend_state_t suspend_state)
{
	int ret = 0;

	switch (suspend_state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		ret = omap4_pm_suspend();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int omap4_pm_begin(suspend_state_t state)
{
	disable_hlt();
	return 0;
}

static void omap4_pm_end(void)
{
	enable_hlt();
	return;
}

static const struct platform_suspend_ops omap_pm_ops = {
	.begin		= omap4_pm_begin,
	.end		= omap4_pm_end,
	.enter		= omap4_pm_enter,
	.valid		= suspend_valid_only_mem,
};
#else
void omap4_enter_sleep(unsigned int cpu, unsigned int power_state){ return; }
#endif /* CONFIG_SUSPEND */

/*
 * Enable hardware supervised mode for all clockdomains if it's
 * supported. Initiate sleep transition for other clockdomains, if
 * they are not used
 */
static int __init clkdms_setup(struct clockdomain *clkdm, void *unused)
{
	if (clkdm->flags & CLKDM_CAN_ENABLE_AUTO)
		clkdm_allow_idle(clkdm);
	else if (clkdm->flags & CLKDM_CAN_FORCE_SLEEP &&
			atomic_read(&clkdm->usecount) == 0)
		clkdm_sleep(clkdm);
	return 0;
}


static int __init pwrdms_setup(struct powerdomain *pwrdm, void *unused)
{
	struct power_state *pwrst;

	if (!pwrdm->pwrsts)
		return 0;

	pwrst = kmalloc(sizeof(struct power_state), GFP_ATOMIC);
	if (!pwrst)
		return -ENOMEM;

	pwrst->pwrdm = pwrdm;
	if ((!strcmp(pwrdm->name, "mpu_pwrdm")) ||
			(!strcmp(pwrdm->name, "core_pwrdm")) ||
			(!strcmp(pwrdm->name, "cpu0_pwrdm")) ||
			(!strcmp(pwrdm->name, "cpu1_pwrdm")))
		pwrst->next_state = PWRDM_POWER_ON;
	else
		pwrst->next_state = PWRDM_POWER_RET;
	list_add(&pwrst->node, &pwrst_list);

	return omap_set_pwrdm_state(pwrst->pwrdm, pwrst->next_state);
}

static void __init prcm_setup_regs(void)
{
	/* Enable IO_ST interrupt */
	omap4_prminst_rmw_inst_reg_bits(OMAP4430_IO_ST_MASK, OMAP4430_IO_ST_MASK,
		OMAP4430_PRM_PARTITION, OMAP4430_PRM_OCP_SOCKET_INST, OMAP4_PRM_IRQENABLE_MPU_OFFSET);

	/*
	 * Errata ID: i608 Impacted OMAP4430 ES 1.0,2.0,2.1,2.2
	 * On OMAP4, Retention-Till-Access Memory feature is not working
	 * reliably and hardware recommondation is keep it disabled by
	 * default
	 */
	omap4_prminst_rmw_inst_reg_bits(OMAP4430_DISABLE_RTA_EXPORT_MASK,
		0x1 << OMAP4430_DISABLE_RTA_EXPORT_SHIFT,
		OMAP4430_PRM_PARTITION, OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_SRAM_WKUP_SETUP_OFFSET);
	omap4_prminst_rmw_inst_reg_bits(OMAP4430_DISABLE_RTA_EXPORT_MASK,
		0x1 << OMAP4430_DISABLE_RTA_EXPORT_SHIFT,
		OMAP4430_PRM_PARTITION, OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_LDO_SRAM_CORE_SETUP_OFFSET);
	omap4_prminst_rmw_inst_reg_bits(OMAP4430_DISABLE_RTA_EXPORT_MASK,
		0x1 << OMAP4430_DISABLE_RTA_EXPORT_SHIFT,
		OMAP4430_PRM_PARTITION, OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_LDO_SRAM_MPU_SETUP_OFFSET);
	omap4_prminst_rmw_inst_reg_bits(OMAP4430_DISABLE_RTA_EXPORT_MASK,
		0x1 << OMAP4430_DISABLE_RTA_EXPORT_SHIFT,
		OMAP4430_PRM_PARTITION, OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_LDO_SRAM_IVA_SETUP_OFFSET);
	/* Toggle CLKREQ in RET and OFF states */
	omap4_prminst_write_inst_reg(0x2, OMAP4430_PRM_PARTITION,
		OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_CLKREQCTRL_OFFSET);
	/*
	 * De-assert PWRREQ signal in Device OFF state
	 *	0x3: PWRREQ is de-asserted if all voltage domain are in
	 *	OFF state. Conversely, PWRREQ is asserted upon any
	 *	voltage domain entering or staying in ON or SLEEP or
	 *	RET state.
	 */
	omap4_prminst_write_inst_reg(0x3, OMAP4430_PRM_PARTITION,
		OMAP4430_PRM_DEVICE_INST, OMAP4_PRM_PWRREQCTRL_OFFSET);
}
static irqreturn_t prcm_interrupt_handler (int irq, void *dev_id)
{
	u32 irqenable_mpu, irqstatus_mpu;

	irqenable_mpu = omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST,
					 OMAP4_PRM_IRQENABLE_MPU_OFFSET);
	irqstatus_mpu = omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST,
					 OMAP4_PRM_IRQSTATUS_MPU_OFFSET);

	/* Check if a IO_ST interrupt */
	if (irqstatus_mpu & OMAP4430_IO_ST_MASK) {
		/* Check if HSI caused the IO wakeup */
		if (omap_hsi_is_io_wakeup_from_hsi())
			omap_hsi_wakeup(0);
		omap_uart_resume_idle();
		usbhs_wakeup();
		omap4_trigger_ioctrl();
	}

	/* Clear the interrupt */
	irqstatus_mpu &= irqenable_mpu;
	omap4_prm_write_inst_reg(irqstatus_mpu, OMAP4430_PRM_OCP_SOCKET_INST,
					OMAP4_PRM_IRQSTATUS_MPU_OFFSET);

	return IRQ_HANDLED;
}

/**
 * omap_default_idle() - implement a default idle for !CONFIG_CPUIDLE
 *
 * Implements OMAP4 memory, IO ordering requirements which can't be addressed
 * with default arch_idle() hook. Used by all CPUs with !CONFIG_CPUIDLE and
 * by secondary CPU with CONFIG_CPUIDLE.
 */
static void omap_default_idle(void)
{
	local_irq_disable();
	local_fiq_disable();

	omap_do_wfi();

	local_fiq_enable();
	local_irq_enable();
}

/**
 * omap4_device_set_state_off() - setup device off state
 * @enable:	set to off or not.
 *
 * When Device OFF is enabled, Device is allowed to perform
 * transition to off mode as soon as all power domains in MPU, IVA
 * and CORE voltage are in OFF or OSWR state (open switch retention)
 */
void omap4_device_set_state_off(u8 enable)
{
#ifdef CONFIG_OMAP_ALLOW_OSWR
	if (enable)
		omap4_prminst_write_inst_reg(0x1 <<
				OMAP4430_DEVICE_OFF_ENABLE_SHIFT,
		OMAP4430_PRM_PARTITION, OMAP4430_PRM_DEVICE_INST,
		OMAP4_PRM_DEVICE_OFF_CTRL_OFFSET);
	else
#endif
		omap4_prminst_write_inst_reg(0x0 <<
				OMAP4430_DEVICE_OFF_ENABLE_SHIFT,
		OMAP4430_PRM_PARTITION, OMAP4430_PRM_DEVICE_INST,
		OMAP4_PRM_DEVICE_OFF_CTRL_OFFSET);
}

/**
 * omap4_device_prev_state_off:
 * returns true if the device hit OFF mode
 * This is API to check whether OMAP is waking up from device OFF mode.
 * There is no other status bit available for SW to read whether last state
 * entered was device OFF. To work around this, CORE PD, RFF context state
 * is used which is lost only when we hit device OFF state
 */
bool omap4_device_prev_state_off(void)
{
	u32 reg;

	reg = omap4_prminst_read_inst_reg(core_pwrdm->prcm_partition,
				core_pwrdm->prcm_offs,
				OMAP4_RM_L3_1_L3_1_CONTEXT_OFFSET)
		& OMAP4430_LOSTCONTEXT_RFF_MASK;

	return reg ? true : false;
}

void omap4_device_clear_prev_off_state(void)
{
	omap4_prminst_write_inst_reg(OMAP4430_LOSTCONTEXT_RFF_MASK |
				OMAP4430_LOSTCONTEXT_DFF_MASK,
				core_pwrdm->prcm_partition,
				core_pwrdm->prcm_offs,
				OMAP4_RM_L3_1_L3_1_CONTEXT_OFFSET);
}

/**
 * omap4_device_next_state_off:
 * returns true if the device next state is OFF
 * This is API to check whether OMAP is programmed for device OFF
 */
bool omap4_device_next_state_off(void)
{
	return omap4_prminst_read_inst_reg(OMAP4430_PRM_PARTITION,
			OMAP4430_PRM_DEVICE_INST,
			OMAP4_PRM_DEVICE_OFF_CTRL_OFFSET)
			& OMAP4430_DEVICE_OFF_ENABLE_MASK ? true : false;
}

/**
 * omap4_pm_init - Init routine for OMAP4 PM
 *
 * Initializes all powerdomain and clockdomain target states
 * and all PRCM settings.
 */
static int __init omap4_pm_init(void)
{
	int ret;
	struct clockdomain *emif_clkdm, *mpuss_clkdm, *l3_1_clkdm;
	struct clockdomain *ducati_clkdm, *l3_2_clkdm, *l4_per, *l4_cfg;

	if (!cpu_is_omap44xx())
		return -ENODEV;

	if (omap_rev() == OMAP4430_REV_ES1_0) {
		WARN(1, "Power Management not supported on OMAP4430 ES1.0\n");
		return -ENODEV;
	}

	pr_err("Power Management for TI OMAP4.\n");

	prcm_setup_regs();

	ret = request_irq(OMAP44XX_IRQ_PRCM,
			  (irq_handler_t)prcm_interrupt_handler,
			  IRQF_DISABLED, "prcm", NULL);
	if (ret) {
		printk(KERN_ERR "request_irq failed to register for 0x%x\n",
		       OMAP44XX_IRQ_PRCM);
		goto err2;
	}
	ret = pwrdm_for_each(pwrdms_setup, NULL);
	if (ret) {
		pr_err("Failed to setup powerdomains\n");
		goto err2;
	}

	/*
	 * The dynamic dependency between MPUSS -> MEMIF and
	 * MPUSS -> L3_* and DUCATI -> doesn't work as expected.
	 * The hardware recommendation is to keep above dependencies.
	 * Without this system locks up or randomly crashesh.
	 */
	mpuss_clkdm = clkdm_lookup("mpuss_clkdm");
	emif_clkdm = clkdm_lookup("l3_emif_clkdm");
	l3_1_clkdm = clkdm_lookup("l3_1_clkdm");
	l3_2_clkdm = clkdm_lookup("l3_2_clkdm");
	ducati_clkdm = clkdm_lookup("ducati_clkdm");
	l4_per = clkdm_lookup("l4_per_clkdm");
	l4_cfg = clkdm_lookup("l4_cfg_clkdm");
	if ((!mpuss_clkdm) || (!emif_clkdm) || (!l3_1_clkdm) ||
		(!l3_2_clkdm) || (!ducati_clkdm) || (!l4_per) || (!l4_cfg))
		goto err2;

	ret = clkdm_add_wkdep(mpuss_clkdm, emif_clkdm);
	ret |= clkdm_add_wkdep(mpuss_clkdm, l3_1_clkdm);
	ret |= clkdm_add_wkdep(mpuss_clkdm, l3_2_clkdm);
	ret |= clkdm_add_wkdep(ducati_clkdm, l3_1_clkdm);
	ret |= clkdm_add_wkdep(ducati_clkdm, l3_2_clkdm);
	ret |= clkdm_add_wkdep(mpuss_clkdm, l4_per);
	ret |= clkdm_add_wkdep(mpuss_clkdm, l4_cfg);
	ret |= clkdm_add_wkdep(ducati_clkdm, l4_per);
	ret |= clkdm_add_wkdep(ducati_clkdm, l4_cfg);
	if (ret) {
		pr_err("Failed to add MPUSS -> L3/EMIF, DUCATI -> L3"
				" and MPUSS -> L4* wakeup dependency\n");
		goto err2;
	}

	(void) clkdm_for_each(clkdms_setup, NULL);

	pr_info("OMAP4 PM: Static dependency added between MPUSS <-> EMIF"
		" MPUSS <-> L4_PER/CFG and MPUSS <-> L3_MAIN_1.\n");

	/* Get handles for VDD's for enabling/disabling SR */
	mpu_voltdm = voltdm_lookup("mpu");
	if (!mpu_voltdm) {
		pr_err("%s: Failed to get voltdm for VDD MPU\n", __func__);
		goto err2;
	}
	omap_vc_set_auto_trans(mpu_voltdm,
			       OMAP_VC_CHANNEL_AUTO_TRANSITION_DISABLE);

	iva_voltdm = voltdm_lookup("iva");
	if (!iva_voltdm) {
		pr_err("%s: Failed to get voltdm for VDD IVA\n", __func__);
		goto err2;
	}
	omap_vc_set_auto_trans(iva_voltdm,
			       OMAP_VC_CHANNEL_AUTO_TRANSITION_DISABLE);

	core_voltdm = voltdm_lookup("core");
	if (!core_voltdm) {
		pr_err("%s: Failed to get voltdm for VDD CORE\n", __func__);
		goto err2;
	}
	omap_vc_set_auto_trans(core_voltdm,
			       OMAP_VC_CHANNEL_AUTO_TRANSITION_DISABLE);

	ret = omap4_mpuss_init();
	if (ret) {
		pr_err("Failed to initialise OMAP4 MPUSS\n");
		goto err2;
	}

#ifdef CONFIG_SUSPEND
	suspend_set_ops(&omap_pm_ops);
#endif /* CONFIG_SUSPEND */

	mpu_pwrdm = pwrdm_lookup("mpu_pwrdm");
	cpu0_pwrdm = pwrdm_lookup("cpu0_pwrdm");
	core_pwrdm = pwrdm_lookup("core_pwrdm");
	per_pwrdm = pwrdm_lookup("l4per_pwrdm");

	 /* Enable wakeup for PRCM IRQ for system wide suspend */
	enable_irq_wake(OMAP44XX_IRQ_PRCM);

	/* Overwrite the default arch_idle() */
	pm_idle = omap_default_idle;

	omap4_idle_init();

	omap_pm_is_ready_status = true;
	/* let the other CPU know as well */
	smp_wmb();

err2:
	return ret;
}
late_initcall(omap4_pm_init);
