/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2020-2025 NXP
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/swait.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/delay.h>

#include <gul_host_if.h>
#include "gul_base.h"
#include "gul_dcs.h"
#include "gul_vspa.h"
#include "gul_ipc_kern.h"
#include "gul_stats.h"
#include <linux/of_irq.h>
#ifndef GUL_RESET_HANDSHAKE_POLLING_ENABLE
#include <linux/completion.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#endif
#include <linux/eventfd.h>
#ifdef RF_DRVR_ENABLED
#include "gul_rfic.h"
#endif
#ifdef HAWK_DRVR_ENABLED
#include "gul_hawk.h"
#endif

#ifdef YUCCA_RF_DRVR_ENABLED
#include "gul_fr1_rfic.h"
#endif

#ifdef USIM_DRVR_ENABLED
#include "gul_usim.h"
#endif

int gul_pcal6416_default_init(void);

static int gul_subdrv_cnt_g;
static const char *host_board_version_str;
static struct gul_sub_driver *gul_get_subdrv(int i);
#if 0
static int gul_get_subdrv_virqmap(struct gul_dev *gul_dev,
				  struct gul_sub_driver *subdrv,
				  struct virq_evt_map *subdrv_virqmap,
				  int subdrv_virqmap_size);
#endif
int gul_map_mem_regions(struct gul_dev *gul_dev)
{
	struct gul_mem_region_info *mem_region;
	phys_addr_t phys_addr;
	u8 __iomem *vaddr;
	int i, rc = 0, size;

	for (i = GUL_MEM_REGION_CCSR; i < GUL_MEM_REGION_BAR_END; i++) {
		mem_region	= &gul_dev->mem_regions[i];
		phys_addr	= mem_region->phys_addr;
		size		= mem_region->size;
		size		= ALIGN(size, PAGE_SIZE);

		if (phys_addr && size) {
			vaddr = ioremap(phys_addr, size);
			if (!vaddr) {
				dev_err(gul_dev->dev,
					"ioremap failed, adr %llx, size %d",
					phys_addr, size);
				rc = -ENOMEM;
				goto out;
			}
			dev_info(gul_dev->dev, "mem[%d] phy %llx, vaddr %px\n",
				i, phys_addr, vaddr);
			mem_region->vaddr = vaddr;
			mem_region->type = i;
		}
	}

out:
	return rc;
}

void gul_unmap_mem_regions(struct gul_dev *gul_dev)
{
	struct gul_mem_region_info *mem_region;
	u8 __iomem *vaddr;
	int i;

	for (i = 0; i < GUL_MEM_REGION_BAR_END; i++) {
		mem_region = &gul_dev->mem_regions[i];
		vaddr = mem_region->vaddr;
		if (vaddr) {
			dev_info(gul_dev->dev,
				"unmap: region %d, vaddr %px, phys %llx",
				i, vaddr, mem_region->phys_addr);
			iounmap(vaddr);
			mem_region->vaddr = NULL;
		}
	}
}

int gul_udev_load_firmware(struct gul_dev *gul_dev, char *buf,
			   int buff_sz, char *name, int *fw_size)
{
	int rc = 0, size;
	const struct firmware *fw;

	rc = request_firmware(&fw, name, gul_dev->dev);
	if (rc) {
		dev_err(gul_dev->dev, "Failed to load %s, %d\n", name, rc);
		goto out;
	}

	size = fw->size;
	dev_dbg(gul_dev->dev, "Firmware \"%s\" downloaded at 0x%px size: %d\n",
		 name, fw->data, size);

	if (buff_sz < size) {
		dev_err(gul_dev->dev, "Insufficient fw buff %px: siz %d\n",
					buf, size);
		rc = -ENOBUFS;
	} else {
		dev_dbg(gul_dev->dev, "Copy fw to %px, size %d\n", buf, size);
		memcpy(buf, fw->data, size);
		*fw_size = size;
	}
	dev_dbg(gul_dev->dev, "memcopy finished..!!\n");

	release_firmware(fw);
out:
	return rc;
}

void ls_pcie_iatu_outbound_set(void __iomem *dbi, int idx, int type,
		u64 cpu_addr, u64 pci_addr, u32 size)
{
	writel(PCIE_ATU_REGION_OUTBOUND | idx, dbi + PCIE_ATU_VIEWPORT);
	writel(lower_32_bits(cpu_addr), dbi +  PCIE_ATU_LOWER_BASE);
	writel(upper_32_bits(cpu_addr), dbi + PCIE_ATU_UPPER_BASE);
	writel(lower_32_bits(cpu_addr + size - 1), dbi + PCIE_ATU_LIMIT);
	writel(lower_32_bits(pci_addr), dbi + PCIE_ATU_LOWER_TARGET);
	writel(upper_32_bits(pci_addr), dbi + PCIE_ATU_UPPER_TARGET);
	writel(type, dbi + PCIE_ATU_CR1);
	writel(PCIE_ATU_ENABLE, dbi + PCIE_ATU_CR2);
}
EXPORT_SYMBOL_GPL(ls_pcie_iatu_outbound_set);

int gul_create_outbound_msi(struct gul_dev *gul_dev)
{

	struct gul_mem_region_info *ccsr_region;
	u64 msi_msg_addr = 0, mod_msi_phys;
	u32 pcie_offset;
	u32 msi_addr_offset, msi_val;
	struct gul_hif *hif = gul_dev->hif;
	int i;

	ccsr_region = &gul_dev->mem_regions[GUL_MEM_REGION_CCSR];

	dev_dbg(gul_dev->dev, "CCSR: vaddr %px, size %d\n",
		 ccsr_region->vaddr, (int) ccsr_region->size);

	/* out bound for MSI */
	pcie_offset = PCIE_RHOM_DBI_BASE + PCIE_MSI_MSG_ADDR_OFF;
	msi_msg_addr = readl(ccsr_region->vaddr + pcie_offset) |
	(((u64)readl(ccsr_region->vaddr + pcie_offset + 4)) << 32);
	mod_msi_phys = GUL_EP_TOHOST_MSI_PHY_ADDR;
	dev_dbg(gul_dev->dev, "MSI:ATU: DBI 0x%px, DMA %llx, EP %llx\n",
		 (ccsr_region->vaddr + pcie_offset),
		 msi_msg_addr, mod_msi_phys);
	/* outbound iATU for MSI. From GUL to host*/
	ls_pcie_iatu_outbound_set(ccsr_region->vaddr + PCIE_RHOM_DBI_BASE,
				GUL_OB_WIN_MSI,
				PCIE_ATU_TYPE_MEM,
				mod_msi_phys, /*Modem Physical addr*/
				msi_msg_addr, /*Host Physical addr*/
				GUL_EP_TO_HOST_MSI_SIZE);

	pcie_offset = PCIE_RHOM_DBI_BASE + PCIE_MSI_MSG_DATA_OFF;
	msi_val = readl(ccsr_region->vaddr + pcie_offset);
	msi_addr_offset = GUL_EP_TOHOST_MSI_PHY_ADDR - GUL_USER_HUGE_PAGE_ADDR;
	for (i = 0; i < MSI_IRQ_COUNT; i++) {
		writel(msi_addr_offset, &hif->msi_regs[i].msi_addr_off_l);
		writel(msi_val, &hif->msi_regs[i].msi_val);
		msi_val++;
	}
	dev_dbg(gul_dev->dev, "MSI ATU done\n");
	return 0;
}

struct gul_mem_region_info *scratch_buf_allocator(struct gul_dev *gul_dev,
						enum scratch_buf_request_id id,
						int size)
{
	uint64_t mod_phys_current, mod_phys_end, offset;
	struct gul_mem_region_info *scratch_buf_region =
					&gul_dev->scratch_buf_region[id];
	struct scratch_allocator *scr = &gul_dev->scratch_allocator;

	if (!scratch_buf_region) {
		dev_err(gul_dev->dev, "%s:scratch_buf_region (%d):invalid\n",
			__func__, id);
		return NULL;
	}

	if (scratch_buf_region->size) {
		dev_dbg(gul_dev->dev,
			"%s:returning exising scratch_buf_region (%d)\n",
			__func__, id);
		return scratch_buf_region;
	}

	mod_phys_current = scr->mod_phys_current;
	mod_phys_end = scr->mod_phys_end;
	if (size > (mod_phys_end - mod_phys_current)) {
		dev_err(gul_dev->dev,
			"Scratch OOM! Req siz 0x%x, left 0x%llx (%d)\n",
			size, (mod_phys_end - mod_phys_current), id);
		return NULL;
	}

	offset = scr->mod_phys_current - scr->mod_phys;

	scratch_buf_region->vaddr = scr->host_vaddr + offset;
	scratch_buf_region->phys_addr = scr->mod_phys + offset;
	scratch_buf_region->size = size;

	scr->mod_phys_current += size;

	dev_dbg(gul_dev->dev, "scratch Alloc: Region id %d\n", id);
	dev_dbg(gul_dev->dev, "Host virt 0x%px, Mod Phys 0x%llx, size %d\n",
		 scratch_buf_region->vaddr, scratch_buf_region->phys_addr,
		 size);

	return scratch_buf_region;
}

static void allocate_scratch_buf_regions(struct gul_dev *gul_dev)
{
	if (modem_share_buf_size) {
		/* Allocating memory for shared are with modem */
		scratch_buf_allocator(gul_dev, GUL_SCRATCH_MODEM_SHARE,
			modem_share_buf_size);
	}
	if (modem_host_data_size)
		scratch_buf_allocator(gul_dev, GUL_HOST_DATA, modem_host_data_size);

	if (modem_rf_data_size)
		scratch_buf_allocator(gul_dev, GUL_RF_DATA, modem_rf_data_size);

	scratch_buf_allocator(gul_dev, GUL_FIRMWARE, GUL_MAX_IMAGE_SIZE);
	scratch_buf_allocator(gul_dev, GUL_SCRATCH_DBG_LOGGER, GUL_EP_LOGGER_SIZE);
	scratch_buf_allocator(gul_dev, GUL_VSPA_FW, GUL_VSPA_SCRATCH_BUF_MAX_SIZE);
	scratch_buf_allocator(gul_dev, GUL_VSPA_OVERLAY, GUL_VSPA_OVERLAY_SIZE);

}

static int gul_scratch_outbound_create(struct gul_dev *gul_dev)
{

	struct gul_mem_region_info *ccsr_region;
	struct scratch_allocator *scratch_allocator =
		&gul_dev->scratch_allocator;

	ccsr_region = &gul_dev->mem_regions[GUL_MEM_REGION_CCSR];

	ls_pcie_iatu_outbound_set(ccsr_region->vaddr + PCIE_RHOM_DBI_BASE,
			GUL_OB_WIN_SCRATCH_BUF, PCIE_ATU_TYPE_MEM,
			scratch_allocator->mod_phys,/* Modem cpu addr*/
			scratch_allocator->host_phys,/* Host Physical address*/
			scratch_allocator->size);

	dev_info(gul_dev->dev,
		"Scratch buff ATU:0x%llx[Host],0x%llx[Modem],size=%d (0x%x)\n",
		scratch_allocator->host_phys, scratch_allocator->mod_phys,
		(int) scratch_allocator->size, (int) scratch_allocator->size);

	return 0;
}

#if 0
static int gul_create_outbound_dma(struct gul_dev *gul_dev)
{

	struct gul_mem_region_info *ccsr_region;
	struct gul_mem_region_info *host_dma_region;

	ccsr_region = &gul_dev->mem_regions[GUL_MEM_REGION_CCSR];
	host_dma_region = &gul_dev->dma_info.host_buf;

	dev_info(gul_dev->dev, "CCSR: vaddr %px, size %d\n",
		 ccsr_region->vaddr, (int) ccsr_region->size);

	dev_info(gul_dev->dev, "DMA:ATU: DBI 0x%px, DMA %llx, EP %llx\n",
		 (ccsr_region->vaddr + PCIE_RHOM_DBI_BASE),
		 host_dma_region->phys_addr,
		 GUL_EP_DMA_BUF_PHYS_ADDR);
	/*
	 * outbound iATU for GUL bin memory.
	 * Access memory from GUL to host
	 * Copy rattler.bin from host memory to GUL memory.
	 */
	ls_pcie_iatu_outbound_set(ccsr_region->vaddr + PCIE_RHOM_DBI_BASE,
			GUL_OB_WIN_DMA_BUF, PCIE_ATU_TYPE_MEM,
			GUL_EP_DMA_BUF_PHYS_ADDR,/*cpu addr*/
			host_dma_region->phys_addr,/*pci addr 1 to 1 map*/
			host_dma_region->size);

	dev_info(gul_dev->dev, "DMA ATU done\n");
	gul_dev->dma_info.ep_pcie_addr = GUL_EP_DMA_BUF_PHYS_ADDR;

	return 0;
}
#endif

struct gul_mem_region_info *gul_get_scratch_region(struct gul_dev *gul_dev,
					enum scratch_buf_request_id id)
{
	struct gul_mem_region_info *pebm_buf;

	pebm_buf = &gul_dev->scratch_buf_region[id];
	return pebm_buf;
}

struct gul_mem_region_info *gul_get_dma_region(struct gul_dev *gul_dev,
					enum gul_mem_region_t type)
{
	struct gul_mem_region_info *ep_buf;
	int idx;

	idx = GUL_SUBDRV_DMA_REGION_IDX(type);
	ep_buf = &gul_dev->dma_info.ep_bufs[idx];

	return ep_buf;
}

static int gul_scratch_buf_init(struct gul_dev *gul_dev)
{
	int rc = 0;
	struct scratch_allocator *scr = &gul_dev->scratch_allocator;
	uint64_t gul_scratch_buf_phys_addr;
	int gul_scratch_buf_size;

	/* usages per modem */
	gul_scratch_buf_size = GUL_MAX_IMAGE_SIZE + GUL_EP_LOGGER_SIZE
			+ GUL_VSPA_SCRATCH_BUF_MAX_SIZE
			+ GUL_VSPA_OVERLAY_SIZE
			+ modem_share_buf_size
			+ modem_host_data_size
			+ modem_rf_data_size;

	gul_scratch_buf_size = ALIGN(gul_scratch_buf_size, PAGE_SIZE);

	gul_scratch_buf_phys_addr = scratch_buf_phys_addr +
					gul_dev->id * gul_scratch_buf_size;

	if (gul_scratch_buf_phys_addr > scratch_buf_phys_addr + global_scratch_buf_size) {
		dev_err(gul_dev->dev,
			"Err:ID Dev-%d Scratch buffer size required (0x%x/perDev=0x%x) is more than provisioined (0x%x)\n",
			gul_dev->id, gul_scratch_buf_size*(gul_dev->id+1),
			gul_scratch_buf_size,
			global_scratch_buf_size);
		return -ENOMEM;
	}

	g_gul_global[gul_dev->id].scratch_buf_size = gul_scratch_buf_size;
	g_gul_global[gul_dev->id].scratch_buf_phys_addr =
					gul_scratch_buf_phys_addr;

	/* we are mapping the scratch buf size area for this modem*/
	scr->host_vaddr = memremap(gul_scratch_buf_phys_addr,
			gul_scratch_buf_size, MEMREMAP_WB);
	if (!scr->host_vaddr) {
		dev_err(gul_dev->dev, "ERR: ioremap DDR Address Failed\n");
		iounmap(scr->host_vaddr);
		return -ENOMEM;
	}

	scr->host_phys = gul_scratch_buf_phys_addr;
	scr->size = gul_scratch_buf_size;
	/**
	 * Each device mapped to it's own address space. This way,
	 * we can use maximum pcie address space per device.
	 */
	 /* Taking the address from the end side */
	scr->mod_phys = GUL_SCRATCH_DMA_BUF_END_ADDR - gul_scratch_buf_size;
	scr->mod_phys_current = scr->mod_phys;
	scr->mod_phys_end = scr->mod_phys + scr->size;

	dev_info(gul_dev->dev,
		"Scratch buff Phys = 0x%0llx, Virt=%px Size = 0x%x",
		gul_scratch_buf_phys_addr, scr->host_vaddr,
		gul_scratch_buf_size);

	rc = gul_scratch_outbound_create(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "scratch buf outbound window creation failed\n");
		goto out;
	}

	if (gul_modinfo_init(gul_dev))
		goto out;

	return 0;
out:
	memunmap(scr->host_vaddr);

	return rc;
}

static int gul_free_dma_buf(struct gul_dev *gul_dev)
{

	struct gul_dma_info *dma_info = &gul_dev->dma_info;
	struct gul_mem_region_info *host_region;

	host_region = &dma_info->host_buf;

	if (host_region->vaddr) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
		dma_free_coherent(&((struct pci_dev *)gul_dev->pdev)->dev,
				host_region->size,
				host_region->vaddr,
				host_region->phys_addr);
 #else
		pci_free_consistent(gul_dev->pdev,
				host_region->size,
				host_region->vaddr,
				host_region->phys_addr);
 #endif
		memset(dma_info, 0, sizeof(struct gul_dma_info));
	}

	return 0;
}
static int gul_verify_hif_compatibility(struct gul_dev *gul_dev)
{
	struct gul_hif *hif = gul_dev->hif;
	u32 hif_ep_version, hif_host_version;
	int rc = 0;

	hif_host_version = GUL_VER_MAKE(GUL_HIF_MAJOR_VERSION,
					GUL_HIF_MINOR_VERSION);
	hif_ep_version = readl(&hif->hif_ver);

	/* magic added to check HIF curroption */
	writel(0xcafebabe, &hif->status);

	if (hif_ep_version != hif_host_version) {
		dev_err(gul_dev->dev, "HIF ver mismatch, Host 0x%x, GUL 0x%x\n",
			hif_host_version, hif_ep_version);
		rc = -EINVAL;
		goto out;

	}
out:
	dev_dbg(gul_dev->dev, "HIF Version : %d.%d\n",
		 GUL_VER_MAJOR(hif_ep_version), GUL_VER_MINOR(hif_ep_version));
	return rc;
}

void gul_set_host_ready(struct gul_dev *gul_dev, u32 set_bit)
{
	struct gul_hif *hif = gul_dev->hif;

	SET_HIF_HOST_RDY(hif, set_bit);
}

static void hif_init_region_addrs(struct gul_dev *gul_dev)
{
	struct host_mem_region *region;
	uint32_t ml, mh = 0, size;
	struct scratch_allocator *scr = &gul_dev->scratch_allocator;
	struct gul_hif *hif = gul_dev->hif;

	ml = SPLIT_VA32_L(scr->mod_phys);
	mh = SPLIT_VA32_H(scr->mod_phys);

	region = &hif->host_regions[HOST_MEM_SCRATCH_BUF];
	writel(ml, &region->mod_phys_l);
	writel(mh, &region->mod_phys_h);
	writel(g_gul_global[gul_dev->id].scratch_buf_size, &region->size_l);
	writel(g_gul_global[gul_dev->id].share_buf_size, &hif->modem_share_area_size);
	writel(modem_host_data_size, &hif->modem_host_data_size);
	writel(modem_rf_data_size, &hif->modem_rf_data_size);

	/* Scratch buffer to start pcie address space is for IPC/usr */
	ml = SPLIT_VA32_L(GUL_USER_HUGE_PAGE_ADDR);
	mh = SPLIT_VA32_H(GUL_USER_HUGE_PAGE_ADDR);
	region = &hif->host_regions[HOST_MEM_HUGE_PAGE_BUF];
	writel(ml, &region->mod_phys_l);
	writel(mh, &region->mod_phys_h);
	size = scr->mod_phys - GUL_USER_HUGE_PAGE_ADDR;
	writel(size, &region->size_l);

	dma_wmb();
	SET_HIF_HOST_RDY(hif, HIF_HOST_READY_HOST_REGIONS);
	if (tbgen2_disable == 1)
		SET_HIF_HOST_RDY(hif, HIF_HOST_DISABLE_TBGEN2);
}

static int gul_init_hif(struct gul_dev *gul_dev)
{
	struct gul_mem_region_info *pebm_region;
	int rc = 0;

	if (sizeof(struct gul_hif) != gul_dev->hif_size) {

		dev_err(gul_dev->dev,
			"LA12xx firmware(%s) not compatible with Yami driverHIF siz mismatch %d!=%d",
			firmware_name, (int) sizeof(struct gul_hif), gul_dev->hif_size);
		rc = -EINVAL;
		goto out;
	}

	/*GUL Host interface (HIF) @ PEBM + hif_offset*/
	pebm_region = &gul_dev->mem_regions[GUL_MEM_REGION_PEBM];
	gul_dev->hif = (struct gul_hif *)((u64) pebm_region->vaddr +
					  gul_dev->hif_offset);
	/* Verify that Host and target are using same version of HIF */
	rc = gul_verify_hif_compatibility(gul_dev);
	if (rc)
		goto out;
	hif_init_region_addrs(gul_dev);

	if (rfic_disable)
		writel(1, &gul_dev->hif->rfic_regs.spi_access_disabled);
out:
	return rc;
}

static void gul_init_ep_logger(struct gul_dev *gul_dev)
{
	struct gul_mem_region_info *logger_region;
	struct gul_ep_log *ep_log;
	struct gul_hif *hif = gul_dev->hif;
	struct debug_log_regs *dbg_log_regs;
	int core_id = 0;

	logger_region = scratch_buf_allocator(gul_dev,
				GUL_SCRATCH_DBG_LOGGER,
				GUL_EP_LOGGER_SIZE);
	if (!logger_region) {
		dev_err(gul_dev->dev, "EP Logger: Memory Alocation Failed\n");
		return;
	}
	logger_region->type = GUL_MEM_REGION_DBG_LOG;

	for (core_id = 0; core_id < gul_ep_get_numcores(); core_id++) {
		/* Initializing EP log buffer */
		ep_log = &gul_dev->ep_log[core_id];
		ep_log->buf = logger_region->vaddr +
				(core_id * GUL_CORE_LOG_BUF_SIZE);
		ep_log->len = GUL_CORE_LOG_BUF_SIZE;
		ep_log->offset = 0;
		memset_io(ep_log->buf, 0, ep_log->len);
		dev_dbg(gul_dev->dev,
			"[CORE %d]:Logger init vaddr %px, len %d, offset %d\n",
			core_id, ep_log->buf, ep_log->len,
			ep_log->offset);

		/* update HIF to tell GUL the pointer to log buffer*/
		dbg_log_regs = &hif->dbg_log_regs[core_id];
		dev_dbg(gul_dev->dev, "Logger: M Phys_addr = %llx\n",
				logger_region->phys_addr);
		iowrite32be((logger_region->phys_addr +
				(core_id * GUL_CORE_LOG_BUF_SIZE)),
				&dbg_log_regs->buf);
		iowrite32be(GUL_CORE_LOG_BUF_SIZE, &dbg_log_regs->len);
	}
	gul_set_host_ready(gul_dev, HIF_HOST_READY_LOGGER);
}


ssize_t gul_ep_show_stats(void *stats_args, char *buf, void *dev)
{
	ssize_t len = 0;
#if GUL_AVI_STATS_ENABLE
	ssize_t count = 0;
#endif
	struct gul_dev *gul_dev = (struct gul_dev *)dev;
	struct gul_stats *stats = (struct gul_stats *)stats_args;

	len += sprintf((buf + len), "GUL End Point Stats:\n");

	if (gul_dev->stats_desc.stats_control & (1 << EP_CONTROL_IRQ_STATS)) {
#if GUL_AVI_STATS_ENABLE
		for (count = 0; count < GUL_VSPA_CORE_MAX; count++) {
			len += sprintf((buf + len),
			"VSPA[%ld] avi_E200_mbox0_tx_count=%u\n",
			count, ioread32be(
			&stats->vspa_avi_stats[count].avi_E200_mbox0_tx_cnt));
			len += sprintf((buf + len),
			"VSPA[%ld] avi_E200_mbox1_tx_count=%u\n",
			count, ioread32be(
			&stats->vspa_avi_stats[count].avi_E200_mbox1_tx_cnt));
			len += sprintf((buf + len),
			"VSPA[%ld] avi_E200_mbox0_rx_count=%u\n",
			count, ioread32be(
			&stats->vspa_avi_stats[count].avi_E200_mbox0_rx_cnt));
			len += sprintf((buf + len),
			"VSPA[%ld] avi_E200_mbox1_rx_count=%u\n",
			count, ioread32be(
			&stats->vspa_avi_stats[count].avi_E200_mbox1_rx_cnt));
		}
#endif
	}

	/* Update Modem stats here */
	len += sprintf((buf + len), "\nGUL Modem Stats:\n");

	if (gul_dev->stats_desc.stats_control & (1 << EP_CONTROL_IRQ_STATS)) {
		len += sprintf((buf + len), "timer_irq_cnt %d\n",
				stats->mpic_stats.timer_irq_cnt);
	}

	if (gul_dev->stats_desc.stats_control & (1 << EP_CONTROL_IRQ_STATS)) {
		len += sprintf((buf + len), "msi_irq_cnt %d\n",
				stats->mpic_stats.msi_irq_cnt);
	}

	if (gul_dev->stats_desc.stats_control & (1 << EP_CONTROL_IRQ_STATS)) {
		len += sprintf((buf + len), "spinlock_count %d\n",
				stats->spinlock_stats.spinlock_count);
	}

	return len;
}

void gul_ep_reset_stats(void *stats_args)
{
	struct gul_stats *stats = (struct gul_stats *)stats_args;

	memset_io(stats, 0, sizeof(struct gul_stats));
}

/*XXX:TBD: Sysfs files are not yet supported*/

int gul_register_ep_stats_ops(struct gul_dev *gul_dev)
{
	struct gul_hif *hif = gul_dev->hif;
	struct gul_stats_ops stats_ops;

	stats_ops.gul_show_stats = gul_ep_show_stats;
	stats_ops.gul_reset_stats = gul_ep_reset_stats;
	stats_ops.stats_args = &hif->stats;

	return gul_host_add_stats(gul_dev, &stats_ops);
}

/*
 * MPIC MSI IRQs to trigger host->modem interrupts
 */
void raise_modem_msi(struct gul_dev *gul_dev, int msi_type, int irq_no)
{
	struct host_msi_unit *ccsr_hmsi = NULL;
	int mpic_reg_offset = MPIC_REG_MSIIR;

	switch (msi_type)
	{
		case MSI_TYPE_A:
			mpic_reg_offset = MPIC_REG_MSIIR;
			break;
		case MSI_TYPE_B:
			mpic_reg_offset = MPIC_REG_MSIIRB;
			break;
		case MSI_TYPE_C:
			mpic_reg_offset = MPIC_REG_MSIIRC;
			break;
	}


	ccsr_hmsi = (struct host_msi_unit *)(
		gul_dev->mem_regions[GUL_MEM_REGION_CCSR].vaddr +
		(MPIC_BASE_ADDRESS + mpic_reg_offset));

	/* Raise Host message signal interrupt to Modem */
	iowrite32be(
	((irq_no << MPIC_MSIIR_SRS_BIT) | (irq_no << MPIC_MSIIR_IBS_BIT)),
	&(ccsr_hmsi->msiir));

	/* Memory Barrier */
	dma_wmb();

	dev_dbg(gul_dev->dev, "[%s] Value written to MSIIR is: %x\n", __func__,
			ioread32be(&(ccsr_hmsi->msiir)));
}

static int gul_tti_dev_open(struct inode *inode, struct file *filp)
{
	struct gul_tti_device_data *tti_dev = NULL;

	tti_dev = container_of(inode->i_cdev, struct gul_tti_device_data, cdev);
	filp->private_data = tti_dev;
	return 0;
}

static int gul_tti_dev_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static ssize_t gul_tti_dev_read(struct file *filp, char __user *buf,
				size_t count, loff_t *offset)
{
	int rc = 0;
	DECLARE_SWAITQUEUE(wait);
	struct gul_tti_device_data *tti_dev = NULL;

	tti_dev = filp->private_data;
	raw_spin_lock(&tti_dev->tti_dev->tti_priv_t->wq_lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	prepare_to_swait_exclusive(&tti_dev->tti_dev->tti_priv_t->tti_wq, &wait,
						TASK_INTERRUPTIBLE);
#else
	prepare_to_swait(&tti_dev->tti_dev->tti_priv_t->tti_wq, &wait,
						TASK_INTERRUPTIBLE);
#endif
	raw_spin_unlock(&tti_dev->tti_dev->tti_priv_t->wq_lock);

	/*Now wait here, tti notificaion will wakeup*/
	schedule();

	raw_spin_lock(&tti_dev->tti_dev->tti_priv_t->wq_lock);
	finish_swait(&tti_dev->tti_dev->tti_priv_t->tti_wq, &wait);
	raw_spin_unlock(&tti_dev->tti_dev->tti_priv_t->wq_lock);

	rc = put_user(tti_dev->tti_dev->tti_priv_t->tti_count, (int *)buf);
	if (!rc)
		rc = sizeof(tti_dev->tti_dev->tti_priv_t->tti_count);
	return rc;
}

static long gul_tti_dev_ioctl(struct file *filp, unsigned int cmd,
			unsigned long arg)
{
	int ret = 0;
	struct tti tti_t = {0};
	struct gul_tti_device_data *tti_dev = NULL;
	struct tti_dev *tti_device = NULL;

	tti_dev = (struct gul_tti_device_data *)filp->private_data;
	tti_device = tti_dev->tti_dev;

	switch (cmd) {
	case IOCTL_GUL_MODEM_TTI_REGISTER:
		ret = copy_from_user(&tti_t, (struct tti *)arg,
						sizeof(struct tti));
		if (ret != 0)
			return -EFAULT;
		ret = tti_register_irq(tti_device, &tti_t);
		break;

	case IOCTL_GUL_MODEM_TTI_DEREGISTER:
		ret = copy_from_user(&tti_t, (struct tti *)arg,
						sizeof(struct tti));
		if (ret != 0)
			return -EFAULT;
		tti_deregister_irq(tti_device, &tti_t);
		break;

	default:
		ret = -ENOTTY;
	}
	return ret;
}


/* TTI initialize file_operations */
static const struct file_operations gul_tti_dev_fops = {
	.owner		= THIS_MODULE,
	.open		= gul_tti_dev_open,
	.release	= gul_tti_dev_release,
	.read		= gul_tti_dev_read,
	.unlocked_ioctl = gul_tti_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gul_tti_dev_ioctl,
#endif
};

static struct class *gul_tti_dev_class;
static dev_t tti_dev_number;
static int tti_dev_major;
static struct gul_tti_device_data **tti_dev_data;

ssize_t
tti_device_dump(int id, char *buf)
{
	struct tti_priv *priv;
	int i;

	for (i = 0; i < tti_per_dev; i++) {
		if (tti_dev_data[id][i].tti_dev == NULL)
			continue;
		priv = &tti_dev_data[id][i].tti_dev->tti_priv_t[0];

		sprintf(&buf[strlen(buf)],
			" TTI:gulttidev%d-%d irq %d irq status=%d\n",
			id, i, priv->irq,
			priv->tti_irq_status);
	}
	return 0;
}

int init_tti_dev(void)
{
	int j, err;
#if defined (LA1224)
	char brd_ver;

	brd_ver = gul_get_host_board_rev();
	/* Updating default value of tti_per_dev = 3 for LA1224 Rev C */
		if (!disable_sideband)
		{
			if (brd_ver == 'C')
			tti_per_dev = 3;
		}
		else{
			tti_per_dev = 1;
		}
#endif
	/*Allocating chardev region and assigning Major number*/
	err = alloc_chrdev_region(&tti_dev_number, 0,
				  MAX_MODEM*tti_per_dev,
				  "gulttidev");
	/* Device Major number*/
	tti_dev_major = MAJOR(tti_dev_number);
	/*sysfs class creation */
	gul_tti_dev_class = class_create(THIS_MODULE, "gulttidev");

	tti_dev_data = kmalloc(MAX_MODEM * sizeof(*tti_dev_data), GFP_KERNEL);
	if (tti_dev_data == NULL) {
		pr_err("TTI device data array alloc failed\n");
		err = -ENOMEM;
		goto out;
	}
	for (j = 0; j < MAX_MODEM; j++) {
		tti_dev_data[j] =
			kzalloc(tti_per_dev*sizeof(*tti_dev_data[j]),
				GFP_KERNEL);
		if (tti_dev_data[j] == NULL) {
			pr_err("TTI device data alloc failed\n");
			err = -ENOMEM;
			break;
		}
	}
out:
	return err;
}

void remove_tti_dev(void)
{
	int j;

	class_destroy(gul_tti_dev_class);
	gul_tti_dev_class = NULL;
	unregister_chrdev_region(tti_dev_number, MAX_MODEM*tti_per_dev);

	for (j = 0; j < MAX_MODEM; j++)
		kfree(tti_dev_data[j]);

	kfree(tti_dev_data);
}

void tti_dev_stop(struct gul_dev *dev)
{
	struct tti_priv *priv;
	int i, j;

	j = dev->id;

	for (i = 0; i < tti_per_dev; i++) {
		if (tti_dev_data[j][i].tti_dev == NULL)
			continue;
		priv = &tti_dev_data[j][i].tti_dev->tti_priv_t[0];
		if (priv && priv->tti_irq_status) {
			free_irq(priv->irq, priv);
			priv->tti_irq_status = 0;
		}
		cdev_del(&tti_dev_data[j][i].cdev);
		device_destroy(gul_tti_dev_class, MKDEV(tti_dev_major,
				j*tti_per_dev + i));
		kfree(tti_dev_data[j][i].tti_dev);
		tti_dev_data[j][i].tti_dev = NULL;
	}
}

int tti_dev_start(struct gul_dev *dev)
{
	struct device_node *dn_modem_tti;
	struct tti_dev *tti_dev = NULL;
	int i, j;

	j = dev->id;

	if (!disable_sideband) {
		/* TTI Interrupt Number extraction from device node */
		if (dev->dn_modem)
			of_node_get(dev->dn_modem);

		dn_modem_tti = of_find_node_by_name(dev->dn_modem, "modem_tti");
		if (!dn_modem_tti) {
			dev_err(dev->dev, "Error:modem_tti Node missing in DTB\n");
			return -ENODEV;
		}
	}
	for (i = 0; i < tti_per_dev; i++) {
		tti_dev = kmalloc(sizeof(struct tti_dev), GFP_KERNEL);
		if (tti_dev == NULL)
			return -ENOMEM;

		if (!disable_sideband)
			tti_dev->tti_priv_t[0].irq = of_irq_get(dn_modem_tti, i);
		else {
			tti_dev->tti_priv_t[0].msi_index = gul_dev_get_msi(dev);
			tti_dev->tti_priv_t[0].irq =
				dev->irq[tti_dev->tti_priv_t[0].msi_index].irq_val;
		}

		if (tti_dev->tti_priv_t[0].irq < 0) {
			dev_dbg(dev->dev,
				"ttidev: TTI IRQ-%d not available in DTB for Modem Instance:%d\n",
				i, j+1);
			kfree(tti_dev);
			break;
		}

		tti_dev->tti_priv_t[0].tti_irq_status = 0;
		/*simple wait queue init*/
		init_swait_queue_head(&tti_dev->tti_priv_t[0].tti_wq);
		/*raw spinlock init for TTI module*/
		raw_spin_lock_init(&tti_dev->tti_priv_t[0].wq_lock);

		tti_dev_data[j][i].tti_dev = tti_dev;

		if (device_create(gul_tti_dev_class, NULL,
				MKDEV(tti_dev_major, j*tti_per_dev + i),
				NULL, "gulttidev%d-%d", j, i) == NULL) {
			dev_dbg(dev->dev,
				"tti device_create failed :%d:%d\n", j, i);
			kfree(tti_dev);
			break;
		}

		cdev_init(&tti_dev_data[j][i].cdev, &gul_tti_dev_fops);
		tti_dev_data[j][i].cdev.ops = &gul_tti_dev_fops;
		tti_dev_data[j][i].cdev.owner = THIS_MODULE;
		/* Adding a device to the system:i-Minor number of new device*/
		cdev_add(&tti_dev_data[j][i].cdev, MKDEV(tti_dev_major,
					j*tti_per_dev + i), 1);
	}

	if (i == 0)
		return -ENODEV;

	return 0;
}

int get_fuse_val(struct gul_dev *gul_dev)
{
	struct gul_mem_region_info *ccsr_region;
	uint32_t fuse_personality;

	ccsr_region = &gul_dev->mem_regions[GUL_MEM_REGION_CCSR];

	fuse_personality = readl(ccsr_region->vaddr +
			(DCFG_CCSSR_BASE_ADDR + FUSESR_OFFSET));
	fuse_personality >>= FUSE_PERSONALITY_SHIFT;

	if (fuse_personality == 0) {
		dev_err(gul_dev->dev,
		"WARNING!! LA12xx fuse register reads 0x0, it's unexpected\n");
		return FUSE_INVALID;
	}
	return fuse_personality;
}

int gul_subdrv_check_fail(struct gul_sub_driver *subdrv)
{
	if (vspa_disable && subdrv->type == GUL_SUBDRV_TYPE_VSPA)
		return -1;

	if (rfic_disable && subdrv->type == GUL_SUBDRV_TYPE_RFIC)
		return -1;

	if (tvd_disable && subdrv->type == GUL_SUBDRV_TYPE_TVD)
		return -1;

	if (wdog_disable && subdrv->type == GUL_SUBDRV_TYPE_WDOG)
		return -1;

	return 0;
}
char gul_hsdcs_check(enum soc_fuse soc_fuse)
{
	switch (soc_fuse) {
	case FUSE_LA1215:
	case FUSE_LA1225:
	case FUSE_LA1235:
	case FUSE_LA1216:
	case FUSE_LA1218:
	case FUSE_LA1236:
	case FUSE_LA1238:
	case FUSE_LA1224:
		return  0;
	default:
		return -1;
	}
}

char gul_lsdcs_check(enum soc_fuse soc_fuse)
{
	switch (soc_fuse) {
	case FUSE_LA1212:
	case FUSE_LA1214:
	case FUSE_LA1223:
	case FUSE_LA1232:
	case FUSE_LA1234:
	case FUSE_LA1216:
	case FUSE_LA1218:
	case FUSE_LA1236:
	case FUSE_LA1238:
	case FUSE_LA1224:
		return  0;
	default:
		return -1;
	}
}
int gul_warmup_subdrv_probe_check_fail(struct gul_dev *gul_dev,
		struct gul_sub_driver *subdrv, int modem_id)
{
	struct gul_hif *hif = gul_dev->hif;
	int32_t fuse_personality;
	char hsdcs_check = -1, lsdcs_check = -1;

	if (warmup_flag[modem_id]) {
		switch (subdrv->type) {
		case GUL_SUBDRV_TYPE_DCS:
			if (gul_ep_get_soc_rev() == GEUL_SVR_REVA_VAL)
				return 0;
			else
				return -1;
			break;
		case GUL_SUBDRV_TYPE_WDOG:
		case GUL_SUBDRV_TYPE_VSPA:
			return 0;
		default:
			return -1;
		}
	} else {
		if (subdrv->type == GUL_SUBDRV_TYPE_DCS) {
			if ((gul_get_host_board_rev() != 'A') &&
				(gul_get_host_board_rev() != 'B') &&
				(gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL)) {
				fuse_personality = get_fuse_val(gul_dev);

				/* Returning 0 for invalid fuse,
				   you may see unexpected results */
				if (fuse_personality == FUSE_INVALID)
					return 0;

				if (hsdcs_enable == 1) {
					hsdcs_check = gul_hsdcs_check(fuse_personality);
					if (hsdcs_check == -1) {
						dev_err(gul_dev->dev,
						"This LA12xx device doesn't support HS-DCS\n");
						hsdcs_enable = 0;
					}
				}
				lsdcs_check = gul_lsdcs_check(fuse_personality);
				if ((!hsdcs_check) || (!lsdcs_check))
					return 0;
				else
					return -1;

			} else
				return 0;
		} else
			/*
			 * Checking for non-dcs sub-driver
			 */
			return gul_subdrv_check_fail(subdrv);
	}
	return 0;
}

#if defined (LA1224) || defined(LA1238RDB)
char gul_get_host_board_rev(void)
{
	struct device_node *node;

	if (host_board_version_str)
		return host_board_version_str[strlen(host_board_version_str) - 1];

	node = of_find_node_by_path("/");
	if (node) {
		if (of_property_read_string(node, "model",
						&host_board_version_str))
			host_board_version_str = "Unknown";
		of_node_put(node);
	} else {
		host_board_version_str = "Unknown";
	}

	if (strcmp(host_board_version_str, "Unknown") == 0)
		return 'U'; /* assuming max board versions is less than 20 */
	else
		return host_board_version_str[strlen(host_board_version_str) - 1];
}
#endif

#if defined(ERRATA_A010867)
/*
 * Work around:
 * Firmware in LA1224A may not run alway unless JTAG TCK is first
 * toggle "1-0-1", while holding TMS low.
 *
 */
int gul_errata_a010867(struct gul_dev *gul_dev)
{
	struct device_node *dn_modem;
	int gpio;
	int ret,cnt;

	if (gul_dev->dn_modem)
		of_node_get(gul_dev->dn_modem);

	dn_modem = of_find_node_by_name(gul_dev->dn_modem, "modem_errata_a010867");
	if (!dn_modem) {
		dev_err(gul_dev->dev, "modem_errata_a010867:Node missing in DTB\n");
		return -ENODEV;
	}

	gpio = of_get_named_gpio(dn_modem, "errata-reset-gpio", 0);
	if (!gpio_is_valid(gpio)) {
		dev_err(gul_dev->dev,
		"reset-gpio not defined in dtb\n");
		return -EINVAL;
	}

/**
 * Request gpio pin.
 * GPIO driver alraedy enables input buffer for GPIO pins.
 */
	ret = gpio_request(gpio, "errata reset gpio");
	if (ret) {
		dev_err(gul_dev->dev,
			"%s:Can't request gpio %d\n", __func__, ret);
		return ret;
	}

	mdelay(1);
	/* Configure GPIO pin as an output */
	ret = gpio_direction_output(gpio, 0);
	if (ret < 0) {
		dev_err(gul_dev->dev,
			"%s: Can't configure gpio %d\n", __func__, ret);
		gpio_free(gpio);
		return ret;
	}

        /**
         * LA1224A RevB JTAG TCK Workaround.
         * Drive GPIO PIN high.
         */
	gpio_set_value(gpio, 1);
	for(cnt = 0; cnt < 10; cnt++) {
		/* Drive GPIO PIN low */
		gpio_set_value(gpio, 0);

		mdelay(10);
		/* Drive GPIO PIN  high */
		gpio_set_value(gpio, 1);
		mdelay(10);
	}
	dev_dbg(gul_dev->dev, "reset (gpio pin toggle) is done\n");

	/* Set gpio input direction */
	ret = gpio_direction_input(gpio);
	if (ret)
		dev_err(gul_dev->dev,
			"Failed to set input direct for gpio\n");

	gpio_free(gpio);

	return ret;
}
#endif /* ERRATA_A010867 */

int gul_base_probe(struct gul_dev *gul_dev)
{
	int i = 0, rc = 0, virq_count = 0, init_stage = 0;
	struct gul_sub_driver *subdrv;
	struct gul_sub_driver_ops *ops;
	struct gul_mem_region_info *iq_sample_region;
	u32 pci_abserr;
	struct gul_mem_region_info *ccsr_region;
	struct gul_hif *hif = NULL;
	struct tti_priv *tti_priv;
	u64 start_time, end_time, diff;

#if defined (LA1224)
	char board_ver;
#endif
	start_time = get_jiffies_64();
#if 0
	struct virq_evt_map subdrv_virqmap[IRQ_REAL_MSI_BIT];
	struct virq_evt_map *subdrv_virqmap_ptr;
#endif
	gul_dev->stats_desc.stats_control = GUL_STATS_DEFAULT_ENABLE_MASK;
#if 0
	rc = gul_init_dma_buf(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev,
				"Failed to init DMA buf, err %d", rc);
		return rc;
	}
#endif
	init_stage = GUL_SCRATCH_DMA_INIT_STAGE;
	rc = gul_scratch_buf_init(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev,
			"Failed to init DMA buf for outbound, err%d\n", rc);
		goto out;
	}
	allocate_scratch_buf_regions(gul_dev);
	g_gul_global[gul_dev->id].share_buf_size = modem_share_buf_size;

	rc = tti_dev_start(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "Failed to start tti, err %d", rc);
		goto out;
	}
	tti_priv = &tti_dev_data[gul_dev->id][0].tti_dev->tti_priv_t[0];

	rc = gul_load_rtos_img(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "Failed to load Modem image, err %d", rc);
		goto out;
	}

	init_stage = GUL_HANDSHAKE_INIT_STAGE;
	dev_dbg(gul_dev->dev, "%s: Initiating Reset handshake\n",
				gul_dev->name);
	rc = gul_do_reset_handshake(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "Reset handshake failed, err %d", rc);
		goto out;
	}
	g_gul_global[gul_dev->id].e200_load_status = 1;

	rc = gul_init_hif(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "gul_init_hif failed, err %d", rc);
		goto out;
	}

	hif = gul_dev->hif;
	g_gul_global[gul_dev->id].soc_version = gul_ep_get_svr();

	if (warmup_flag[gul_dev->id]) {
		writel(warmup_temp, &hif->warmup_info.warmup_temp);
		writel(warmup_timeout, &hif->warmup_info.warmup_timeout);
		writel(warmup_poll_intvl, &hif->warmup_info.warmup_poll_intvl);
	}

	writel(modem_host_uart, &gul_dev->hif->modem_host_uart);
	writel(disable_sideband, &gul_dev->hif->disable_sideband);

	if (disable_sideband)
		writel(tti_priv->msi_index, &gul_dev->hif->msi_tti);

#if defined (LA1224)
	board_ver = gul_get_host_board_rev();
	dev_info(gul_dev->dev,
		"NXP Layerscape LA1224-RDB Rev-%c (SVR:0x%x)-%s\n",
		board_ver, g_gul_global[gul_dev->id].soc_version,
		((g_gul_global[gul_dev->id].soc_version &  GEUL_SVR_REVB_VAL) == GEUL_SVR_REVB_VAL) ? "B0" : "A0");

	switch (board_ver) {
	case 'A':
		dev_dbg(gul_dev->dev, "%s: GEUL_HOST_REVA_VAL\n",
					gul_dev->name);
		writel(GEUL_HOST_REVA_VAL, &gul_dev->hif->host_board_rev);
		break;
	case 'B':
		dev_dbg(gul_dev->dev, "%s: GEUL_HOST_REVB_VAL\n",
					gul_dev->name);
		writel(GEUL_HOST_REVB_VAL, &gul_dev->hif->host_board_rev);
		break;
	case 'C':
		dev_dbg(gul_dev->dev, "%s: GEUL_HOST_REVC_VAL\n",
					gul_dev->name);
		writel(GEUL_HOST_REVC_VAL, &gul_dev->hif->host_board_rev);
		break;
	default:
		dev_dbg(gul_dev->dev, "%s: GEUL_HOST_UNKNOWN_VAL\n",
					gul_dev->name);
		writel(GEUL_HOST_UNKNOWN_VAL, &gul_dev->hif->host_board_rev);
		break;
	}
#endif

	gul_init_ep_logger(gul_dev);

	init_stage = GUL_SYSFS_INIT_STAGE;

	rc = gul_init_sysfs(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "gul_init_sysfs failed, err %d", rc);
		goto out;
	}

	gul_stats_init(gul_dev);

	rc = gul_register_ep_stats_ops(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "gul_register_ep_stats_ops, err %d", rc);
		goto out;
	}
	init_stage = GUL_IRQ_INIT_STAGE;

	gul_create_outbound_msi(gul_dev);

	init_stage = GUL_SUBDRV_PROBE_STAGE;
	subdrv = gul_get_subdrv(i);
	ops = &subdrv->ops;
	virq_count = 0;
	dev_dbg(gul_dev->dev, "%s:Initiating sub-drivers\n", gul_dev->name);
	for (i = 0; i < gul_subdrv_cnt_g; i++) {
		subdrv = gul_get_subdrv(i);
		if (gul_warmup_subdrv_probe_check_fail(gul_dev,
					subdrv, gul_dev->id))
			continue;
		ops = &subdrv->ops;
		if (ops->probe) {
			dev_dbg(gul_dev->dev, "subdrv probe : %s\n",
					&subdrv->name[0]);
#if 0
			memset(&subdrv_virqmap[0], 0, sizeof(subdrv_virqmap));
			virq_count = gul_get_subdrv_virqmap(gul_dev, subdrv,
					&subdrv_virqmap[0],
					IRQ_REAL_MSI_BIT);
			if (virq_count)
				subdrv_virqmap_ptr = &subdrv_virqmap[0];
			else
				subdrv_virqmap_ptr = NULL;
			rc = ops->probe(gul_dev, virq_count,
					subdrv_virqmap_ptr);
#endif
			rc = ops->probe(gul_dev, virq_count, NULL);
			if (rc) {
				dev_err(gul_dev->dev,
					"%s: probe failed, err %d\n",
					&subdrv->name[0], rc);
				goto out;
			}
		}
	}

	/* Errata A-008822 */
	pci_abserr = PCIE_RHOM_DBI_BASE + PCIE_ABSERR;
	ccsr_region = &gul_dev->mem_regions[GUL_MEM_REGION_CCSR];
	writel(PCIE_ABSERR_SETTING, ccsr_region->vaddr + pci_abserr);

	wdog_set_modem_status(gul_dev->id, WDOG_MODEM_READY);
	SET_HIF_HOST_RDY(gul_dev->hif, HIF_HOST_PROBE_COMPLETE);
	if (!warmup_flag[gul_dev->id])
		gul_set_warmup_status(gul_dev->id, GUL_WARMUP_COMPLETE);

	wdog_set_pci_domain_nr(gul_dev->id, pci_domain_nr(gul_dev->pdev->bus));
/*XXX:TODO: Commented to resolve compilation errors */
#if 0
	rc = gul_wait_for_freertos_ipc_init(gul_dev->ipc_priv);
	if (rc) {
		pr_err("Rattler have not valid written IPC signature : %s\n",
		__func__);
		goto out;
	}
#endif
	if (modem_share_buf_size) {
		iq_sample_region = scratch_buf_allocator(gul_dev,
					GUL_SCRATCH_MODEM_SHARE,
					modem_share_buf_size);
		if (!iq_sample_region) {
			dev_err(gul_dev->dev, "MODEM SHARE AREA: Memory Alocation Failed\n");
			goto out;
		}
		iq_sample_region->type = GUL_SCRATCH_MODEM_SHARE;
		g_gul_global[gul_dev->id].share_buf_phys_addr =
			iq_sample_region->phys_addr;

		dev_info(gul_dev->dev,
			"MODEM SHARE AREA: Host: Virt 0x%px,Phys 0x%llx,IQ sample Mod: Phys 0x%llx, size 0x%lx\n",
			iq_sample_region->vaddr,
			gul_dev->scratch_allocator.host_phys - gul_dev->scratch_allocator.mod_phys + iq_sample_region->phys_addr,
			iq_sample_region->phys_addr,
			iq_sample_region->size);
	} else
		g_gul_global[gul_dev->id].share_buf_phys_addr = 0;

	end_time = get_jiffies_64();
	diff = (u64) (end_time - start_time);
	dev_info(gul_dev->dev, "Modem Id %d: Probed OK, %s  Time elasped %llu msec \n",
			gul_dev->id, __func__, (diff * 1000 / HZ));

out:
	if (rc)
		gul_base_deinit(gul_dev, init_stage, i);

	return rc;

}

static int gul_base_cleanup_subdrv(struct gul_dev *gul_dev, int drv_index)
{
	int i = 0, rc = 0;
	struct gul_sub_driver *subdrv;
	struct gul_sub_driver_ops *ops;

	dev_info(gul_dev->dev, "%s: Removing sub-drivers because of error\n",
		 gul_dev->name);
	for (i = drv_index; i >= 0; i--) {
		subdrv = gul_get_subdrv(i);
		if (gul_subdrv_check_fail(subdrv))
			continue;
		ops = &subdrv->ops;
		if (ops->remove) {
			dev_dbg(gul_dev->dev, "subdrv remove : %s\n",
				&subdrv->name[0]);

			rc = ops->remove(gul_dev);
			if (rc) {
				dev_err(gul_dev->dev,
					"%s: Remove failed err %d\n",
					&subdrv->name[0], rc);
				/*Other drivers to be removed so continue*/
			}
		}
	}
	return rc;
}

int gul_base_deinit(struct gul_dev *gul_dev, int stage, int drv_index)
{
	int count = 0;

	dev_info(gul_dev->dev, "%s: De-init LA12xx dev\n", gul_dev->name);

	switch (stage) {
	case GUL_SUBDRV_PROBE_STAGE:
		gul_base_cleanup_subdrv(gul_dev, drv_index);
		for (; count < GUL_MSI_MAX_CNT; count++) {
			if (gul_dev->irq[count].free == GUL_MSI_IRQ_BUSY) {
				free_irq(gul_dev->irq[count].irq_val, gul_dev);
				gul_dev->irq[count].free = GUL_MSI_IRQ_FREE;
			}
		}
		__attribute__((__fallthrough__));
	/*Fallthrough*/
	case GUL_SYSFS_INIT_STAGE:
		gul_stats_exit(gul_dev);
		gul_remove_sysfs(gul_dev);
		__attribute__((__fallthrough__));
	/*Fallthrough*/
	case GUL_IRQ_INIT_STAGE:
	/*Fallthrough*/
	case GUL_HANDSHAKE_INIT_STAGE:
	/*Fallthrough*/
	case GUL_SCRATCH_DMA_INIT_STAGE:
		gul_modinfo_exit(gul_dev);
		memunmap(gul_dev->scratch_allocator.host_vaddr);
		tti_dev_stop(gul_dev);
		__attribute__((__fallthrough__));
	/*Fallthrough*/
	case GUL_DMA_INIT_STAGE:
		gul_free_dma_buf(gul_dev);
	}
	return 0;
}

int gul_base_remove(struct gul_dev *gul_dev)
{
	int count = 0;

	dev_info(gul_dev->dev, "%s: Removing LA12xx dev\n", gul_dev->name);

	gul_modinfo_exit(gul_dev);
	memunmap(gul_dev->scratch_allocator.host_vaddr);

	gul_subdrv_remove(gul_dev);

	if (disable_sideband)
	gul_dev_put_msi(gul_dev,gul_dev->hif->msi_tti);

	for (; count < GUL_MSI_MAX_CNT; count++) {
		if (gul_dev->irq[count].free == GUL_MSI_IRQ_BUSY && count != gul_dev->hif->msi_tti) {
			free_irq(gul_dev->irq[count].irq_val, gul_dev);
			gul_dev->irq[count].free = GUL_MSI_IRQ_FREE;
		}
	}
	dev_dbg(gul_dev->dev, "%s: Removing sub-drivers\n", gul_dev->name);

	gul_remove_sysfs(gul_dev);

	gul_free_dma_buf(gul_dev);

	gul_stats_exit(gul_dev);

	tti_dev_stop(gul_dev);

	g_gul_global[gul_dev->id].e200_load_status = 0;

	return 0;
}


void gul_subdrv_mod_exit(void)
{
	int i, rc = 0;
	struct gul_sub_driver *subdrv;
	struct gul_sub_driver_ops *ops;

	for (i = gul_subdrv_cnt_g; i > 0; i--) {
		subdrv = gul_get_subdrv(i-1);
		if (gul_subdrv_check_fail(subdrv))
			continue;
		ops = &subdrv->ops;
		if (ops->mod_exit) {
			pr_debug("%s: subdrv mod exit : %s\n",
					__func__, &subdrv->name[0]);
			rc = ops->mod_exit();
			if (rc) {
				pr_err("%s: %s: mod exit failed, err %d\n",
					__func__, &subdrv->name[0], rc);
			}
		}
	}

}

void gul_subdrv_remove(struct gul_dev *gul_dev)
{
	int i, rc = 0;
	struct gul_sub_driver *subdrv;
	struct gul_sub_driver_ops *ops;

	for (i = 0; i < gul_subdrv_cnt_g; i++) {
		subdrv = gul_get_subdrv(i);
		if (gul_subdrv_check_fail(subdrv))
			continue;
		ops = &subdrv->ops;
		if (ops->remove) {
			dev_dbg(gul_dev->dev, "[%s] subdrv remove : %s\n",
					gul_dev->name,
					&subdrv->name[0]);
			rc = ops->remove(gul_dev);
			if (rc) {
				dev_err(gul_dev->dev,
					"%s:[%s] mod exit failed, err %d\n",
					gul_dev->name,
					&subdrv->name[0], rc);
			}
		}
	}

}

/* Dummy functions to check VSPA/IPC probe/remove invocation from the sub-driver
 * probe/remove. These functions are defined weak so they will be over-ridden
 * when real guys arive in arena.
 */
int  __attribute__((weak)) vspa_probe(struct gul_dev *gul_dev, int virq_count,
		struct virq_evt_map *virq_map)
{
	dev_dbg(gul_dev->dev, "[%s]Dummy VSPA probe\n", gul_dev->name);
	return 0;
}

int  __attribute__((weak)) vspa_remove(struct gul_dev *gul_dev)
{
	dev_dbg(gul_dev->dev, "[%s]Dummy VSPA remove\n", gul_dev->name);
	return 0;
}

int  __attribute__((weak)) gul_ipc_probe(struct gul_dev *gul_dev,
		int virq_count,	struct virq_evt_map *virq_map)
{
	dev_dbg(gul_dev->dev, "[%s]Dummy IPC probe\n", gul_dev->name);
	return 0;
}

int  __attribute__((weak)) gul_ipc_remove(struct gul_dev *gul_dev)
{
	dev_dbg(gul_dev->dev, "[%s]Dummy IPC remove\n", gul_dev->name);
	return 0;
}

int  __attribute__((weak)) gul_ipc_init(void)
{
	printk(KERN_DEBUG "Dummy IPC init\n");
	return 0;
}

int  __attribute__((weak)) gul_ipc_exit(void)
{
	printk(KERN_DEBUG "Dummy IPC exit\n");
	return 0;
}

int  __attribute__((weak)) wdog_probe(struct gul_dev *gul_dev, int virq_count,
		struct virq_evt_map *virq_map)
{
	dev_dbg(gul_dev->dev, "[%s]Dummy WDOG probe\n", gul_dev->name);
	return 0;
}

int  __attribute__((weak)) wdog_init(void)
{
	printk(KERN_DEBUG "Dummy WDOG init\n");
	return 0;
}

int  __attribute__((weak)) wdog_exit(void)
{
	printk(KERN_DEBUG "Dummy WDOG exit\n");
	return 0;
}

int  __attribute__((weak)) tvd_init(void)
{
	printk(KERN_DEBUG "Dummy TVD init\n");
	return 0;
}

int  __attribute__((weak)) tvd_exit(void)
{
	printk(KERN_DEBUG "Dummy TVD exit\n");
	return 0;
}

int  __attribute__((weak)) tvd_probe(struct gul_dev *gul_dev,
		int virq_count, struct virq_evt_map *virq_map)
{
	dev_dbg(gul_dev->dev, "[%s]Dummy TVD probe\n", gul_dev->name);
	return 0;
}

int  __attribute__((weak)) tvd_remove(struct gul_dev *gul_dev)
{
	dev_dbg(gul_dev->dev, "[%s]Dummy TVD remove\n", gul_dev->name);
	return 0;
}
int  __attribute__((weak)) cli_init(void)
{
	printk(KERN_DEBUG "Cli init\n");
	return 0;
}

int  __attribute__((weak)) cli_exit(void)
{
	printk(KERN_DEBUG "Cli exit\n");
	return 0;
}

int  __attribute__((weak)) cli_probe(struct gul_dev *gul_dev,
					    int virq_count,
					    struct virq_evt_map *virq_map)
{
	dev_dbg(gul_dev->dev, "[%s]Dummy Cli probe\n", gul_dev->name);
	return 0;
}

int  __attribute__((weak)) cli_remove(struct gul_dev *gul_dev)
{
	dev_dbg(gul_dev->dev, "[%s]Dummy Cli remove\n", gul_dev->name);
	return 0;
}

/* Sub driver initializer table
 * Add the subdrivers in the order of desired initiazation sequence
 * NOTE: IPC ALWAYS HAS TO BE THE FIRST SUB-DRIVER
 */
static struct gul_sub_driver sub_drvs_g[] = {
	{ .name = "WDOG",
	  .type = GUL_SUBDRV_TYPE_WDOG,
		{
			.probe = wdog_probe,
			.mod_init = wdog_init,
			.mod_exit = wdog_exit,
		},
	},
	{ .name = "TVD",
	  .type = GUL_SUBDRV_TYPE_TVD,
		{
			.probe = tvd_probe,
			.remove = tvd_remove,
			.mod_init = tvd_init,
			.mod_exit = tvd_exit,

		},
	},
#ifdef DCS_DRVR_ENABLED
	{ .name = "DCS_HS",
	  .type = GUL_SUBDRV_TYPE_DCS,
	{
		.probe = dcs_probe,
		.remove = dcs_remove,
		.mod_init = dcs_init,
		.mod_exit = dcs_exit,
	  },
	},
#endif
	{ .name = "VSPA",
	  .type = GUL_SUBDRV_TYPE_VSPA,
		{
			.probe = vspa_probe,
			.remove = vspa_remove,
		},
	},
	{ .name = "IPC",
	  .type = GUL_SUBDRV_TYPE_IPC,
		{
			.probe = gul_ipc_probe,
			.remove = gul_ipc_remove,
			.mod_init = gul_ipc_init,
			.mod_exit = gul_ipc_exit,
		},
	},

#ifdef RF_DRVR_ENABLED
	{ .name = "RFIC",
	  .type = GUL_SUBDRV_TYPE_RFIC,
		{
			.probe = gul_rfic_probe,
			.remove = gul_rfic_remove,
			.mod_init = gul_rfic_init,
			.mod_exit = gul_rfic_exit,
		},
	},

#endif

	{ .name = "CLI",
	  .type = GUL_SUBDRV_TYPE_CLI,
		{
			.probe = cli_probe,
			.remove = cli_remove,
			.mod_init = cli_init,
			.mod_exit = cli_exit,
		},
	},
#ifdef HAWK_DRVR_ENABLED
	{ .name = "HAWK",
	  .type = GUL_SUBDRV_TYPE_HAWK,
		{
			.probe = gul_hawk_probe,
			.remove = gul_hawk_remove,
		},
	},

#endif

#ifdef YUCCA_RF_DRVR_ENABLED
	{ .name = "YUCCA_RFIC",
	  .type = GUL_SUBDRV_TYPE_RFIC,
		{
			.probe = gul_yucca_rfic_probe,
			.remove = gul_yucca_rfic_remove,
		},
	},

#endif

#ifdef USIM_DRVR_ENABLED
	{ .name = "USIM",
	  .type = GUL_SUBDRV_TYPE_USIM,
		{
			.probe = gul_usim_probe,
			.remove = gul_usim_remove,
		},
	},
#endif

	{
	}

};

static struct gul_sub_driver *gul_get_subdrv(int i)
{
	return &sub_drvs_g[i];
}

extern int gul_get_msi_irq(struct gul_dev *gul_dev, enum gul_msi_id type)
{
	dev_dbg(gul_dev->dev, "return irq=%d\n", gul_dev->irq[type].irq_val);
	return gul_dev->irq[type].irq_val;
}

int gul_subdrv_mod_init(void)
{
	int i, rc = 0;
	struct gul_sub_driver *subdrv;
	struct gul_sub_driver_ops *ops;

	gul_subdrv_cnt_g = ARRAY_SIZE(sub_drvs_g);

	for (i = 0; i < gul_subdrv_cnt_g; i++) {
		subdrv = gul_get_subdrv(i);
		if (gul_subdrv_check_fail(subdrv))
			continue;
		ops = &subdrv->ops;
		if (ops->mod_init) {
			pr_debug("%s: subdrv mod init : %s\n",
					__func__, &subdrv->name[0]);
			rc = ops->mod_init();
			if (rc) {
				pr_err("%s: %s: mod init failed, err %d\n",
						__func__, &subdrv->name[0], rc);
				goto out;
			}
		}
	}

out:
	return rc;
}

/*
 * Initializes PCIe outbound window attributes in gul_dev.
 * Note: Maximum PCIe outbound window size can be of 1 GB.
 */
void gul_init_ep_pcie_allocator(struct gul_dev *gul_dev)
{
	gul_dev->pci_outbound_win_start_addr = PCI_OUTBOUND_WINDOW_BASE_ADDR;
	gul_dev->pci_outbound_win_current_addr = PCI_OUTBOUND_WINDOW_BASE_ADDR;
	gul_dev->pci_outbound_win_limit = 0xE0000000;
}

uint32_t gul_alloc_ep_pcie_addr(struct gul_dev *gul_dev, uint32_t window_size)
{
	uint32_t return_address = gul_dev->pci_outbound_win_current_addr;

	if ((return_address + window_size) > gul_dev->pci_outbound_win_limit) {
		dev_err(gul_dev->dev,
			"Windows size is exceeding total PCIe memory\n");
		return -1;
	}
	gul_dev->pci_outbound_win_current_addr = return_address +
		window_size;

	return return_address;
}


int gul_raise_msgunit_irq(struct gul_dev *gul_dev,
			  int msg_unit_idx, int bit_num)
{
	struct gul_msg_unit *msg_unit;

	msg_unit = gul_dev->msg_units[msg_unit_idx];
	writel(bit_num, &msg_unit->msiir);

	return 0;
}
