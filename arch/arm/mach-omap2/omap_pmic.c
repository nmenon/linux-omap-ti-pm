/*
 * Registration hooks for PMICs used with OMAP
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *	Nishanth Menon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>

#include "voltage.h"

#include "pm.h"

/**
 * omap_pmic_data_init() - trigger point for all PMIC initializers
 */
void __init omap_pmic_data_init(void)
{
	omap_twl_init();
	omap_tps6236x_init();
}

/**
 * omap_pmic_register_data() - Register the PMIC information to OMAP mapping
 * @omap_pmic__maps:	array ending with a empty element representing the maps
 */
int __init omap_pmic_register_data(struct omap_pmic_map *omap_pmic_maps)
{
	struct voltagedomain *voltdm;
	struct omap_pmic_map *map;
	int r;

	if (!omap_pmic_maps)
		return 0;

	map = omap_pmic_maps;

	while(map->name) {
		if (!omap_chip_is(map->omap_chip))
			goto next;

		voltdm = voltdm_lookup(map->name);
		if (IS_ERR_OR_NULL(voltdm)) {
			pr_err("%s: unable to find map %s\n",__func__,
				map->name);
			goto next;
		}
		if (IS_ERR_OR_NULL(map->pmic_data)) {
			pr_warning("%s: domain[%s] has no pmic data\n",__func__,
				map->name);
			goto next;
		}

		r = omap_voltage_register_pmic(voltdm, map->pmic_data);
		if (r) {
			pr_warning("%s: domain[%s] register returned %d\n",
					__func__, map->name, r);
			goto next;
		}
		if (map->special_action) {
			r = map->special_action(voltdm);
			WARN(r, "%s: domain[%s] action returned %d\n", __func__,
				map->name, r);
		}
next:
		map++;
	}

	return 0;
}
