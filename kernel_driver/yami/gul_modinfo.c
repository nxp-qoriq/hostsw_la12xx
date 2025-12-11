/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
* Copyright 2022-2024 NXP
*
*/

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/mman.h>
#include <linux/fdtable.h>

#include "gul_base.h"
#include "gul_ipc_kern.h"
#include <gul_ipc_ioctl.h>
#include <gul_host_if.h>

#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/rcupdate.h>
#include <linux/eventfd.h>
#include <linux/irqreturn.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include <gul_modinfo.h>
#include "gul_dcs.h"

extern struct list_head pcidev_list;

extern int modem_share_buf_size;
extern int modem_host_data_size;
extern int modem_rf_data_size;

#define GUL_MODINFO_MINOR_CNT 1

static int gul_modinfo_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int rc;
	struct gul_dev *gul_dev = filp->private_data;
	phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;
	size_t size = vma->vm_end - vma->vm_start;


	if (!gul_dev) {
		return -EINVAL;
	}

	dev_dbg(gul_dev->dev, "request to mmap %llx:%lx\n", offset, size);

	if ((offset >= gul_dev->scratch_allocator.host_phys) &&
		((offset + size) <= (gul_dev->scratch_allocator.host_phys +
			modem_share_buf_size + modem_host_data_size + modem_rf_data_size))) {
		vma->vm_page_prot = pgprot_cached(vma->vm_page_prot);
		dev_dbg(gul_dev->dev,
			"request to mmap %llx:%lx marked cacheable\n",
			offset, size);
	} else
		return -EINVAL;

	rc = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, size,
				vma->vm_page_prot);
	return rc;
}

void gul_modinfo_get(struct gul_dev *gul_dev, modinfo_t *mi)
{
	int32_t i;
	uint32_t scratch_mod_phy_addr = 0;
	struct gul_hif *hif;

	mi->active = g_gul_global[gul_dev->id].active;
	sprintf(mi->name, "%s", g_gul_global[gul_dev->id].dev_name);
	hif = gul_dev->hif;
	mi->rev = gul_ep_get_svr();
	mi->id = gul_dev->id;
	mi->hif.host_phy_addr = gul_dev->mem_regions[GUL_MEM_REGION_PEBM].phys_addr + gul_dev->hif_offset;
	mi->hif.size = gul_dev->hif_size;

	mi->dcsr.host_phy_addr = gul_dev->mem_regions[GUL_MEM_REGION_DCSR].phys_addr;
	mi->dcsr.size = gul_dev->mem_regions[GUL_MEM_REGION_DCSR].size;

	mi->peb.host_phy_addr = gul_dev->mem_regions[GUL_MEM_REGION_PEBM].phys_addr;
	mi->peb.size = gul_dev->mem_regions[GUL_MEM_REGION_PEBM].size;

	mi->ccsr.host_phy_addr = gul_dev->mem_regions[GUL_MEM_REGION_CCSR].phys_addr;
	mi->ccsr.size = gul_dev->mem_regions[GUL_MEM_REGION_CCSR].size;

	mi->feca.host_phy_addr = gul_dev->mem_regions[GUL_MEM_REGION_FECA].phys_addr;
	mi->feca.size = gul_dev->mem_regions[GUL_MEM_REGION_FECA].size;

	mi->scratchbuf.host_phy_addr = gul_dev->scratch_allocator.host_phys;
	mi->scratchbuf.size = gul_dev->scratch_allocator.size;

	mi->clk_info.tbgen1_freq = ioread32be(&hif->tbgen_clk_info.tbgen1_freq_khz);
	mi->clk_info.tbgen2_freq = ioread32be(&hif->tbgen_clk_info.tbgen2_freq_khz);
	switch (ioread32be(&hif->tbgen_clk_info.tbgen1_clk_src)) {
	case LS_DCS_OUTPUT:
		sprintf(mi->clk_info.tbgen1_src, "%s", "LS_DCS_OUTPUT");
		break;
	case IPG_CLK:
		sprintf(mi->clk_info.tbgen1_src, "%s", "IPG_CLK");
		break;
	default:
		sprintf(mi->clk_info.tbgen1_src, "%s", "DISABLED");
	}
	switch (ioread32be(&hif->tbgen_clk_info.tbgen2_clk_src)) {
	case HS_DCS_OUTPUT:
		sprintf(mi->clk_info.tbgen2_src, "%s", "HS_DCS_OUTPUT");
		break;
	case IPG_CLK:
		sprintf(mi->clk_info.tbgen2_src, "%s", "IPG_CLK");
		break;
	default:
		sprintf(mi->clk_info.tbgen2_src, "%s", "DISABLED");
	}
	mi->clk_info.ls_adc_sps = ls_adc_sps;
	mi->clk_info.ls_dac_sps = ls_dac_sps;
	mi->clk_info.hs_dcs_sps = hif->hsdcs_sps;

	for (i = 0; i < GUL_SCRATCH_END; i++) {
		mi->scratchregions[i].modem_phy_addr = gul_dev->scratch_buf_region[i].phys_addr;
		if (i == 0)
			scratch_mod_phy_addr = mi->scratchregions[i].modem_phy_addr;
		mi->scratchregions[i].host_phy_addr =
			mi->scratchbuf.host_phy_addr + (mi->scratchregions[i].modem_phy_addr - scratch_mod_phy_addr);
		mi->scratchregions[i].size = gul_dev->scratch_buf_region[i].size;
	}
	mi->modem_host_uart = modem_host_uart;
}

static long gul_modinfo_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	int32_t ret = 0;
	struct gul_dev *gul_dev = filp->private_data;
	struct gul_hif *hif;

	modinfo_socrev_t mi_sr;
	modinfo_socrev_t *mi_sr_user = (modinfo_socrev_t *)arg;

	modinfo_t *mil_user = (modinfo_t *)arg;
	modinfo_t mil;
	modinfo_t *mi = &mil;

	switch (cmd) {
	case IOCTL_GUL_MODINFO_GET_SOCREV:
		ret = copy_from_user(&mi_sr, (modinfo_socrev_t *)arg, sizeof(modinfo_socrev_t));
		if (ret != 0) {
			dev_err(NULL, "gulmodinfo: copyfromuser failed\n");
			return -EFAULT;
		}

		hif = gul_dev->hif;
		mi_sr.rev = gul_ep_get_soc_rev();

		ret = copy_to_user(mi_sr_user, &mi_sr, sizeof(modinfo_socrev_t));
		if (ret != 0) {
			dev_err(NULL, "gulmodinfo: copytouser failed\n");
			return -EFAULT;
		}
		break;
	case IOCTL_GUL_MODINFO_GET:
		ret = copy_from_user(&mil, (modinfo_t *)arg, sizeof(modinfo_t));
		if (ret != 0) {
			dev_err(NULL, "gulmodinfo: copyfromuser failed\n");
			return -EFAULT;
		}

		gul_modinfo_get(gul_dev, mi);

		ret = copy_to_user(mil_user, &mil, sizeof(modinfo_t));
		if (ret != 0) {
			dev_err(NULL, "gulmodinfo: copytouser failed\n");
			return -EFAULT;
		}
		break;
	default:
		dev_err(NULL, "INVALID ioctl for gulmodinfo\n");
		return -EINVAL;
	}

return 0;
}

static int gul_modinfo_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static int gul_modinfo_open(struct inode *inode, struct file *filp)
{
	struct gul_dev *gul_dev;
	const char *file_name = file_dentry(filp)->d_iname;

	gul_dev = get_gul_dev_byname(file_name);

	if (!gul_dev) {
		return -EINVAL;
	}

	filp->private_data = (void *)gul_dev;

	return 0;
}

static const struct file_operations gul_modinfo_fops = {
	.owner		= THIS_MODULE,
	.open              = gul_modinfo_open,
	.release           = gul_modinfo_release,
	.mmap              = gul_modinfo_mmap,
	.unlocked_ioctl = gul_modinfo_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gul_modinfo_ioctl,
#endif
};

static struct miscdevice gul_modinfo_miscdev[MAX_MODEM];
int gul_modinfo_init(struct gul_dev *gul_dev)
{
	int rc = -1;
	struct miscdevice *miscdev;

	if (gul_dev->id >= MAX_MODEM) {
		dev_err(gul_dev->dev, "Invalid modem id : %d\n", gul_dev->id);
		return rc;
	}

	miscdev = &gul_modinfo_miscdev[gul_dev->id];

	miscdev->name = gul_dev->name;
	miscdev->fops = &gul_modinfo_fops;
	miscdev->minor = MISC_DYNAMIC_MINOR;

	rc = misc_register(miscdev);

	if (rc)
		dev_err(gul_dev->dev,
			"gul_modinfo: failed to register misc device\n");
	else
		dev_dbg(gul_dev->dev,
			"gul_modinfo: misc driver created with minor :%d\n",
			miscdev->minor);

	return rc;
}
EXPORT_SYMBOL_GPL(gul_modinfo_init);


int gul_modinfo_exit(struct gul_dev *gul_dev)
{
	if (gul_dev->id >= MAX_MODEM) {
		dev_err(gul_dev->dev, "Invalid modem id : %d\n", gul_dev->id);
		return -1;
	}
	misc_deregister(&gul_modinfo_miscdev[gul_dev->id]);
	return 0;
}
EXPORT_SYMBOL_GPL(gul_modinfo_exit);
