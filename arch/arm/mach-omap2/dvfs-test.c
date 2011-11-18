#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/plist.h>
#include <linux/slab.h>
#include <linux/opp.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <plat/common.h>
#include <plat/omap_device.h>
#include <plat/omap_hwmod.h>
#include <plat/clock.h>
#include "dvfs.h"
#include "smartreflex.h"
#include "powerdomain.h"
#include "pm.h"

#define DEFAULT_UNDER_TEST_DEV_NAME "gpu"

/* L3 device */
static struct device *under_test_dev;
/* test params */
static u32 test_iterations = 1;
static u32 test_delay_duration = 0;
/* some dummy device - useful for name */
static struct device some_device = {
	.init_name = "some dummy test device"
};
static char *under_test_oh_name;
static u8 num_opps;
static unsigned long *frequencies;

static int test_info_creator(void)
{
	int ret = 0;
	unsigned long f = 0;
	struct opp *o;

	if (!under_test_oh_name)
		return -EINVAL;

	under_test_dev = omap_hwmod_name_get_dev(under_test_oh_name);
	if (!under_test_dev) {
		pr_err("%s: Unable to get %s device pointer\n",__func__,
				under_test_oh_name);
		ret = -EINVAL;
		goto dead_meat;
	}

	dev_err(under_test_dev, "%s: UNDER TESTING\n", __func__);
	/* count up the opps */
	f = 0;
	num_opps = 0;
	do {
		rcu_read_lock();
		o = opp_find_freq_ceil(under_test_dev, &f);
		if (!IS_ERR_OR_NULL(o)) {
			dev_err(under_test_dev, "[%d]frequency = %ld\n",
					num_opps, f);
			num_opps++;
		}
		rcu_read_unlock();
		f += 1;
	} while (!IS_ERR_OR_NULL(o));

	if (!num_opps) {
		pr_err("%s: unable to find any OPP for device\n", __func__);
		ret = -ENODEV;
		goto dead_meat;
	}

	kfree(frequencies);
	frequencies = kmalloc(sizeof (unsigned long) * num_opps, GFP_KERNEL);
	if (!frequencies) {
		pr_err("%s: unable to allocate memory for %d freqs\n", __func__,
				num_opps);
		ret = -ENOMEM;
		goto dead_meat;
	}

	/* collect the opps */
	f = 0;
	num_opps = 0;
	do {
		rcu_read_lock();
		o = opp_find_freq_ceil(under_test_dev, &f);
		if (!IS_ERR_OR_NULL(o)) {
			frequencies[num_opps] = f;
			num_opps++;
		}
		rcu_read_unlock();
		f += 1;
	} while (!IS_ERR_OR_NULL(o));
dead_meat:
	return ret;
}

static int test_go_freq(unsigned long f)
{
	int ret;

	ret = omap_device_scale(&some_device, under_test_dev,
			f);
	if (ret) {
		dev_err(under_test_dev, "%s: Scale for %ld freq"
			"failed(%d)!\n", __func__, f, ret);
		return ret;
	}
	/* in usecs.. */
	if (test_delay_duration < 1000)
		udelay(test_delay_duration);
	else
		msleep(DIV_ROUND_UP(test_delay_duration, 1000));
	return 0;
}

static int test_start(void *data, u64 val)
{
	u8 idx, idx1;
	int ret;
	int sample_iterations = test_iterations;

	ret = test_info_creator();
	if (ret) {
		pr_err("%s: unable to create test params %d\n",
				__func__, ret);
		return ret;
	}
	if (!under_test_dev) {
		pr_err("%s: no test device!\n", __func__);
		return -ENODEV;
	}

	if (!frequencies) {
		pr_err("%s: no frequencies!\n", __func__);
		return -ENODEV;
	}

	ret = pm_runtime_get_sync(under_test_dev);
	if (!ret) {
		dev_err(under_test_dev, "get_sync failed=%d\n", ret);
		return ret;
	}
	if (!sample_iterations) {
		sample_iterations = 0xFFFFFFFF;
		dev_err(under_test_dev, "re-routing to max iterations %u\n",
				sample_iterations);
	}

	while (sample_iterations) {
		for (idx = 0; idx < num_opps; idx++) {
			for (idx1 = 0; idx1 < num_opps; idx1++) {
				ret = test_go_freq(frequencies[idx]);
				if (ret)
					goto bad;
				ret = test_go_freq(frequencies[idx1]);
				if (ret)
					goto bad;
			}
		}
		if (!(sample_iterations % 5000))
			dev_err(under_test_dev, "test iteration: %u\n",
				sample_iterations);
		sample_iterations--;
	}
bad:
	pm_runtime_put_sync(under_test_dev);

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(test_fops, NULL, test_start, "%llu\n");

static int __init test_init(void)
{

	under_test_oh_name = kasprintf(GFP_KERNEL, "%s", DEFAULT_UNDER_TEST_DEV_NAME);
	if (!under_test_oh_name) {
		pr_err("%s: could not put the name\n", __func__);
		return -ENOMEM;
	}

	debugfs_create_u32("test_iterations", S_IRUGO | S_IWUGO, NULL,
				 &test_iterations);
	debugfs_create_u32("test_delay_duration", S_IRUGO | S_IWUGO, NULL,
				 &test_delay_duration);
	debugfs_create_file("test_start", S_IRUGO|S_IWUSR, NULL,
			NULL, &test_fops);
	return 0;
}
late_initcall(test_init);

static void __exit test_exit(void)
{
	kfree(frequencies);
	kfree(under_test_oh_name);
}
module_exit(test_exit);
