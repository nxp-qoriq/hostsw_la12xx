/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2020-2026 NXP
 */

#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/pid.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/version.h>

#include "gul_base.h"
#include "gul_vspa.h"
#include "gul_pci.h"
#include "gul_base.h"

/* Additional options for checking if Mailbox write to VSP completed */
#ifdef VSPA_DEBUG
#define MB_CHECK_ON_WRITE	/* Check when queuing a new mailbox write */
#define MB_CHECK_IN_IRQ	/* Check during Mailbox IRQ processing    */
#define MB_CHECK_TIMER	/* Use timer to keep checking after MB OUT*/
#endif

#define VSPA_HALT_TIMEOUT	(100000)
#define VSPA_STARTUP_TIMEOUT	(100000)
#define VSPA_OVERLAY_TIMEOUT	(100)

#define CONTROL_REG_MASK	(~0x000100FF)
#define CONTROL_PDN_EN		(1<<31)
#define CONTROL_HOST_MSG_GO	(1<<20 | 1<<21 | 1<<22 | 1<<23)
#define CONTROL_VCPU_RESET	(1<<16)
#define CONTROL_DEBUG_MSG_GO	(1<<5)
#define CONTROL_IPPU_GO		(1<<1)
#define CONTROL_HOST_GO		(1<<0)

#define DMA_COMP_STAT_SET	0x01
#define VSPA_DMA_MAX_COUNTER	100
#define VSPA_DMA_TIMEOUT        10

static const struct _vspa_loadable_section {
	uint8_t sect_name_len;
	char * section_name;
} vspa_loadable_section[] = {
	{6, ".sync_"},
	{6, ".slot_"},
	{6, ".pebm_"},
	{6, ".hram_"},
/* Customized loadable section can be added here 
 e.g.  {<sect_name_len>, "<section_name>"}	
 */
	{5, "pebm_"}
};

static ssize_t vspa_show_stats(void *vspa_stats, char *buf,
			       void *dev)
{
	int len = 0;
	struct gul_dev *gul_dev = (struct gul_dev *)dev;
	struct vspa_stats_info *vspa_host_stats;

	if ((gul_dev->stats_desc.stats_control &
		(1 << HOST_CONTROL_VSPA_STATS)) == 0)

		return 0;

	vspa_host_stats = (struct vspa_stats_info *) vspa_stats;

	return len;
}

static void vspa_reset_stats(void *vspa_stats)
{
	struct vspa_stats_info *vspa_host_stats_reset;

	vspa_host_stats_reset = (struct vspa_stats_info *) vspa_stats;
	vspa_host_stats_reset->vspa_loading_count = 0;

}

static int gul_vspa_stats_init(struct gul_dev *gul_dev, int vspa_num)
{
	struct gul_stats_ops vspa_stats_ops;
	struct vspa_device **max_vspadev = (struct vspa_device **)
						gul_dev->vspa_priv;
	struct vspa_device *vspadev = (struct vspa_device *)
						max_vspadev[vspa_num];

	vspa_stats_ops.gul_show_stats = vspa_show_stats;
	vspa_stats_ops.gul_reset_stats = vspa_reset_stats;
	vspa_stats_ops.stats_args = (void *) &vspadev->vspa_stats;

	return gul_host_add_stats(gul_dev, &vspa_stats_ops);
}

/* Function to fetch the vspa state for sysfs */
int full_state(struct vspa_device *vspadev)
{
	int state = vspadev->state;

	if (state == VSPA_STATE_UNPROGRAMMED_IDLE ||
		state == VSPA_STATE_RUNNING_IDLE) {
		if (vspa_reg_read(vspadev->regs + STATUS_REG_OFFSET) & 0x100)
			state++;
	}
	return state;
}

/*This function will program the DMA in polling mode */
static int dma_raw_transmit(struct vspa_device *vspadev,
				struct vspa_dma_req *dr)
{
	int stat_abort;
	uint32_t counter = 0;
	u32 __iomem *regs = vspadev->regs;

	volatile int dma_comp_stat, xfr_err, cfg_err;

	/* Program the DMA transfer */
	vspa_reg_write(regs + DMA_DMEM_ADDR_REG_OFFSET, dr->dmem_addr);
	vspa_reg_write(regs + DMA_AXI_ADDR_REG_OFFSET,
						(dr->axi_addr));
	vspa_reg_write(regs + DMA_BYTE_CNT_REG_OFFSET,  dr->byte_cnt);

	dma_comp_stat = vspa_reg_read(regs + DMA_COMP_STAT_REG_OFFSET);
	dev_dbg(vspadev->dev, "dma_comp_stat = %d\n", dma_comp_stat);
	dev_dbg(vspadev->dev, "dmem_addr=%x \t axi_addr=%x \t byte_cnt=%x \t \
		xfer_ctrl=%x\n",
		dr->dmem_addr, (uint32_t)dr->axi_addr,
		dr->byte_cnt, dr->xfr_ctrl);

	vspa_reg_write(regs + DMA_XFR_CTRL_REG_OFFSET,  dr->xfr_ctrl);

	counter = VSPA_DMA_MAX_COUNTER;

	while (counter) {
		dma_comp_stat = vspa_reg_read(regs + DMA_COMP_STAT_REG_OFFSET);
		if((dma_comp_stat & DMA_COMP_STAT_SET) == DMA_COMP_STAT_SET)
			break;
		else
			udelay(VSPA_DMA_TIMEOUT);
		counter--;
	}

	if (!counter) {
		dev_err(vspadev->dev, "Timeout Error in Raw transmit\n");
		goto error;
	}

	dma_comp_stat = vspa_reg_read(regs + DMA_COMP_STAT_REG_OFFSET);
	stat_abort = vspa_reg_read(regs + DMA_STAT_ABORT_REG_OFFSET);
	dev_dbg(vspadev->dev, "dma_comp_stat = %x stat_abort %x\n",
			dma_comp_stat, stat_abort);

	vspa_reg_write(regs + DMA_COMP_STAT_REG_OFFSET, DMA_COMP_STAT_SET);

	xfr_err = vspa_reg_read(regs + DMA_XFRERR_STAT_REG_OFFSET);
	cfg_err = vspa_reg_read(regs + DMA_CFGERR_STAT_REG_OFFSET);
	dev_dbg(vspadev->dev, "DMA_XFRERR value = %x CFG error = %x\n",
							xfr_err, cfg_err);
	return 0;
error:
	return -ETIMEDOUT;
}

static int powerdown(struct vspa_device *vspadev)
{
	u32 __iomem *regs = vspadev->regs;
	u32 __iomem *dbg_regs = vspadev->dbg_regs;
	int ret = 0, ret1 = 0;
	uint32_t val;
	int ctr;

	/* Disable all interrupts */
	vspa_reg_write(regs + IRQEN_REG_OFFSET, 0x0);

	vspadev->versions.vspa_sw_version = ~0;
	vspadev->versions.ippu_sw_version = ~0;
	vspadev->eld_filename[0] = '\0';
	vspadev->watchdog_interval_msecs = VSPA_WATCHDOG_INTERVAL_DEFAULT;

	if (vspadev->state == VSPA_STATE_RUNNING_IDLE) {
		/* clear debug_msg_go, host_msg_go, ru_go and host_go bits */
		val = vspa_reg_read(regs + CONTROL_REG_OFFSET);
		val = (val & CONTROL_REG_MASK) & ~CONTROL_HOST_GO;
		vspa_reg_write(regs + CONTROL_REG_OFFSET, val);

		/* Enable the invasive (halting) debug mode */
		vspa_reg_write(dbg_regs + DBG_GDBEN_REG_OFFSET, 0x1);

		/* Enable dbg_dbgen */
		val = vspa_reg_read(dbg_regs + DBG_DVR_REG_OFFSET);
		val = val | (1 << 13);
		vspa_reg_write(dbg_regs + DBG_DVR_REG_OFFSET, val);

		/* change access perpective to VCPU */
		val = vspa_reg_read(dbg_regs + DBG_RAVAP_REG_OFFSET);
		val = val | (1 << 31);
		vspa_reg_write(dbg_regs + DBG_RAVAP_REG_OFFSET, val);

		val = vspa_reg_read(regs + CONTROL_REG_OFFSET);
		val = (val & CONTROL_REG_MASK) &
			~(CONTROL_IPPU_GO | CONTROL_HOST_MSG_GO
					| CONTROL_DEBUG_MSG_GO);

		/* change access perpective back to Host */
		val = vspa_reg_read(dbg_regs + DBG_RAVAP_REG_OFFSET);
		val = val & ~(1 << 31);
		vspa_reg_write(dbg_regs + DBG_RAVAP_REG_OFFSET, val);

		/* Stop all VSPA activities using “force_halt” */
		vspa_reg_write(dbg_regs + DBG_RCR_REG_OFFSET, 0x4);

		/* Wait for the “halted” bit to be set */
		for (ctr = VSPA_HALT_TIMEOUT; ctr; ctr--) {
			udelay(1);
			ret1 = vspa_reg_read(dbg_regs + DBG_RCSTATUS_REG_OFFSET)
				& (1 << 13);
			if (ret1)
				break;
		}
		if (!(ret1)) {
			dev_err(vspadev->dev, "%d:%s timeout waiting for halt\n",
					vspadev->id, __func__);
			ret = -ETIME;
			vspadev->state = VSPA_STATE_UNKNOWN;
			return ret;
		}

		/* Issue the reset */
		val = vspa_reg_read(regs + CONTROL_REG_OFFSET);
		val = (val & CONTROL_REG_MASK) | CONTROL_VCPU_RESET;
		vspa_reg_write(regs + CONTROL_REG_OFFSET, val);

		vspa_reg_write(regs + VCPU_GO_ADDR_REG_OFFSET, 0x0);

		/* Start VSPA activities using “resume” */
		vspa_reg_write(dbg_regs + DBG_RCR_REG_OFFSET, 0x2);
	}

	vspadev->state = ret ? VSPA_STATE_UNKNOWN : VSPA_STATE_POWER_DOWN;
	return ret;
}

static int powerup(struct vspa_device *vspadev)
{
	int ret = 0;
	u32 __iomem *regs = vspadev->regs;

	if (vspadev->state != VSPA_STATE_POWER_DOWN)
		return -EPERM;

	/* Disable all interrupts */
	vspa_reg_write(regs + IRQEN_REG_OFFSET, 0x0);

	vspadev->state = ret ? VSPA_STATE_UNKNOWN :
					VSPA_STATE_UNPROGRAMMED_IDLE;
	return ret;
}

/* To start the vspa core after dma is programmed */
static int startup(struct gul_dev *gul_dev, int vspa_num)
{
	struct vspa_device **max_vspadev = (struct vspa_device **)
						gul_dev->vspa_priv;
	struct vspa_device *vspadev = (struct vspa_device *)
					max_vspadev[vspa_num];
	u32 __iomem *regs = vspadev->regs;
	uint32_t val, msb, lsb;
	uint32_t dma_channels;
	uint32_t vspa_sw_version, ippu_sw_version;
	int ctr;

	/* Ask the VSPA to go */
	val = vspa_reg_read(regs + CONTROL_REG_OFFSET);
	val = (val & CONTROL_REG_MASK) | CONTROL_HOST_GO;
	vspa_reg_write(regs + CONTROL_REG_OFFSET, val);
	vspadev->vspa_stats.vspa_loading_count++;
	vspa_sw_version = vspa_reg_read(regs + SWVERSION_REG_OFFSET);
	ippu_sw_version = vspa_reg_read(regs + IPPU_SWVERSION_REG_OFFSET);

	/* Wait for the 64 bit mailbox bit to be set */

	for (ctr = VSPA_STARTUP_TIMEOUT; ctr; ctr--) {
		if (vspa_reg_read(regs + HOST_MBOX_STATUS_REG_OFFSET) &
							MBOX_STATUS_IN_64_BIT)
			break;
		schedule_timeout(1000);
	}
	if (!ctr) {
		ERR("%d: timeout waiting for Boot Complete msg\n", vspadev->id);
		goto startup_fail;
	}
	msb = vspa_reg_read(regs + HOST_IN_64_MSB_REG_OFFSET);
	lsb = vspa_reg_read(regs + HOST_IN_64_LSB_REG_OFFSET);
	if (vspadev->debug & DEBUG_STARTUP)
		dev_info(vspadev->dev, "Boot Ok Msg: msb = %08X, lsb = %08X\n",
								msb, lsb);

	/* Check Boot Complete message */
	if (msb != 0xF1000000) {
		dev_err(vspadev->dev, "%d: Boot Complete msg did not match\n",
								vspadev->id);
		goto startup_fail;
	} else {
		dev_info(vspadev->dev, "VSPA %d Boot Ok\n", vspa_num);
		dev_dbg(vspadev->dev, "Msg Verified: msb = %08X, lsb = %08X\n",
			msb, lsb);
	}

	dma_channels = lsb;

	vspa_sw_version = vspa_reg_read(regs + SWVERSION_REG_OFFSET);
	ippu_sw_version = vspa_reg_read(regs + IPPU_SWVERSION_REG_OFFSET);

	/* Set SPM buffer */
	msb = (0x70 << 24) | vspadev->spm_buf_bytes;
	lsb = (uint32_t)(dma_addr_t)vspadev->spm_buf_paddr;
	vspa_reg_write(regs + HOST_OUT_64_MSB_REG_OFFSET, msb);
	vspa_reg_write(regs + HOST_OUT_64_LSB_REG_OFFSET, lsb);
	vspa_sw_version = vspa_reg_read(regs + SWVERSION_REG_OFFSET);
	ippu_sw_version = vspa_reg_read(regs + IPPU_SWVERSION_REG_OFFSET);
	dev_dbg(vspadev->dev, "SW Version: vspa = %08X, ippu = %08X\n",
				vspa_sw_version, ippu_sw_version);
	/* Wait for the 64 bit mailbox bit to be set */
	for (ctr = VSPA_STARTUP_TIMEOUT; ctr; ctr--) {
		if (vspa_reg_read(regs + HOST_MBOX_STATUS_REG_OFFSET) &
							MBOX_STATUS_IN_64_BIT)
			break;
		schedule_timeout(100);
	}
	if (!ctr) {
		ERR("%d: timeout waiting for SPM Ack msg\n", vspadev->id);
		goto startup_fail;
	}
	msb = vspa_reg_read(regs + HOST_IN_64_MSB_REG_OFFSET);
	lsb = vspa_reg_read(regs + HOST_IN_64_LSB_REG_OFFSET);
	dev_dbg(vspadev->dev, "SPM Ack Msg: msb = %08X, lsb = %08X\n",
				msb, lsb);
	if (vspadev->debug & DEBUG_STARTUP)
		dev_dbg(vspadev->dev,
			"SPM Ack Msg: msb = %08X, lsb = %08X\n",
			msb, lsb);
	if (msb != 0xF0700000) {
		ERR("%d: SPM Ack error %08X\n", vspadev->id, msb);
		goto startup_fail;
	}

	/*This piece of code is for passing the PCI base address to VSPA via
	 *mailbox 0. Error Print will come since VSPA FW image is not having
	 *response support for base address implemented
	 */

	val = vspa_reg_read(regs + CONTROL_REG_OFFSET);

	if (dma_channels) {
		vspadev->spm_dma_chan   = (dma_channels >> 24)&0xFF;
		vspadev->bulk_dma_chan  = (dma_channels >> 16)&0xFF;
		vspadev->reply_dma_chan = (dma_channels >>  8)&0xFF;
		vspadev->cmd_dma_chan   = (dma_channels)&0xFF;
	} else { /* legacy images */
		vspadev->spm_dma_chan   = VSPA_DMA_CHANNELS;
		vspadev->bulk_dma_chan  = 0xFF;
		vspadev->reply_dma_chan = 0xFF;
		vspadev->cmd_dma_chan   = vspadev->legacy_cmd_dma_chan;
	}

	if (vspadev->debug & DEBUG_STARTUP) {
		dev_info(vspadev->dev, "SW Version: vspa = %08X, ippu = %08X\n",
				vspa_sw_version, ippu_sw_version);
		dev_info(vspadev->dev,
			"DMA chan: spm %02X, bulk %02X, reply %02X, cmd %02X\n",
			vspadev->spm_dma_chan, vspadev->bulk_dma_chan,
			vspadev->reply_dma_chan, vspadev->cmd_dma_chan);
	}

	vspadev->versions.vspa_sw_version = vspa_sw_version;
	vspadev->versions.ippu_sw_version = ippu_sw_version;
	vspadev->state = VSPA_STATE_RUNNING_IDLE;
	init_completion(&vspadev->watchdog_complete);
	return 0;

startup_fail:
	vspadev->versions.vspa_sw_version = ~0;
	vspadev->versions.ippu_sw_version = ~0;
	vspadev->state = VSPA_STATE_STARTUP_ERR;
	vspa_reg_write(vspadev->regs + IRQEN_REG_OFFSET, 0);
	return -EIO;
}

/**
 * FIXME : This function is not required. Right now VMA is used to
 * load firmware not LMA.
 * And sec_cnt should be index into program header section not section header.
 */
#ifdef GET_LMA
static int get_lma(uint8_t *file_start, uint32_t pg_hd_off,
		uint32_t ph_num, uint32_t vma, uint32_t *lma,
		int sec_cnt)
{
	struct program_header *ph =
			(struct program_header *)(file_start + pg_hd_off);

	if (ph[sec_cnt-1].prg_vaddr == vma) {
		*lma = ph[sec_cnt-1].prg_paddr;
		/* reset 2nd nibble as requested by VSPA f/w team */
		*lma = (*lma & 0xF0FFFFFF);
		return LIBVSPA_ERR_OK;
	}

	return -LIBVSPA_ERR_INVALID_FILE;
}
#endif

static char *get_section_name(uint8_t *string_table, int sec_name_offset)
{
	if (!string_table)
		return NULL;

	return (char *)(string_table + sec_name_offset);
}


static uint8_t is_vspa_section_loadable(char *sec_name)
{
	int8_t  i = 0, 
		ret = 0,
		sect_num = sizeof(vspa_loadable_section) / sizeof(vspa_loadable_section[0]);

	for (i = 0; i < sect_num; i++)
	{
		if (!strncmp(sec_name, vspa_loadable_section[i].section_name, vspa_loadable_section[i].sect_name_len)) {
			ret = 1;
			break;
		}
	}
	return ret;
}

static int8_t is_vspa_section_valid(struct vspa_device *vspadev, int sec_num)
{
	int8_t i = 0, memspace_id = -1;

	for (i = 0; i < vspadev->vspa_sec_info.vspa_sec_count; i++) {
		if (vspadev->vspa_sec_info.section_memspace_map[i].section_id == sec_num) {
			memspace_id = vspadev->vspa_sec_info.section_memspace_map[i].memspace_id;
			break;
		}
	}
	return memspace_id;
}

static int32_t get_vspa_section_trans_mode(struct vspa_device *vspadev, int sec_num, int8_t memspace_id)
{
	int8_t i = 0;
	char *memspace_name = NULL;
	int32_t trans_mode = -1;

	if (memspace_id < 0)
		return -1;

	for (i = 0; i < vspadev->vspa_sec_info.vspa_memspace_count; i++) {
		if (vspadev->vspa_sec_info.memspace_map[i].memspace_id == memspace_id) {
			memspace_name = vspadev->vspa_sec_info.memspace_map[i].memspace_name;
			break;
		}
	}

	if (memspace_name == NULL)
		return NO_LOAD;
	else if (!strcmp(memspace_name, "axim"))
		trans_mode = AXI_NC;
	else if (!strcmp(memspace_name, "dram"))
		trans_mode = DMEM_NC;
	else if (!strcmp(memspace_name, "ippu") || !strcmp(memspace_name, "ippu_pram"))
		trans_mode = IPPUPRAM;
	else if (!strcmp(memspace_name, "vcpu") || !strcmp(memspace_name, "vcpu_pram"))
		trans_mode = PRAM;
	else
		trans_mode = NO_LOAD;

	return trans_mode;
}

static int vspa_fw_dma_write(struct gul_dev *gul_dev, struct dma_param *linfo,
		uint32_t flags, int vspa_num)
{
	uint32_t size_to_xfer = linfo->size;
	static uint8_t id;
	int rc = 0;
	struct vspa_dma_req  dma_req;
	int8_t err = -LIBVSPA_ERR_EAGAIN;
	struct gul_mem_region_info *vspa_dma_region;
	struct vspa_device **max_vspadev = (struct vspa_device **)
						gul_dev->vspa_priv;
	struct vspa_device *vspadev = (struct vspa_device *)
						max_vspadev[vspa_num];

	dev_dbg(gul_dev->dev,
			"INFO: %s :DMA_write: mode = %x from = %0llx \
			to = %06x total %06x bytes\n", __func__,
			linfo->xfr_ctrl, linfo->phys, linfo->offset,
			linfo->size);

	do {
		size_to_xfer = linfo->size > MAX_DMA_TRANSFER ?
				MAX_DMA_TRANSFER : linfo->size;

		dev_dbg(gul_dev->dev,
			"INFO: %s :DMA_write: size_to_xfer = %x\n", __func__, size_to_xfer);

		dma_req.type      = 0;
		dma_req.id        = id++;
		dma_req.flags     = VSPA_FLAG_REPORT_DMA_COMPLETE;
		dma_req.axi_addr  = linfo->phys;
		dma_req.dmem_addr = linfo->offset & PRAM_ADDR_MASK;
		dma_req.byte_cnt  = size_to_xfer & DMA_BCNT_MASK;
		dma_req.xfr_ctrl  = linfo->xfr_ctrl;
		dev_dbg(gul_dev->dev, "DMA: %08X %08X %0llX %08X %08X\n",
							dma_req.control,
		dma_req.dmem_addr, dma_req.axi_addr, dma_req.byte_cnt,
							dma_req.xfr_ctrl);
		linfo->phys   += size_to_xfer; /* Increment the src ptr */
		linfo->size   -= size_to_xfer; /* Decrement the byte count */
		linfo->offset += size_to_xfer; /* Inc the dst ptr */

		if (dma_req.dmem_addr >= VSPA_DRAM_SIZE) {
			dev_err(gul_dev->dev,
				"ERR %s: VSPA DMEM Address (0x%x) is out of range\n",
				__func__, dma_req.dmem_addr);
			return -EINVAL;
		}

		/*Fetching DMA region address*/
		vspa_dma_region = &vspadev->vspa_dma_region;
		if (!vspa_dma_region) {
			dev_err(gul_dev->dev, "ERR %s: DMA region not found",
								__func__);
			return -ENOMEM;
		}

		memcpy(vspa_dma_region->vaddr, (const void *)dma_req.axi_addr,
							dma_req.byte_cnt);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0)
		dma_map_page_attrs(&vspadev->pdev->dev,
				virt_to_page(vspa_dma_region->vaddr),
				offset_in_page(vspa_dma_region->vaddr), dma_req.byte_cnt,
				(enum dma_data_direction)DMA_TO_DEVICE, 0);
#else
		pci_map_single(vspadev->pdev, vspa_dma_region->vaddr,
				dma_req.byte_cnt, PCI_DMA_TODEVICE);
#endif

		dma_wmb();
		dma_req.axi_addr = vspa_dma_region->phys_addr;
		dev_dbg(gul_dev->dev, "vspa%d: ctrl %08x, dmem %08x, \
				axi %08llx, cnt %08x, ctrl %08x\n",
				vspadev->id, dma_req.control, dma_req.dmem_addr,
				dma_req.axi_addr, dma_req.byte_cnt,
				dma_req.xfr_ctrl);
		rc = dma_raw_transmit(vspadev, &dma_req);
		if (rc < 0) {
			dev_err(vspadev->dev, "ERR: Timeout in Raw transmit\n");
			return -ECOMM;
		}
		err = 0;
	} while (linfo->size != 0 && err == 0);

	dev_dbg(gul_dev->dev, "DMA_write: operation complete\n");

	return 0;

}

static void fw_dump_sections_info(struct gul_dev *gul_dev, int vspa_num)
{
	int i = 0;
	struct vspa_device **max_vspadev = (struct vspa_device **)
						gul_dev->vspa_priv;
	struct vspa_device *vspadev = (struct vspa_device *)
					max_vspadev[vspa_num];
	mw_info_type_14 *memspace_map = NULL;
	mw_info_type_15 *section_memspace_map = NULL;

	dev_dbg(gul_dev->dev, "vspa_memspace_count %d\n", vspadev->vspa_sec_info.vspa_memspace_count);
	for (i = 0; i < vspadev->vspa_sec_info.vspa_memspace_count; i++) {
		memspace_map = &vspadev->vspa_sec_info.memspace_map[i];
		dev_dbg(gul_dev->dev, "memspace_id %u, flags %u, start_address %x, size %x, memspace_namesz %u, memspace_name %s\n",
				memspace_map->memspace_id, memspace_map->flags, memspace_map->start_address, memspace_map->size,
				memspace_map->memspace_namesz, memspace_map->memspace_name);
	}

	dev_dbg(gul_dev->dev, "vspa_sec_count %d\n", vspadev->vspa_sec_info.vspa_sec_count);
	for (i = 0; i < vspadev->vspa_sec_info.vspa_sec_count; i++) {
		section_memspace_map = &vspadev->vspa_sec_info.section_memspace_map[i];
		dev_dbg(gul_dev->dev, "section_id %u, memspace_id %u\n", section_memspace_map->section_id, section_memspace_map->memspace_id);
	}
}

#ifdef VSPA_IMG_CHECK
static int check_fw_section_info(struct gul_dev *gul_dev, uint8_t *file_start,
		uint32_t fw_size, struct section_header *sec_header,
		int sec_num, uint8_t *section_end, uint8_t *section_ptr) {
	phys_addr_t section_start_phy, section_end_phy, vspa_firmware_start_loc;

	vspa_firmware_start_loc = virt_to_phys((void *)file_start);
	section_start_phy = virt_to_phys((void *)section_ptr);
	section_end_phy = virt_to_phys((void *)section_end);

	if (((sec_header[sec_num].sec_addr +
		sec_header[sec_num].sec_offset +
		sec_header[sec_num].sec_size) > fw_size) ||
		(!section_ptr) || (!section_end) ||
		(section_start_phy < vspa_firmware_start_loc) ||
		(section_end_phy >= (vspa_firmware_start_loc + fw_size))) {
		return -EADDRNOTAVAIL;
	}

	dev_dbg(gul_dev->dev,"Sec_ptr: 0x%llx, Sec_end: 0x%llx, Sec_addr: 0x%x, Sec_offset 0x%x, Sec_size: 0x%x, vaddr: 0x%llx, fw_size:0x%x\n",
			section_start_phy, section_end_phy,
			sec_header[sec_num].sec_addr,
			sec_header[sec_num].sec_offset,
			sec_header[sec_num].sec_size,
			vspa_firmware_start_loc, fw_size);

	return 0;
}
#endif

static int fw_read_and_load_sections_info(struct gul_dev *gul_dev,
		uint8_t *file_start, uint32_t pg_hd_off,
		uint32_t sec_header_off, int sec_cnt, int str_section_off,
		uint32_t fw_size, uint32_t ph_num,
		int vspa_num)
{
	struct vspa_device **max_vspadev = (struct vspa_device **)
						gul_dev->vspa_priv;
	struct vspa_device *vspadev = (struct vspa_device *)
					max_vspadev[vspa_num];
	uint32_t  record_type = 0, record_size = 0;
	struct section_header *sec_header = NULL;
	uint8_t *section_ptr  = NULL, *section_end = NULL;
	uint8_t  *str          = NULL;
	int sec_num = 0, found = 0;

	if (file_start == NULL) {
		dev_err(gul_dev->dev, "Load sections: file start is NULL");
		return -EEXIST;
	}

	/*Fetching Section header from firmware file */
	sec_header = (struct section_header *)(file_start + sec_header_off);

	str = (file_start + sec_header[str_section_off].sec_offset);
	for (sec_num = 0; sec_num < sec_cnt; sec_num++) {
		section_ptr = (uint8_t *) (file_start +  sec_header[sec_num].sec_offset);
		section_end = (uint8_t *) (section_ptr + sec_header[sec_num].sec_size);
#ifdef VSPA_IMG_CHECK
		if (check_fw_section_info(gul_dev, file_start, fw_size,
				sec_header, sec_num, section_end,
				section_ptr)) {
			dev_err(gul_dev->dev, "ERR: Incorrect Section address, offset or size\n");
			return -EADDRNOTAVAIL;
		}
#endif

		if (!strncmp((char *)(str + sec_header[sec_num].sec_name),
					".mw_info", strlen(".mw_info"))) {
			found = 1;
			break;
		}
	}

	if (found) {
		/* allocate section_memspace_map as per number of sections in VSPA binary */
		vspadev->vspa_sec_info.section_memspace_map = kcalloc(sec_cnt, sizeof(mw_info_type_15), GFP_KERNEL);
		if (!vspadev->vspa_sec_info.section_memspace_map) {
			dev_err(gul_dev->dev, "ERR %s[%d]: Memory allocation failed\n",
					__func__, __LINE__);
			return LIBVSPA_ERR_ENOMEM;
		}

		while (section_ptr < section_end) {
			record_size = *(uint8_t *) (section_ptr);
			record_type = *(uint8_t *) (section_ptr + 4);

			if (!record_type)
				break;

			switch (record_type) {
			case MW_INFO_TYPE_14:
				WARN_ON(vspadev->vspa_sec_info.vspa_memspace_count >= MAX_VSPA_MEMSPACE);
				memcpy(&vspadev->vspa_sec_info.memspace_map[vspadev->vspa_sec_info.vspa_memspace_count], section_ptr + 5, record_size - 5);
				vspadev->vspa_sec_info.vspa_memspace_count++;
				break;
			case MW_INFO_TYPE_15:
				memcpy(&vspadev->vspa_sec_info.section_memspace_map[vspadev->vspa_sec_info.vspa_sec_count], section_ptr + 5, record_size - 5);
				vspadev->vspa_sec_info.vspa_sec_count++;
				break;
			default:
				break;
			}
			section_ptr = (uint8_t *)(section_ptr + record_size);
		}

		fw_dump_sections_info(gul_dev, vspa_num);
		return LIBVSPA_ERR_OK;
	}
	return LIBVSPA_ERR_EIO;
}

static int fw_read_and_load_sections(struct gul_dev *gul_dev,
		uint8_t *file_start, uint32_t pg_hd_off,
		uint32_t sec_header_off, int sec_cnt, int str_section_off,
		uint32_t fw_size, uint32_t ph_num,
		struct vspa_hardware *hardware, int vspa_num)
{
	int sec_num = 0, ret = LIBVSPA_ERR_OK;
	uint32_t  axi_align    = 0;
	uint32_t *section_ptr  = NULL;
	uint8_t  *str          = NULL, *overlay_ptr = NULL;
	dma_addr_t aligned_addr = 0;
	uint32_t  section_size = 0, section_vma = 0;
	uint32_t  section_inf = 0;
	struct section_header *sec_header = NULL;
	struct dma_param dparam = { 0 };
	uint8_t  section_parsed = 0x0;
	uint32_t axi_data_width;
	char *section_name;
	struct vspa_device **max_vspadev = (struct vspa_device **)
						gul_dev->vspa_priv;
	struct vspa_device *vspadev = (struct vspa_device *)
					max_vspadev[vspa_num];
	struct gul_mem_region_info *overlay_region = vspadev->overlay_region;
	uint32_t overlay_base_addr = (uint32_t)overlay_region->phys_addr;
	int8_t memspace_id = -1, sec_loaded_cnt = 0, sec_failed_cnt = 0;

	if (file_start == NULL) {
		dev_err(gul_dev->dev, "Load sections: file start is NULL");
		ret = -EEXIST;
		goto err_out;
	}

	/*Fetching Section header from firmware file */
	sec_header = (struct section_header *)(file_start + sec_header_off);

	str = (file_start + sec_header[str_section_off].sec_offset);
	for (sec_num = 0; sec_num < sec_cnt; sec_num++) {

		axi_data_width = hardware->axi_data_width;
		axi_align      = axi_data_width / BITS_PER_BYTE;

		section_vma = sec_header[sec_num].sec_addr;
		section_size = sec_header[sec_num].sec_size;
		section_size = align(section_size, axi_align);

		section_name = get_section_name(str, sec_header[sec_num].sec_name);
		if (is_vspa_section_loadable(section_name) || ((memspace_id = is_vspa_section_valid(vspadev, sec_num)) < 0)) {
			dev_dbg(gul_dev->dev, "Section %d: %s ignored", sec_num, section_name);

			section_inf = sec_header[sec_num].sec_info;

			dev_dbg(gul_dev->dev,
				"Load section:VMA 0x%08X size 0x%X FSA 0x%08X",
				 section_vma, section_size, section_inf);

			if (section_inf) {
				section_ptr = (uint32_t *)
						(file_start +
						sec_header[sec_num].sec_offset);
				if ((section_inf >= overlay_base_addr) && ((section_inf +
						section_size) < (overlay_base_addr +
							GUL_VSPA_OVERLAY_SIZE))) {
					overlay_ptr = (overlay_region->vaddr +
						+ (section_inf -
						overlay_base_addr));
					memcpy((void *)overlay_ptr, section_ptr,
						section_size);
				} else if ((section_inf & GUL_PEBM_OVERLAY_BASE)
					 == GUL_PEBM_OVERLAY_BASE) {
					overlay_ptr =
						(gul_dev->mem_regions
						[GUL_MEM_REGION_PEBM].vaddr +
						(section_inf -
						GUL_PEBM_BASE_ADDR));
					memcpy((void *)overlay_ptr, section_ptr,
						section_size);
				} else {
					dev_dbg(gul_dev->dev,
						"Ignore: overlay Addr = 0x%08X\n",
						section_inf);
				}
			} else if ((section_vma & GUL_FECA_BASE_ADDR)
					== GUL_FECA_BASE_ADDR) {
				overlay_ptr = (gul_dev->mem_regions
						[GUL_MEM_REGION_FECA].vaddr +
					(section_vma - GUL_FECA_BASE_ADDR));
				memcpy((void *)overlay_ptr, section_ptr, section_size);
			}
		} else {

			section_parsed |= (1 << sec_num);

			/* skip zero size section */
			if (section_size == 0) {
				dev_dbg(gul_dev->dev, "Load Section %d: %s is size zero, ignored!",
						sec_num, section_name);
				continue;
			}

			dev_dbg(gul_dev->dev, "Load Section %d: %s", sec_num, section_name);

			if (section_size > fw_size) {
				dev_err(gul_dev->dev, "Load section: Section too big 0x%08X",
						section_size);
				ret = -LIBVSPA_ERR_ENOMEM;
				goto err_out;
			}

			if (!isaligned(section_vma, axi_align)) {
				dev_err(gul_dev->dev,
						"Load section: Unaligned VMA 0x%08X for section '%s'",
						section_vma,
						section_name);

				ret = -LIBVSPA_ERR_EINVOFFSET;
				goto err_out;
			}

			aligned_addr = (dma_addr_t) file_start;
			aligned_addr = align(aligned_addr, axi_align);

			section_ptr = (uint32_t *)
				(file_start +
				 sec_header[sec_num].sec_offset);
			dev_dbg(gul_dev->dev, "Section header address : %px\n", section_ptr);

			dparam.phys     = (uint64_t)section_ptr;
			dparam.size     = section_size;
			dparam.offset   = section_vma;
			if ((dparam.xfr_ctrl = get_vspa_section_trans_mode(vspadev, sec_num, memspace_id)) != -1) {
				ret = vspa_fw_dma_write(gul_dev,
						&dparam,
						BLOCK, vspa_num);
				if (ret < 0) {
					dev_err(gul_dev->dev,
							"Load sections:DMA failed (%d)",
							ret);
					sec_failed_cnt++;
					goto err_out;
				}
				sec_loaded_cnt++;
			} else {
				dev_err(gul_dev->dev, "Load sections:Trans mode failed (%d)",
						dparam.xfr_ctrl);
				sec_failed_cnt++;
			}
		}
	}

	dev_dbg(gul_dev->dev,
		"INFO %s:Section Count: %d, Loaded: %d, Fail %d, Ignored: %d\n",
		__func__, sec_cnt, sec_loaded_cnt, sec_failed_cnt,
		sec_cnt - (sec_loaded_cnt + sec_failed_cnt));
	dev_dbg(gul_dev->dev, "Load sections: sections parsed 0x%08X",
							section_parsed);
err_out:
	/* free section_memspace_map */
	kfree(vspadev->vspa_sec_info.section_memspace_map);
	return ret;
}

static int gul_load_vspa_image(struct gul_dev *gul_dev, char *vaddr,
				int vspa_fw_size, int vspa_num)
{
	struct file_header *hd;
	struct vspa_hardware hardware = {0};

	struct vspa_device **max_vspadev = (struct vspa_device **)
					gul_dev->vspa_priv;
	struct vspa_device *vspadev = (struct vspa_device *)
					max_vspadev[vspa_num];

	hardware = vspadev->hardware;

	dev_dbg(gul_dev->dev, "%s: AXI data width %d\n",
				__func__, hardware.axi_data_width);

	/* FW Header Initialization*/
	hd = (struct file_header *) vaddr;

	/* Image Header Format Checking */
	if (hd->fh_machine != GCC_MACH_CODE &&
		hd->fh_machine != ISCAPE_MACH_CODE) {
		dev_err(gul_dev->dev, "Load_elf: bad hdr fh_machine 0x%02X",
							hd->fh_machine);
		return -ENOEXEC;
	}

	/* FW image parsing to load section info (.mw_info) */
	if (fw_read_and_load_sections_info(gul_dev, vaddr, hd->fh_phoff,
		hd->fh_shoff, hd->fh_shnum, hd->fh_shstrndx,
		vspa_fw_size, hd->fh_phnum, vspa_num)) {
		dev_err(gul_dev->dev, "ERR %s: Image Section Info Loading Failed",
						__func__);
		return -EIO;
	}

	/*FW image parsing and load sections*/
	if (fw_read_and_load_sections(gul_dev, vaddr, hd->fh_phoff,
		hd->fh_shoff, hd->fh_shnum, hd->fh_shstrndx,
		vspa_fw_size, hd->fh_phnum, &hardware, vspa_num)) {
		dev_err(gul_dev->dev, "ERR %s: Image Section Loading Failed",
						__func__);
		return -EIO;
	}

	return 0;
}

static int vspa_get_fw_image(struct gul_dev *gul_dev, int vspa_num)
{
	int ret = 0;
	int buf_size, vspa_fw_size;
	uint32_t axi_align = 0, axi_data_width = 0;
	struct vspa_device **max_vspadev = (struct vspa_device **)
					gul_dev->vspa_priv;
	struct gul_mem_region_info *scratch_buf_region =
				&gul_dev->scratch_buf_region[GUL_VSPA_FW];
	struct vspa_device *vspadev = (struct vspa_device *)
					max_vspadev[vspa_num];

	axi_data_width = vspadev->hardware.axi_data_width;
	axi_align = axi_data_width / BITS_PER_BYTE;

	buf_size = GUL_VSPA_SCRATCH_BUF_MAX_SIZE;

	scratch_buf_region->vaddr = PTR_ALIGN(scratch_buf_region->vaddr,
			  axi_data_width);

	ret = gul_udev_load_firmware(gul_dev, scratch_buf_region->vaddr,
			buf_size, vspadev->eld_filename, &vspa_fw_size);
	if (ret < 0) {
		dev_err(gul_dev->dev, "%s: udev firmware request failed\n",
							__func__);
		goto OUT;
	}

	dev_dbg(gul_dev->dev, "Udev FW [%s]: Address:%px\tVSPA FW Size:%d\n",
		vspa_fw_name_prefix, scratch_buf_region->vaddr, vspa_fw_size);

	dev_dbg(gul_dev->dev, "Target DDR Virtual address = %px and size = %d\n",
		scratch_buf_region->vaddr, vspa_fw_size);

	ret = gul_load_vspa_image(gul_dev, scratch_buf_region->vaddr,
				  vspa_fw_size, vspa_num);
	if (ret < 0) {
		dev_err(gul_dev->dev, "ERR %s: Image loading failed\n",
					__func__);
		goto OUT;
	}
	dev_dbg(gul_dev->dev, "DBG %s: \tOUT\n", __func__);
	return 0;

OUT:
	dev_dbg(gul_dev->dev, "Error in Loading VSPA FW image\n");
	return -EFAULT;
}

/************************* Probe / Remove ***********************************/

static int __vspa_probe(struct gul_dev *gul_dev, int vspa_num)
{
	struct vspa_device **max_vspadev = (struct vspa_device **)
						gul_dev->vspa_priv;
	struct vspa_device *vspadev = NULL;
	struct device *dev = gul_dev->dev;
	struct vspa_hardware *hw;
	int err = 0;
	u32 vspa_instance_offset;
	u32 vspa_dbg_instance_offset;
	uint32_t param0, param1, param2;
	uint32_t val;
	char name[50];
	char eld_filename[VSPA_MAX_ELD_FILENAME] = "";
	int rc = 1;

	/* Getting the VSPA CCSR addresses from PCI */
	vspadev = max_vspadev[vspa_num];
	vspa_instance_offset = VSPA_CCSR_OFFSET +
		(VSPA_INSTANCE_OFFSET * vspa_num);
	vspadev->regs = (u32 __iomem *)
		(gul_dev->mem_regions[GUL_MEM_REGION_CCSR]
		 .vaddr + vspa_instance_offset);
	val = vspa_reg_read(vspadev->regs);
	if (!vspadev->regs) {
		dev_err(dev, "ERR:VSPA-%d:Illegal address mapping\n", vspa_num);
		err = -EINVAL;
		goto err_out;
	}

	vspa_dbg_instance_offset = VSPA_DCSR_OFFSET +
		(VSPA_DBG_INSTANCE_OFFSET * vspa_num);
	vspadev->dbg_regs = (u32 __iomem *)
		(gul_dev->mem_regions[GUL_MEM_REGION_DCSR]
		 .vaddr + vspa_dbg_instance_offset);
	val = vspa_reg_read(vspadev->dbg_regs);
	if (!vspadev->regs) {
		dev_err(dev, "ERR:VSPA-%d:Illegal dbg_regs addr mapping\n",
			vspa_num);
		err = -EINVAL;
		goto err_out;
	}

	vspadev->dev = gul_dev->dev;
	vspadev->pdev = gul_dev->pdev;

	sprintf(name, "%s%s%d", gul_dev->name, VSPA_DEVICE_NAME, vspa_num);

	vspadev->state = VSPA_STATE_UNKNOWN;
	vspadev->debug = DEBUG_MESSAGES;
	vspadev->mem_size = GUL_VSPA_SIZE;

	/* Vspa hardware initialization */
	hw = &vspadev->hardware;
	param0 = vspa_reg_read(vspadev->regs + PARAM0_REG_OFFSET);
	hw->param0           = param0;
	param1 = vspa_reg_read(vspadev->regs + PARAM1_REG_OFFSET);
	hw->param1           = param1;
	hw->axi_data_width   = 32 << ((param1 >> 28) & 7);
	hw->dma_channels     = (param1 >> 16) & 0xFF;
	hw->gp_out_regs      = (param1 >> 8) & 0xFF;
	hw->gp_in_regs       = param1 & 0xFF;
	param2 = vspa_reg_read(vspadev->regs + PARAM2_REG_OFFSET);
	hw->param2           = param2;
	hw->dmem_bytes       = ((param2 >> 8) & 0x3FF) * 400;
	hw->ippu_bytes       = (param2 >> 31) * 4096;
	hw->arithmetic_units = param2 & 0xFF;

	vspadev->versions.vspa_hw_version =
		vspa_reg_read(vspadev->regs+HWVERSION_REG_OFFSET);
	vspadev->versions.ippu_hw_version =
		vspa_reg_read(vspadev->regs+IPPU_HWVERSION_REG_OFFSET);
	vspadev->versions.vspa_sw_version = ~0;
	vspadev->versions.ippu_sw_version = ~0;
	vspadev->eld_filename[0] = '\0';
	vspadev->watchdog_interval_msecs =
		VSPA_WATCHDOG_INTERVAL_DEFAULT;

	vspadev->poll_mask = VSPA_MSG_ALL;

	/* Enable core power gating */
	val = vspa_reg_read(vspadev->regs + CONTROL_REG_OFFSET);
	val = (val & CONTROL_REG_MASK) | CONTROL_PDN_EN;
	vspa_reg_write(vspadev->regs + CONTROL_REG_OFFSET, val);

	/* Make sure all interrupts are disabled */
	vspa_reg_write(vspadev->regs + IRQEN_REG_OFFSET, 0);
	dev_dbg(gul_dev->dev, "%s: hwver 0x%08x, %d AUs, dmem %d bytes\n",
			name, vspadev->regs[HWVERSION_REG_OFFSET],
			hw->arithmetic_units, hw->dmem_bytes);

	dev_dbg(gul_dev->dev, "INFO:%s : VSPA Loading firmware initiated-\n",
			__func__);

	vspadev->state = VSPA_STATE_LOADING;

#ifdef MULTI_LA12XX_MULTI_IMAGE_SUPPORT
	/*multi-modem case - looking to use different files */
	/*check if modem-id based firmware file present */
	if (warmup_flag[gul_dev->id] == 1)
		snprintf(eld_filename, VSPA_MAX_ELD_FILENAME, "%s%d-%d.eld",
			VSPA_WARMUP_FW_NAME_PREFIX, vspa_num, gul_dev->id);
	else
		snprintf(eld_filename, VSPA_MAX_ELD_FILENAME, "%s%d-%d.eld",
				vspa_fw_name_prefix, vspa_num, gul_dev->id);
	rc = check_file(eld_filename);
#endif

	if (rc) {
		/*Different files not present. So using common image file*/
		if (warmup_flag[gul_dev->id] == 1)
			snprintf(vspadev->eld_filename, VSPA_MAX_ELD_FILENAME,
			"%s%d.eld", VSPA_WARMUP_FW_NAME_PREFIX, vspa_num);
		else
			snprintf(vspadev->eld_filename, VSPA_MAX_ELD_FILENAME,
			"%s%d.eld", vspa_fw_name_prefix, vspa_num);

	} else {
		/*Different files  present. So using specific image file*/
		snprintf(vspadev->eld_filename, VSPA_MAX_ELD_FILENAME,
			"%s", eld_filename);
	}

	/* Call the GUL base APIs to request_firmware*/
	if (vspa_get_fw_image(gul_dev, vspa_num)) {
		dev_err(gul_dev->dev, "ERR %s : Loading VSPA FW failed\n",
				__func__);
		err = -EBADRQC;
		goto err_out;
	}

	dev_dbg(gul_dev->dev,
			"INFO: VSPA NUM %d  fileName %s FW image loading finished\n",
			vspa_num, (char *)vspadev->eld_filename);

	/* Updating VSPA core Index in ulIppuSwVer Register */
	vspa_reg_write(vspadev->regs + IPPU_SWVERSION_REG_OFFSET, vspa_num);
	dev_dbg(gul_dev->dev, "ulIppuSwVer = %08X\n",
		vspa_reg_read(vspadev->regs + IPPU_SWVERSION_REG_OFFSET));

	/* Initiate the VSPA_GO to start VSPA booting */
	if (startup(gul_dev, vspa_num)) {
		dev_err(gul_dev->dev,
				"ERR %s: VSPA failed to start VSPA\n",
				__func__);
		err = -EBADRQC;
		goto err_out;
	}

	err = gul_vspa_stats_init(gul_dev, vspa_num);
	if (err < 0) {
		dev_err(gul_dev->dev, "ERR: VSPA stats error\n");
		err = -EBADRQC;
		goto err_out;
	}
	dev_dbg(gul_dev->dev, "DBG: Fw image name saved: %s",
			vspadev->eld_filename);

	/*Clearing the VCPU_TO_HOST MBOXs */
	vspa_reg_write(vspadev->regs + HOST_FLAGS0_REG_OFFSET,
			0xFFFFFFFFUL);
	vspa_reg_write(vspadev->regs + HOST_FLAGS1_REG_OFFSET,
			0xFFFFFFFFUL);

	/*Clearing the mailbox status bits for AVI*/
	vspa_reg_write(vspadev->regs + STATUS_REG_OFFSET, 0xF000);

	dma_wmb();

	return 0;

err_out:
	return err;
}

int vspa_probe(struct gul_dev *gul_dev, int vspa_irq_count,
		struct virq_evt_map *vspa_virq_map)
{
	struct vspa_device *vspadev = NULL;
	struct vspa_device **max_vspadev;
	struct device *dev = gul_dev->dev;
	struct gul_mem_region_info *vspa_dma_region, *overlay_region;
	int err = 0;
	int vspa_num = 0;

	max_vspadev = (struct vspa_device **)
		kzalloc(sizeof(struct vspa_device *) * MAX_VSPA, GFP_KERNEL);
	if (!max_vspadev) {
		dev_err(dev, "ERR %s[%d]: Memory allocation failed\n",
				__func__, __LINE__);
		goto err_out;
	}

	/* Getting scratch buffer memory for loading the VSPA FW
	 * for all 8 cores
	 */
	vspa_dma_region = scratch_buf_allocator(gul_dev, GUL_VSPA_FW,
				GUL_VSPA_SCRATCH_BUF_MAX_SIZE);
	if (!vspa_dma_region) {
		dev_err(dev, "ERR %s: Address of dma region not found\n",
							__func__);
		err = -EADDRNOTAVAIL;
		goto err_mem_out;
	}

	/* Reserving region from scratch buffer for Overlay
	 * section in HOST DDR
	 */
	overlay_region = scratch_buf_allocator(gul_dev, GUL_VSPA_OVERLAY,
						GUL_VSPA_OVERLAY_SIZE);
	if (!overlay_region) {
		dev_err(dev, "ERR %s: Address of overlay region not found\n",
							__func__);
		err = -EADDRNOTAVAIL;
		goto err_mem_out;
	}

	gul_dev->vspa_priv = max_vspadev;

	for (vspa_num = 0; vspa_num < MAX_VSPA; vspa_num++) {
		dev_dbg(gul_dev->dev, "***Start VSPA %d Boot..\n", vspa_num);
		/* Allocating space vspa device structure */
		max_vspadev[vspa_num] = (struct vspa_device *)
			kzalloc(sizeof(struct vspa_device), GFP_KERNEL);
		if (!max_vspadev[vspa_num]) {
			dev_err(dev, "ERR %s: failed to allocate vspa_device\n",
							__func__);
			err = -ENOMEM;
			goto err_mem_single_out;
		}

		vspadev = max_vspadev[vspa_num];
		vspadev->overlay_region = overlay_region;
		vspadev->vspa_dma_region.phys_addr = vspa_dma_region->phys_addr;
		vspadev->vspa_dma_region.vaddr = vspa_dma_region->vaddr;
		vspadev->vspa_dma_region.size = GUL_VSPA_SCRATCH_BUF_MAX_SIZE;

		err = __vspa_probe(gul_dev, vspa_num);
		if (err == -EINVAL)
			goto err_out;
		else if (err == -EBADRQC)
			goto err_mem_single_out;

		dev_dbg(gul_dev->dev, "DONE VSPA%d Boot..\n\r", vspa_num);

		g_gul_global[gul_dev->id].vspa_firmware_status[vspa_num] = 1;
	} /*End of foor loop*/

	dev_info(gul_dev->dev, "VSPA Firmware %s\n\r",
			max_vspadev[0]->eld_filename);
	gul_set_host_ready(gul_dev, HIF_HOST_READY_VSPA1 |
			HIF_HOST_READY_VSPA2 | HIF_HOST_READY_VSPA3
			| HIF_HOST_READY_VSPA4 | HIF_HOST_READY_VSPA5
			| HIF_HOST_READY_VSPA6 | HIF_HOST_READY_VSPA7
			| HIF_HOST_READY_VSPA8);

	return 0;

err_mem_single_out:
	while (vspa_num--) {
		kfree(max_vspadev[vspa_num]);
		g_gul_global[gul_dev->id].vspa_firmware_status[vspa_num] = 0;
	}
err_mem_out:
	kfree(max_vspadev);
	gul_dev->vspa_priv = NULL;
err_out:
	return err;
}

static int __vspa_remove(struct vspa_device *vspadev, int vspa_num)
{
	kfree(vspadev);
	return 0;
}
int vspa_remove(struct gul_dev *gul_dev)
{
	struct vspa_device **max_vspadev = (struct vspa_device **)
						gul_dev->vspa_priv;
	struct vspa_device *vspadev = NULL;
	int vspa_num = 0;

	if (!gul_dev->vspa_priv)
		return 0;

	dev_dbg(gul_dev->dev, "Calling %s\n\r", __func__);
	for (vspa_num = 0; vspa_num < MAX_VSPA; vspa_num++) {
		vspadev = (struct vspa_device *) max_vspadev[vspa_num];
		if (!vspadev)
			goto out_remove;

		__vspa_remove(vspadev, vspa_num);
	}
	gul_dev->vspa_priv = NULL;
out_remove:
	return 0;
}

int __vspa_reset_mask_show(struct gul_dev *gul_dev, char *buf)
{
	int len = 0;

	len = sprintf(buf, "%x\n", gul_dev->vspa_reset_mask);
	return len;
}

int __vspa_reset_mask_set(struct gul_dev *gul_dev, u32 mask)
{
	gul_dev->vspa_reset_mask = mask;
	return 0;
}

int __vspa_reset_set(struct gul_dev *gul_dev, u32 val)
{
	int vspa_num;
	int ret = 0;
	struct vspa_device **max_vspadev
		= (struct vspa_device **) gul_dev->vspa_priv;
	struct vspa_device *vspadev;

	for (vspa_num = 0; vspa_num < GUL_VSPA_CORE_MAX; vspa_num++) {
		vspadev = (struct vspa_device *) max_vspadev[vspa_num];
		memset(&vspadev->vspa_sec_info, 0, sizeof(vspadev->vspa_sec_info));
		if ((1 << vspa_num) & gul_dev->vspa_reset_mask) {
			ret = powerdown(vspadev);
			if (!ret) {
				ret = powerup(vspadev);
				ret = __vspa_probe(gul_dev, vspa_num);
				if (ret) {
					dev_err(vspadev->dev,
					"vspa reset failed for vspa_num=%d\n",
					vspa_num);
					break;
				}
			}
		}
	}

	return ret;
}
