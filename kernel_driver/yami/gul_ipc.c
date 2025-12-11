/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2023 NXP
 */
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/unistd.h>

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

#define DEVICE_NAME_LEN		32
#define GUL_MINOR_START		0
#define IPC_HOST_SIGNATURE	0x10101010

#define IPC_CTRL_AREA_SIZE	0x4000	/* 16KB */
#define RINGS_PER_CHANNEL	2	/* 1 Rx Ring, 1Tx Ring */
#define BD_PER_RING		4

#define RATTLER_IPC_MEM_OFFSET  0x19000

#define GUL_IPC_BLOCK_TIMEOUT	5	/* 5 msec */

#define GUL_IPC_IRQ_NUM_TXT_SIZE	64

static uint32_t gul_ipc_major;
static uint32_t gul_ipc_minor;
static dev_t gul_ipc_devnr;
static uint32_t gul_nr_dev;

static uint8_t ipc_minor_index;
static uint8_t in_use_minor[MAX_MODEM];

/*======== IPC Meta Data =============*/

struct bd {
	uint8_t *msg_ptr;
#ifndef CONFIG_64BIT
	uint32_t pad1;
#endif
	uint32_t msg_len;
	uint32_t pad2;
};

struct ring {
	uint32_t pi;
	uint32_t ci;
	uint32_t pc;
	uint32_t cc;
	struct bd *bd;
#ifndef CONFIG_64BIT
	uint32_t pad;
#endif
};

struct ipc_channel {
	uint32_t chid;
	uint32_t pad1;
	struct ring *rx_ring;
#ifndef CONFIG_64BIT
	uint32_t pad2;
#endif
	struct ring *tx_ring;
#ifndef CONFIG_64BIT
	uint32_t pad3;
#endif
};

struct ipc_metadata_kernel {
	uint32_t ipc_host_signature; /**< IPC host signature, Set by host/L2 */
	uint32_t ipc_geul_signature; /**< IPC geul signature, Set by modem */
} __attribute__((__packed__));

/*=========== IPC Meta Data End ==========*/


/*========= Internal Data Structure ======*/

struct gul_pci_alloc_cb {
	u64 m_addr;
	u64 h_addr;
	int size;
	int valid;
	int iatu_idx;
};

struct gul_pci_allocator_s {
	unsigned int	pci_avail_space;
	unsigned int	pci_total_space;
	u64		pci_alloc_addr;
	int		pci_alloc_cnt;
	struct gul_pci_alloc_cb cbs[MAX_PCI_USER_ALLOC_COUNT];
	int iatu_idx_map[MAX_PCI_USER_ALLOC_COUNT];
};

struct ipc_Irq {
	int free;
	int irq_num;
	int msi_value;
	int irq_index;
	char *irq_num_txt;
};

struct ipc_chan {
	struct eventfd_ctx *evt_fd_ctxt;
	struct ipc_Irq ipc_irq;
};

struct gul_ipc_dev {
	char name[DEVICE_NAME_LEN];

	/* Rattler Device */
	struct gul_dev *gul_dev;

	/* Linux charatre device for GUL IPC */
	struct cdev cdev;

	/* GUL IPC Device Number */
	dev_t gul_ipc_devnr;

	/* IPC device minor number*/
	uint8_t minor;

	/* IPC channel and IRQ mapping */
	struct ipc_chan asyn_chans[IPC_MAX_CHANNEL_COUNT];

	sys_map_t sys_map;
	struct gul_pci_allocator_s pci_allocator;
};

/*========= Internal Data Structure  End ======*/

/*========= IPC IRQ Handler  Start ======*/

static irqreturn_t ipc_irq_handler(int irq, void *data)
{
	#define SIGNAL_TO_CHANNEL_LISTENER	1

	struct ipc_chan *ipc_chan = (struct ipc_chan *)data;

	/* Send signal to IPC Channel Listner */
	if (ipc_chan)
		eventfd_signal(ipc_chan->evt_fd_ctxt,
				SIGNAL_TO_CHANNEL_LISTENER);

	return IRQ_HANDLED;
}

/*========= IPC IRQ Handler  End ======*/

static int gul_ipc_open(struct inode *inode, struct file *filp)
{
	struct gul_ipc_dev *dev = container_of(inode->i_cdev,
					       struct gul_ipc_dev, cdev);

	filp->private_data = dev;

	return 0;
}

static int gul_ipc_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;

	return 0;
}

static void gul_ipc_create_hugepage_outbound(struct gul_dev *gul_dev,
					int idx, u64 pci_addr,
					u64 cpu_addr, int size)
{
	struct gul_mem_region_info *ccsr_region;

	ccsr_region = &gul_dev->mem_regions[GUL_MEM_REGION_CCSR];

	ls_pcie_iatu_outbound_set(ccsr_region->vaddr + PCIE_RHOM_DBI_BASE,
		idx, PCIE_ATU_TYPE_MEM,
		cpu_addr,/* Modem cpu addr*/
		pci_addr,/* Host Physical address*/
		size);

	dev_info(gul_dev->dev, "Huge Page Buff:0x%llx[H]-0x%llx[M],siz %d, dbi_idx: %d\n",
		 pci_addr, cpu_addr, size, idx);

}

int register_ipc_channel_irq(struct gul_dev *gul_dev, int ipc_ch_num,
				uint32_t fd)
{
	int ret = 0, irq_num = 0, index = 0;
	int ipc_interrupt_flags = IRQF_TRIGGER_RISING;
	struct task_struct *userspace_task = NULL;
	struct file *efd_file = NULL;
	struct ipc_chan *ipc_chan = NULL;


	if (!gul_dev) {
		dev_err(NULL,
		"%s : error : gul_dev is NULL\n", __func__);
		ret = -EINVAL;
		goto err;
	} else if (!gul_dev->ipc_priv) {
		dev_err(gul_dev->dev,
		"%s : error : ipc_priv is NULL\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	if (ipc_ch_num < 0 || ipc_ch_num >= IPC_MAX_CHANNEL_COUNT) {
		dev_err(gul_dev->dev,
		"%s : error : Invalid IPC channel number\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	ipc_chan = &((struct gul_ipc_dev *)
		(gul_dev->ipc_priv))->asyn_chans[ipc_ch_num];

	/* Register Async Notifications as per free IRQ */
	index = gul_dev_get_msi(gul_dev);
	if (index >= 0 && index < GUL_MSI_MAX_CNT) {
		ipc_chan->ipc_irq.irq_num = gul_dev->irq[index].irq_val;
		ipc_chan->ipc_irq.msi_value = gul_dev->irq[index].msi_val;
		ipc_chan->ipc_irq.free = gul_dev->irq[index].free;
		ipc_chan->ipc_irq.irq_index = index;
	} else {
		/* Return error : No free IRQ line */
		ret = -EBUSY;
		goto err;
	}

	/* Get current task context from which IOCTL was called */
	userspace_task = current;

	rcu_read_lock();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	efd_file = files_lookup_fd_rcu(userspace_task->files, fd);
#else
	efd_file = fcheck_files(userspace_task->files, fd);
#endif
	rcu_read_unlock();

	ipc_chan->evt_fd_ctxt = eventfd_ctx_fileget(efd_file);
	if (ipc_chan->evt_fd_ctxt != NULL) {
		/* IRQ request */
		irq_num = ipc_chan->ipc_irq.irq_num;
		ipc_chan->ipc_irq.irq_num_txt = kmalloc(GUL_IPC_IRQ_NUM_TXT_SIZE, GFP_KERNEL);
		if (!ipc_chan->ipc_irq.irq_num_txt) {
			dev_err(gul_dev->dev, "no memory\n");
			goto err;
		}
		sprintf(ipc_chan->ipc_irq.irq_num_txt, "ipc_ch_irq_%d", irq_num);
		ret = request_irq(irq_num, ipc_irq_handler,
			ipc_interrupt_flags, ipc_chan->ipc_irq.irq_num_txt, ipc_chan);
		if (ret < 0) {
			dev_err(gul_dev->dev,
				"%s request irq err - %d\n", __func__, ret);
			goto err;
		}
	} else {
		dev_err(gul_dev->dev,
			"%s: efd file(0x%px) or evt_fd_ctxt(0x%px) is invalid\n",
			__func__, efd_file, ipc_chan->evt_fd_ctxt);
		ret = -EINVAL;
		goto err;
	}
err:
	return ret;
}

void deregister_ipc_channel_irq(struct gul_dev *gul_dev, int ipc_ch_num)
{
	struct ipc_chan *ipc_chan;

	if (ipc_ch_num < 0 || ipc_ch_num >= IPC_MAX_CHANNEL_COUNT) {
		dev_err(gul_dev->dev,
		"%s : error : Invalid IPC channel number\n", __func__);
		return;
	}

	ipc_chan = &((struct gul_ipc_dev *)
			(gul_dev->ipc_priv))->asyn_chans[ipc_ch_num];

	if (ipc_chan->ipc_irq.free == GUL_MSI_IRQ_BUSY) {
		/* IRQ request */
		kfree(ipc_chan->ipc_irq.irq_num_txt);
		free_irq(ipc_chan->ipc_irq.irq_num, ipc_chan);
		gul_dev_put_msi(gul_dev, ipc_chan->ipc_irq.irq_index);
		ipc_chan->ipc_irq.free = gul_dev->irq[ipc_chan->ipc_irq.irq_index].free;
		ipc_chan->ipc_irq.irq_index = 0;
	}
}

static int gul_pci_get_iatu_idx(struct gul_ipc_dev *ipc_dev)
{
	int idx = 0;
	struct gul_pci_allocator_s *alctr = &ipc_dev->pci_allocator;

	while (idx < MAX_PCI_USER_ALLOC_COUNT) {
		if (alctr->iatu_idx_map[idx] == 0) {
			alctr->iatu_idx_map[idx] = 1;
			return idx;
		}

		idx++;
	}

	return -1;
}

static void gul_pci_put_iatu_idx(struct gul_ipc_dev *ipc_dev, int idx)
{
	struct gul_pci_allocator_s *alctr = &ipc_dev->pci_allocator;

	alctr->iatu_idx_map[idx] = 0;
}

static int gul_setup_pci_space(struct gul_ipc_dev *ipc_dev)
{
	struct gul_pci_allocator_s *alctr = &ipc_dev->pci_allocator;
	struct gul_hif *hif = ipc_dev->gul_dev->hif;
	struct host_mem_region *region;

	region = &hif->host_regions[HOST_MEM_HUGE_PAGE_BUF];
	memset(alctr, 0, sizeof(struct gul_pci_allocator_s));
	alctr->pci_avail_space = readl(&region->size_l);
	alctr->pci_total_space = readl(&region->size_l);
	alctr->pci_alloc_addr = ((uint64_t)readl(&region->mod_phys_h) << 32) |
				readl(&region->mod_phys_l);

	dev_info(ipc_dev->gul_dev->dev,
		"%s: Modem PCI space start : %llx size : 0x%x\n",
		__func__, alctr->pci_alloc_addr, alctr->pci_avail_space);

	return 0;
}

static int gul_ipc_pci_map_alloc(struct gul_ipc_dev *ipc_dev,
			u64 h_addr, u32 *addr, int size)
{
	struct gul_pci_allocator_s *alctr = &ipc_dev->pci_allocator;
	struct gul_dev *gul_dev = ipc_dev->gul_dev;
	struct gul_pci_alloc_cb *cb;

	if (size > alctr->pci_avail_space) {
		dev_err(gul_dev->dev,
			"%s: Insufficient PCI space\n",  __func__);
		return -1;
	}

	if (alctr->pci_alloc_cnt >= MAX_PCI_USER_ALLOC_COUNT) {
		dev_err(gul_dev->dev,
			"%s: Maximum PCI allocations done %d\n",
			__func__, alctr->pci_alloc_cnt);
		return -1;
	}


	*addr = alctr->pci_alloc_addr;
	alctr->pci_avail_space -= size;
	alctr->pci_alloc_addr += size;
	cb = &alctr->cbs[alctr->pci_alloc_cnt];

	cb->iatu_idx = gul_pci_get_iatu_idx(ipc_dev);

	if (cb->iatu_idx < 0) {
		dev_err(gul_dev->dev,
			"%s: Failed to allocate iATU entry\n",  __func__);
		return -1;
	}

	alctr->pci_alloc_cnt++;

	cb->m_addr = *addr;
	cb->h_addr = h_addr;
	cb->size = size;
	cb->valid = 1;

	dev_info(gul_dev->dev, "%s:%d alloc addr %x size %x cnt %d\n",
		__func__, __LINE__, *addr, size, alctr->pci_alloc_cnt);

	gul_ipc_create_hugepage_outbound(gul_dev,
		GUL_OB_WIN_HUGE_PAGE_BUFS + cb->iatu_idx,
		h_addr, *addr, size);

	return 0;
}

static void gul_ipc_pci_map_free_gc(struct gul_ipc_dev *ipc_dev)
{
	struct gul_pci_allocator_s *alctr = &ipc_dev->pci_allocator;
	int ii;
	struct gul_pci_alloc_cb *cb;

	ii = alctr->pci_alloc_cnt;

	while (ii) {
		cb = &alctr->cbs[ii - 1];
		if (cb->valid)
			break;

		/**
		 * waiting for to be added free pool.
		 * Right now, it is stack approch.
		 */
		/* TODO This check may not be reqyured */
		if ((cb->m_addr + cb->size) == alctr->pci_alloc_addr) {
			alctr->pci_alloc_addr = cb->m_addr;
			alctr->pci_avail_space += cb->size;
			gul_pci_put_iatu_idx(ipc_dev, cb->iatu_idx);
		} else {
			dev_err(ipc_dev->gul_dev->dev,
				"%s:%d Top addr is not matching %llx : %llx\n",
				__func__, __LINE__,
				alctr->pci_alloc_addr,
				cb->m_addr + cb->size);
			break;
		}
		ii--;

	}

	alctr->pci_alloc_cnt = ii;
}

static int gul_ipc_pci_map_free(struct gul_ipc_dev *ipc_dev, u64 addr)
{
	struct gul_pci_allocator_s *alctr = &ipc_dev->pci_allocator;
	int ii = 0;
	struct gul_pci_alloc_cb *cb;

	for (; ii < alctr->pci_alloc_cnt; ii++) {
		cb = &alctr->cbs[ii];
		if ((cb->h_addr == addr) && cb->valid) {
			cb->valid = 0;
			break;
		}
	}

	if (ii == alctr->pci_alloc_cnt) {
		dev_err(ipc_dev->gul_dev->dev,
			"%s: Inivalid PCI space addr(%llx)\n",
			__func__, addr);
		return -1;
	}

	gul_ipc_pci_map_free_gc(ipc_dev);

	return 0;
}

static int gul_ipc_pci_map_is_overlap(struct gul_ipc_dev *ipc_dev,
						uint64_t addr, uint32_t size)
{
	struct gul_pci_allocator_s *alctr = &ipc_dev->pci_allocator;
	int ii = 0;
	struct gul_pci_alloc_cb *cb;

	for (; ii < alctr->pci_alloc_cnt; ii++) {
		cb = &alctr->cbs[ii];

		if (((addr > cb->h_addr) &&
			(addr < (cb->h_addr + cb->size))) ||
			(((addr + size) > cb->h_addr) &&
			((addr + size) < (cb->h_addr + cb->size)))) {
			dev_err(ipc_dev->gul_dev->dev,
				"%s: overlapping address space %llx:%llx",
				__func__, addr, cb->h_addr);
			return -1;
		}
	}

	return 0;
}

static long gul_ipc_ioctl(struct file *filp, unsigned int cmd,
			 unsigned long arg)
{
	int ret = 0, msi_id, ii;
	ipc_eventfd_t ipc_channel = {0};
	sys_map_t sys_map;
	sys_map_t *sys_map_user = (sys_map_t *)arg;
	ipc_pci_map_query_t pci_map;
	mem_strt_addr_t mem_desc;
	struct gul_ipc_dev *ipc_dev = (struct gul_ipc_dev *)filp->private_data;
	struct gul_dev *gul_dev = ipc_dev->gul_dev;
	struct gul_pci_allocator_s *alctr = &ipc_dev->pci_allocator;

	switch (cmd) {

	case  IOCTL_GUL_IPC_QUERY_PCI_MAP:
		pci_map.cur_addr = alctr->pci_alloc_addr;
		pci_map.mem_avail = alctr->pci_avail_space;
		pci_map.mem_total = alctr->pci_total_space;
		pci_map.alloc_cnt = alctr->pci_alloc_cnt;

		/* Dump all active and on-hold  mappings */
		for (ii = 0; ii < pci_map.alloc_cnt; ii++) {
			pci_map.list[ii].host_phys = alctr->cbs[ii].h_addr;
			pci_map.list[ii].modem_phys = alctr->cbs[ii].m_addr;
			pci_map.list[ii].size = alctr->cbs[ii].size;
		}

		ret = copy_to_user((void *)arg, (void *)&pci_map,
					sizeof(ipc_pci_map_query_t));
		if (ret != 0)
			return -EFAULT;
		break;

	case  IOCTL_GUL_IPC_PUT_PCI_MAP:
		ret = copy_from_user(&mem_desc,
			(mem_strt_addr_t *)arg, sizeof(mem_strt_addr_t));
		if (ret != 0)
			return -EFAULT;

		ret = gul_ipc_pci_map_free(ipc_dev, mem_desc.host_phys);
		if (ret != 0)
			return -EFAULT;
		break;

	case  IOCTL_GUL_IPC_GET_PCI_MAP:
		ret = copy_from_user(&mem_desc,
					(mem_strt_addr_t *)arg,
					sizeof(mem_strt_addr_t));
		if (ret != 0)
			return -EFAULT;

		if (gul_ipc_pci_map_is_overlap(ipc_dev,
			mem_desc.host_phys, mem_desc.size) < 0)
			return -EINVAL;

		if (gul_ipc_pci_map_alloc(ipc_dev, mem_desc.host_phys,
			&mem_desc.modem_phys, mem_desc.size) < 0)
			return -ENOMEM;

		ret = copy_to_user((mem_strt_addr_t *)arg, &mem_desc,
					sizeof(mem_strt_addr_t));
		if (ret != 0)
			return -EFAULT;
		break;

	case  IOCTL_GUL_IPC_GET_SYS_MAP:
		ret = copy_from_user(&sys_map,
				(sys_map_t *)arg, sizeof(sys_map_t));
		if (ret != 0)
			return -EFAULT;


		if (gul_dev) {
			sys_map.mhif_start.host_phys =
			gul_dev->mem_regions[GUL_MEM_REGION_PEBM].phys_addr +
			gul_dev->hif_offset;
			sys_map.mhif_start.size = gul_dev->hif_size;

			sys_map.peb_start.host_phys =
			gul_dev->mem_regions[GUL_MEM_REGION_PEBM].phys_addr;
			sys_map.peb_start.size =
			gul_dev->mem_regions[GUL_MEM_REGION_PEBM].size;

			sys_map.modem_ccsrbar.host_phys =
			gul_dev->mem_regions[GUL_MEM_REGION_CCSR].phys_addr;
			sys_map.modem_ccsrbar.size =
				gul_dev->mem_regions[GUL_MEM_REGION_CCSR].size;

			memcpy(&ipc_dev->sys_map, &sys_map, sizeof(sys_map_t));
			ret = copy_to_user(sys_map_user, &sys_map,
						sizeof(sys_map_t));
			if (ret != 0)
				return -EFAULT;

			/* All is done. Set Host Ready now. */
			SET_HIF_HOST_RDY(gul_dev->hif, HIF_HOST_READY_IPC_LIB);
		}
	break;
	/* Register IRQ for PCI channel passed and
	 *  return MSI Number to calling task
	 */
	case  IOCTL_GUL_IPC_CHANNEL_REGISTER:
		ret = copy_from_user(&ipc_channel, (ipc_eventfd_t *)arg,
				sizeof(ipc_eventfd_t));
		if (ret != 0)
			return -EFAULT;

		ret = register_ipc_channel_irq(gul_dev,
						ipc_channel.ipc_channel_num,
						ipc_channel.efd);
		if (ret < 0)
			return -EFAULT;

		/* Return msi_value to caller (mapped to IRQ) */
		ipc_channel.msi_value =
			ipc_dev->asyn_chans
			[ipc_channel.ipc_channel_num].ipc_irq.msi_value;
		ret = copy_to_user((void *)arg, (void *)&ipc_channel,
					sizeof(ipc_eventfd_t));
		if (ret != 0)
			return -EFAULT;
	break;

	/* De-register PCI channel passed and free up associated IRQ */
	case IOCTL_GUL_IPC_CHANNEL_DEREGISTER:
		ret = copy_from_user(&ipc_channel,
			(ipc_eventfd_t *)arg, sizeof(ipc_eventfd_t));
		if (ret != 0)
			return -EFAULT;

		deregister_ipc_channel_irq(gul_dev,
			ipc_channel.ipc_channel_num);
	break;

	case IOCTL_GUL_IPC_CHANNEL_RAISE_INTERRUPT:
		/* Raise requested MSI for channel.*/
		ret = copy_from_user(&msi_id, (int *)arg, sizeof(int));
		if (ret != 0)
			return -EFAULT;

		raise_modem_msi(gul_dev, MSI_TYPE_A, msi_id);
	break;

	default:
		ret = -ENOTTY;
	}

	return ret;
}
static const struct file_operations gul_ipc_fops = {
	.owner = THIS_MODULE,
	.open =	gul_ipc_open,
	.release = gul_ipc_release,
	.unlocked_ioctl = gul_ipc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gul_ipc_ioctl,
#endif
};

static bool is_geul_modem_initialized(struct gul_ipc_dev *ipc_dev)
{
	struct ipc_metadata_kernel *ipcmetadata =
		(struct ipc_metadata_kernel *)
		&ipc_dev->gul_dev->hif->ipc_regs.ipc_mdata_offset;

	if (ipcmetadata->ipc_geul_signature) {
		ipcmetadata->ipc_host_signature = IPC_HOST_SIGNATURE;
		return true;
	}

	return false;
}

static int
_dump_stats_per_channel(struct gul_ipc_ch_stats *stats, char *buf, int len)
{
	len += sprintf((buf + len), "IPC: recvd = %8u  sent = %8u\n",
	       stats->num_of_msg_recved,
	       stats->num_of_msg_sent);
	len += sprintf((buf + len), "IPC: total_message_len = %u\n",
	       stats->total_msg_length);
	len += sprintf((buf + len), "\tIPC: input_invalid: %u\n",
						stats->err_input_invalid);
	len += sprintf((buf + len), "\tIPC: channel_invalid: %u\n",
						stats->err_channel_invalid);
	len += sprintf((buf + len), "\tIPC: Not implemented: %u\n",
						stats->err_not_implemented);
	len += sprintf((buf + len), "\tIPC: invalid_memory: %u\n",
						stats->err_mem_invalid);
	len += sprintf((buf + len), "\tIPC: channel_full: %u\n",
						stats->err_channel_full);
	len += sprintf((buf + len), "\tIPC: channel_empty: %u\n",
						stats->err_channel_empty);
	len += sprintf((buf + len), "\tIPC: buf_full: %u\n",
						stats->err_buf_list_full);
	len += sprintf((buf + len), "\tIPC: buf_empty: %u\n",
						stats->err_buf_list_empty);
	len += sprintf((buf + len), "\tIPC: Buf_alloc_failed: %u\n",
						stats->err_host_buf_alloc_fail);
	len += sprintf((buf + len), "\tIPC: ioctl_failed: %u\n",
						stats->err_ioctl_fail);
	len += sprintf((buf + len), "\tIPC: eventfd_reg_failed: %u\n",
						stats->err_efd_reg_fail);
	return len;
}

static ssize_t ipc_show_stats(void *stats_args, char *buf, void *devp)
{
	int len = 0;
	int i;
	struct gul_dev *gul_dev = (struct gul_dev *)devp;
	struct gul_stats *stats = (struct gul_stats *) stats_args;
	struct gul_ipc_stats *m_ipc_stats = &stats->m_ipc_stats;
	struct gul_ipc_stats *h_ipc_stats = &stats->h_ipc_stats;

	if ((gul_dev->stats_desc.stats_control &
		(1 << HOST_CONTROL_IPC_STATS)) == 0) {
		dev_err(gul_dev->dev,
			"HOST_CONTROL_IPC_STATS not set - stats_conrol 0x%X\n",
					gul_dev->stats_desc.stats_control);
		return 0;
	}

/* TODO How to get this value */
#define CHANNELS_IN_USE 8

	len += sprintf((buf + len), "\nIPC: ##### HOST common stats  ######\n");
	len += sprintf((buf + len), "IPC: Invalid Instance = %u\n",
					h_ipc_stats->err_instance_invalid);
	len += sprintf((buf + len), "IPC: Metadata Size mismatch = %u\n",
					h_ipc_stats->err_md_sz_mismatch);
	len += sprintf((buf + len), "IPC: Invalid Memory= %u\n",
					h_ipc_stats->err_mem_invalid);
	len += sprintf((buf + len), "IPC: Invalid input = %u\n",
					h_ipc_stats->err_input_invalid);
	len += sprintf((buf + len), "\nIPC: ##### MODEM common stats ######\n");
	len += sprintf((buf + len), "IPC: Invalid Instance = %u\n",
					m_ipc_stats->err_instance_invalid);
	len += sprintf((buf + len), "IPC: Metadata Size mismatch = %u\n",
					m_ipc_stats->err_md_sz_mismatch);
	len += sprintf((buf + len), "IPC: Invalid Memory= %u\n",
					h_ipc_stats->err_mem_invalid);
	len += sprintf((buf + len), "IPC: Invalid input = %u\n",
					h_ipc_stats->err_input_invalid);
	len += sprintf((buf + len), "IPC: ---------------------------------\n");
	len += sprintf((buf + len), "IPC: ---- Per Channel Stats ----------\n");
	len += sprintf((buf + len), "IPC: ---------------------------------\n");

	for (i = 0; i < CHANNELS_IN_USE; i++) {
		len += sprintf((buf + len), "IPC: ---- For Channel %d ---\n",
									i);
		len += sprintf((buf + len), "IPC: ##### HOST Stats ######\n");
		len = _dump_stats_per_channel(&h_ipc_stats->ipc_ch_stats[i],
								buf, len);
		len += sprintf((buf + len), "\n");
		len += sprintf((buf + len), "IPC: ##### MODEM Stats ######\n");
		len = _dump_stats_per_channel(&m_ipc_stats->ipc_ch_stats[i],
								buf, len);
		len += sprintf((buf + len), "IPC: -------------------------\n");
	}

	return len;
}

static void ipc_reset_stats(void *stats_args)
{
	struct gul_stats *stats = (struct gul_stats *) stats_args;
	struct gul_ipc_stats *m_ipc_stats = &stats->m_ipc_stats;
	struct gul_ipc_stats *h_ipc_stats = &stats->h_ipc_stats;

	printk(KERN_INFO "------ Resetting IPC Stats --------\n");
	memset_io(h_ipc_stats, 0, sizeof(struct gul_ipc_stats));
	memset_io(m_ipc_stats, 0, sizeof(struct gul_ipc_stats));

}
static int gul_ipc_stats_init(struct gul_dev *gul_dev)
{
	struct gul_stats_ops ipc_stats_ops;
	struct gul_stats *stats = (struct gul_stats *) &(gul_dev->hif->stats);

	ipc_stats_ops.gul_show_stats = ipc_show_stats;
	ipc_stats_ops.gul_reset_stats = ipc_reset_stats;
	ipc_stats_ops.stats_args = (void *)stats;

	return gul_host_add_stats(gul_dev, &ipc_stats_ops);
}

static int gul_ipc_create_cdev(struct gul_ipc_dev *ipc_dev)
{
	int ret = 0;

	cdev_init(&ipc_dev->cdev, &gul_ipc_fops);
	ipc_dev->cdev.owner = THIS_MODULE;
	ipc_dev->cdev.ops = &gul_ipc_fops;
	ret = cdev_add(&ipc_dev->cdev, ipc_dev->gul_ipc_devnr, 1);
	if (ret)
		printk(KERN_CRIT "Error %d adding %s\n", ret, ipc_dev->name);

	return ret;
}

ipc_handle_t gul_ipc_get_handle(const char *dev_name)
{
	struct gul_ipc_dev *dev;
	struct gul_dev *gul_dev;

	gul_dev = get_gul_dev_byname(dev_name);
	if (!gul_dev) {
		printk(KERN_CRIT "%s: Device (%s) do not exists\n",
			__func__, dev_name);
		return NULL;
	}

	dev = ((struct gul_dev *)gul_dev)->ipc_priv;

	if (is_geul_modem_initialized(dev))
		return dev;

	return NULL;
}
EXPORT_SYMBOL_GPL(gul_ipc_get_handle);

static int gul_ipc_pci_map_dump(struct gul_ipc_dev *ipc_dev,  char *buf)
{
	struct gul_pci_allocator_s *alctr = &ipc_dev->pci_allocator;
	int ii = 0;
	struct gul_pci_alloc_cb *cb;

	sprintf(&buf[strlen(buf)], " ipc_pci avail_space:0x%x total :0x%x (%dMB)\n",
		alctr->pci_avail_space, alctr->pci_total_space,
		IN_MB(alctr->pci_total_space));
	sprintf(&buf[strlen(buf)], " ipc_pci alloc addr:0x%llx cnt :%d\n",
		alctr->pci_alloc_addr, alctr->pci_alloc_cnt);

	for (; ii < alctr->pci_alloc_cnt; ii++) {
		cb = &alctr->cbs[ii];
			sprintf(&buf[strlen(buf)], " iatu_id:%d: 0x%llx:0x%x (%dMB)\n",
			alctr->iatu_idx_map[ii],
			cb->h_addr, cb->size, IN_MB(cb->size));
	}

	return strlen(buf);
}

ssize_t gul_ipc_show(struct gul_dev *dev, char *buf)
{
	struct gul_ipc_dev *ipc_dev = dev->ipc_priv;
	int i;

	sprintf(&buf[strlen(buf)], "*****ModemID %d (%s)\n", dev->id, ipc_dev->name);

	if (ipc_dev) {
		sprintf(&buf[strlen(buf)], " hugepg host:0x%llx modem:0x%x size :0x%x\n",
		ipc_dev->sys_map.hugepg_start.host_phys,
		ipc_dev->sys_map.hugepg_start.modem_phys,
		ipc_dev->sys_map.hugepg_start.size);
		gul_ipc_pci_map_dump(ipc_dev, buf);
		for (i = 0; i < IPC_MAX_CHANNEL_COUNT; i++) {
			if (ipc_dev->asyn_chans[i].ipc_irq.free != GUL_MSI_IRQ_BUSY)
				continue;
			sprintf(&buf[strlen(buf)], " IPC ch(%d) irq %d, msi %d, %s\n",
				i, ipc_dev->asyn_chans[i].ipc_irq.irq_index,
				ipc_dev->asyn_chans[i].ipc_irq.msi_value,
				ipc_dev->asyn_chans[i].ipc_irq.irq_num_txt);
		}
	}

	sprintf(&buf[strlen(buf)], " %s", "\n");

	return strlen(buf);
}

int gul_ipc_probe(struct gul_dev *gul_dev, int virq_count,
		  struct virq_evt_map *virq_map)
{
	int ret = 0, retries  = GUL_IPC_INIT_WAIT_RETRIES;
	uint32_t i;
	struct gul_hif *hif;
	struct gul_ipc_dev *ipc_dev;
	struct device *ipc_class_dev = NULL;

	dev_dbg(gul_dev->dev, "Inside %s function K_hif=%lx\n", __func__,
			sizeof(struct gul_hif));

	for (i = 0; i < MAX_MODEM; i++) {
		if (in_use_minor[i] == 0) {
			ipc_minor_index = i;
			break;
		}
	}

	if (i == MAX_MODEM) {
		printk(KERN_ERR "No minor no. free to create ipc dev\n");
		return -ENODEV;
	}

	ipc_dev = kmalloc(sizeof(struct gul_ipc_dev), GFP_KERNEL);
	if (!ipc_dev) {
		printk(KERN_CRIT "Memory allocation failure for ipc_dev\n");
		return -ENOMEM;
	}

	ipc_dev->gul_ipc_devnr = MKDEV(gul_ipc_major, ipc_minor_index);
	sprintf(ipc_dev->name, "%s%s", GUL_IPC_DEVNAME_PREFIX, gul_dev->name);
	ipc_class_dev = device_create(gul_dev->class, NULL,
					ipc_dev->gul_ipc_devnr,
					NULL, ipc_dev->name);
	if (IS_ERR(ipc_class_dev))
		goto fail;

	ipc_dev->gul_dev = gul_dev;

	/* Wait for Modem to ready IPC metadata */
	hif = gul_dev->hif;
	while (!CHK_HIF_MOD_RDY(hif, HIF_MOD_READY_IPC_LIB) && retries) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(
			GUL_IPC_INIT_WAIT_TIMEOUT));
		retries--;
		dma_rmb();
	}

	if (!retries && !CHK_HIF_MOD_RDY(hif, HIF_MOD_READY_IPC_LIB)) {
		dev_err(gul_dev->dev, "IPC modem ready not set, Aborting!\n");
		ret = -EBUSY;
		goto fail;
	}
	dev_info(gul_dev->dev, "IPC modem is ready!\n");

	ret = gul_ipc_create_cdev(ipc_dev);
	if (ret)
		goto fail;

	gul_dev->ipc_priv = ipc_dev;

	in_use_minor[ipc_minor_index] = 1;
	ipc_dev->minor = ipc_minor_index;

	/* Register Stats ops to SYSFS interface */
	ret = gul_ipc_stats_init(gul_dev);
	if (ret < 0) {
		dev_err(gul_dev->dev, "IPC Stats registration failed!\n");
		goto fail;
	}
	gul_dev->stats_desc.stats_control |= (1 << HOST_CONTROL_IPC_STATS);
	gul_setup_pci_space(ipc_dev);

	dev_dbg(gul_dev->dev, "Exiting function %s\n", __func__);
	return ret;

fail:
	if (ipc_dev) {
		if (ipc_class_dev) {
			if (IS_ERR(ipc_class_dev))
				ret = PTR_ERR(ipc_class_dev);
			else
				device_destroy(gul_dev->class,
					       ipc_dev->gul_ipc_devnr);
		}

		kfree(ipc_dev);
	}

	return ret;
}

int gul_ipc_remove(struct gul_dev *gul_dev)
{
	int i;
	struct gul_ipc_dev *ipc_dev;

	ipc_dev = gul_dev->ipc_priv;
	if (ipc_dev) {
		for (i = 0; i < IPC_MAX_CHANNEL_COUNT; i++)
			deregister_ipc_channel_irq(gul_dev, i);

		cdev_del(&ipc_dev->cdev);
		device_destroy(gul_dev->class, ipc_dev->gul_ipc_devnr);
		in_use_minor[ipc_dev->minor] = 0;
		kfree(ipc_dev);
		gul_dev->ipc_priv = NULL;
	}

	return 0;
}

int gul_ipc_init(void)
{
	int ret;

	gul_ipc_devnr = 0;
	ipc_minor_index = GUL_MINOR_START;
	gul_ipc_minor = GUL_MINOR_START;
	gul_nr_dev = MAX_MODEM;

	ret = alloc_chrdev_region(&gul_ipc_devnr, gul_ipc_minor, gul_nr_dev,
				  GUL_IPC_DEVNAME_PREFIX);
	if (ret < 0) {
		printk(KERN_CRIT "gul_ipc: Failed in getting major number\n");
		return ret;
	}

	gul_ipc_major = MAJOR(gul_ipc_devnr);

	printk(KERN_INFO "LA12xx IPC driver: major_nr %d, minor %d\n",
				gul_ipc_major, gul_ipc_minor);

	return ret;
}
EXPORT_SYMBOL_GPL(gul_ipc_init);

int gul_ipc_exit(void)
{
	unregister_chrdev_region(gul_ipc_devnr, gul_nr_dev);
	return 0;
}
EXPORT_SYMBOL_GPL(gul_ipc_exit);
