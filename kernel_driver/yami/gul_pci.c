/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2025 NXP
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/fsl/guts.h>

#include "gul_base.h"
#include "gul_pci.h"
#include "gul_vspa.h"
#include "gul_modinfo.h"

static const char *driver_name = "Yami";
int global_scratch_buf_size;
int modem_share_buf_size = MODEM_SHARE_MODEM_SIZE;
EXPORT_SYMBOL(modem_share_buf_size);

uint64_t scratch_buf_phys_addr;
#if defined(LA1238RDB)
int rfic_disable;
#else
int rfic_disable = 1;
#endif
EXPORT_SYMBOL(rfic_disable);
int modem_rf_data_size = MODEM_SHARE_RF_SIZE;
EXPORT_SYMBOL(modem_rf_data_size);
int modem_host_data_size;
EXPORT_SYMBOL(modem_host_data_size);
int tvd_disable;
EXPORT_SYMBOL(tvd_disable);
int wdog_disable;
EXPORT_SYMBOL(wdog_disable);
#if defined(LA1238RDB)
int ls_dac_sps = LS_DAC_SPS_245;
#else
int ls_dac_sps = LS_DAC_SPS_491;
#endif
EXPORT_SYMBOL(ls_dac_sps);
int ls_adc_sps = LS_ADC_SPS_245;
EXPORT_SYMBOL(ls_adc_sps);
int lsdcs_disable;
EXPORT_SYMBOL(lsdcs_disable);
int hsdcs_sps = HSDCS_SPS_1966;
EXPORT_SYMBOL(hsdcs_sps);
int hsdcs_enable;
EXPORT_SYMBOL(hsdcs_enable);
int hsadc_mask = HS_ENABLE_ALL;
EXPORT_SYMBOL(hsadc_mask);
int hsdac_mask = HS_ENABLE_ALL;
EXPORT_SYMBOL(hsdac_mask);
int lsdac_mask = LS_ENABLE_ALL;
EXPORT_SYMBOL(lsdac_mask);
int lsadc_mask = LS_ENABLE_ALL;
EXPORT_SYMBOL(lsadc_mask);
int tbgen2_disable;
EXPORT_SYMBOL(tbgen2_disable);
int vspa_disable;
EXPORT_SYMBOL(vspa_disable);
char vspa_fw_name_prefix[FIRMWARE_NAME_SIZE] = VSPA_FW_NAME_PREFIX;
EXPORT_SYMBOL(vspa_fw_name_prefix);
char firmware_name[FIRMWARE_NAME_SIZE] = FIRMWARE_RTOS;
EXPORT_SYMBOL(firmware_name);
int pci_addr_count;
char *pci_addr_array[5];
EXPORT_SYMBOL(pci_addr_count);
EXPORT_SYMBOL(pci_addr_array);
int tti_per_dev = 2;
EXPORT_SYMBOL(tti_per_dev);
int cli_dmesg_on = 0;
EXPORT_SYMBOL(cli_dmesg_on);
int modem_host_uart = 0;
EXPORT_SYMBOL(modem_host_uart);
int disable_sideband = 0;
EXPORT_SYMBOL(disable_sideband);

uint32_t warmup_temp = DEFAULT_WARM_UP_TEMP;
uint32_t warmup_timeout = DEFAULT_WARM_UP_TIMEOUT_IN_SEC;
uint32_t warmup_poll_intvl = DEFAULT_WARM_UP_POLL_INTRL_VAL;
bool warmup_flag[MAX_MODEM];

static int modem_num = 1;
static uint32_t fact_id = -1;

LIST_HEAD(pcidev_list);
static int gul_dev_id_g;
char *gul_dev_name_prefix_g = "gul";
EXPORT_SYMBOL(gul_dev_name_prefix_g);
static struct class *gul_class;
static void gul_pcidev_remove(struct pci_dev *pdev);
struct gul_global g_gul_global[MAX_MODEM];
struct gul_modem_dev_id gul_dev_id[MAX_MODEM];

static const char *SOC[][6] = {
	{"LA1200", "HSDCS-NO", "LSDCS-NO", "VSPA-NO", "FECA", "PCI-EP,RC",},
	{"LA1201", "HSDCS-NO", "LSDCS-NO", "VSPA", "FECA", "PCI-EP,RC",},
	{"LA1212", "HSDCS-NO", "LSDCS-2T", "VSPA", "FECA-NO", "PCI-EP",},
	{"LA1214", "HSDCS-NO", "LSDCS-4T", "VSPA", "FECA-NO", "PCI-EP",},
	{"LA1215", "HSDCS-FULL", "LSDCS-NO", "VSPA", "FECA-NO", "PCI-EP",},
	{"LA1216", "HSDCS-RX", "LSDCS-2T", "VSPA", "FECA-NO", "PCI-EP",},
	{"LA1223", "HSDCS-NO", "LSDCS-4T", "VSPA", "FECA", "PCI-EP,RC",},
	{"LA1225", "HSDCS-NO", "LSDCS-NO", "VSPA", "FECA", "PCI-EP,RC",},
	{"LA1234", "HSDCS-NO", "LSDCS-4T", "VSPA", "FECA", "PCI-EP,RC",},
	{"LA1235", "HSDCS-FULL", "LSDCS-NO", "VSPA", "FECA", "PCI-EP,RC",},
	{"LA1236", "HSDCS-RX", "LSDCS-2T", "VSPA", "FECA", "PCI-EP,RC",},
	{"LA1218", "HSDCS-RX", "LSDCS-4T", "VSPA", "FECA-NO", "PCI-EP",},
	{"LA1238", "HSDCS-RX", "LSDCS-4T", "VSPA", "FECA", "PCI-EP,RC",},
	{"LA1232", "HSDCS-NO", "LSDCS-2T", "VSPA", "FECA", "PCI-EP,RC",},
	{"RESV", "RESV", "RESV", "RESV", "RESV", "RESV",},
	{"LA1224", "HSDCS-FULL", "LSDCS-4T", "VSPA", "FECA", "PCI-EP,RC",},
};

static const char *SOC_DCS[] = {
	"HSDCS_NO",
	"HSDCS_RXONLY",
	"HSDCS_FULL"
};

typedef enum {
	HSDCS_NO,
	HSDCS_RXONLY,
	HSDCS_FULL,
} soc_type_t;

#define SVR_LS1080A_FAMILY	0x87030000
#define SVR_LS2080A_FAMILY	0x87010000
#define SVR_LS2088A_FAMILY	0x87090000
#define SVR_LX2160A_FAMILY	0x87360000
#define SVR_LS1043A_FAMILY	0x87920000
#define SVR_LS1046A_FAMILY	0x87070000
#define SVR_FAMILY_MASK	0xffff0000

static unsigned int svr_family;
extern size_t get_qman_fqd_size(void);
extern dma_addr_t get_qman_fqd_addr(void);
extern size_t get_qman_pfdr_size(void);
extern dma_addr_t get_qman_pfdr_addr(void);
extern size_t get_bman_fbpr_size(void);
dma_addr_t get_bman_fbpr_addr(void);

uint32_t get_hsdcs_support(uint32_t rev)
{
	switch (rev & GEUL_SVR_HSDCS_MASK) {
	case GEUL_SVR_HSDCS_NO:
		return HSDCS_NO;
	case GEUL_SVR_HSDCS_RXONLY:
		return HSDCS_RXONLY;
	default:
		return HSDCS_FULL;
	}
	return HSDCS_FULL;
}

static void get_factory_unique_id( void )
{
	void __iomem *virt_addr = ioremap((uint32_t)FACT_UNIQUE_ID, 2);
	fact_id = ioread32(virt_addr);
	iounmap(virt_addr);
}

static int
get_gul_dev_id_pcidevname(struct device *dev)
{
	int i;

	for (i = 0; i < MAX_MODEM; i++) {
		if (!strcmp(dev_name(dev), g_gul_global[i].dev_name)) {
			pr_info("device matched %s at Id %d\n",
				dev_name(dev), i);
			return i;
		}
	}
	/* if pci_addr_count/pci_addr_array is there and we are not able to find the matching
	dev_name, don't allow the modem to be probed */
	if (pci_addr_count && i == MAX_MODEM) {
		dev_info(dev,
		"Probing %s not allowed in pci_addr_array\n", dev_name(dev));
		return -1;
	}

	/*pci_addr_array is must for 2nd modem. Don't allow the creation.
	it can be problematic. */
	/* gul_dev_id_g will be 0 for 1st modem. and during watchdog recreation
	 the dev_name will be present - so this leg will not be hit. */
	if (!pci_addr_count && (gul_dev_id_g >= 1)) {
		dev_err(dev,
		"**IMPORTANT*: pci_addr_array is a must input for more than 1 modem\n");
		return -1;
	}
	if (gul_dev_id_g >= MAX_MODEM) {
		dev_err(dev, "exceeding max permitted (%d) gul devices!\n",
			MAX_MODEM);
		return -1;
	}
	return gul_dev_id_g++;
}

static inline void __hexdump(unsigned long start, unsigned long end,
			     unsigned long p, size_t sz, const unsigned char *c)
{
	while (start < end) {
		unsigned int pos = 0;
		char buf[64];
		int nl = 0;

		pos += sprintf(buf + pos, "%08lx: ", start);
		do {
			if ((start < p) || (start >= (p + sz)))
				pos += sprintf(buf + pos, "..");
			else
				pos += sprintf(buf + pos, "%02x", *(c++));
			if (!(++start & 15)) {
				buf[pos++] = '\n';
				nl = 1;
			} else {
				nl = 0;
			if (!(start & 1))
				buf[pos++] = ' ';
			if (!(start & 3))
				buf[pos++] = ' ';
			}
		} while (start & 15);
		if (!nl)
			buf[pos++] = '\n';
		buf[pos] = '\0';
		pr_info("%s", buf);
	}
}

void gul_hexdump(const void *ptr, size_t sz)
{
	unsigned long p = (unsigned long)ptr;
	unsigned long start = p & ~15UL;
	unsigned long end = (p + sz + 15) & ~15UL;
	const unsigned char *c = ptr;

	__hexdump(start, end, p, sz, c);
}
EXPORT_SYMBOL_GPL(gul_hexdump);

static char *
get_host_svr_name(int value)
{
	switch (value) {
		case SVR_LX2160A_FAMILY: return "LX2160A_FAMILY";
		case SVR_LS1043A_FAMILY: return "LS1043A_FAMILY";
		case SVR_LS1046A_FAMILY: return "LS1046A_FAMILY";
		case SVR_LS1080A_FAMILY: return "LS1080A_FAMILY";
		case SVR_LS2080A_FAMILY: return "LS2080A_FAMILY";
		case SVR_LS2088A_FAMILY: return "LS2088A_FAMILY";
	}
	return "Unknown";
}

struct gul_dev *get_gul_dev_byname(const char *name)
{
	struct list_head *ptr;
	struct gul_dev *dev = NULL;

	list_for_each(ptr, &pcidev_list) {
		dev = list_entry(ptr, struct gul_dev, list);
		if (!strcmp(dev->name, name)) {
			pr_debug("%s gul_dev 0x%px\n", __func__, dev);
			return dev;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(get_gul_dev_byname);

static int pci_current_link_speed(struct pci_dev *pci_dev)
{
	u16 linkstat;
	int err;

	err = pcie_capability_read_word(pci_dev, PCI_EXP_LNKSTA, &linkstat);
	if (err)
		return -EINVAL;

	return linkstat;
}

#define PCI_EXP_SPEED2STR(speed) \
	((speed) == PCI_EXP_LNKSTA_CLS_16_0GB ? "16 GT/s" : \
	 (speed) == PCI_EXP_LNKSTA_CLS_8_0GB ? "8 GT/s" : \
	 (speed) == PCI_EXP_LNKSTA_CLS_5_0GB ? "5 GT/s" : \
	 (speed) == PCI_EXP_LNKSTA_CLS_2_5GB ? "2.5 GT/s" : \
	 "Unknown speed")

static const char *scratch_buf_id_str[] = {
	"MODEM_SHARE",
	"HOST_DATA",
	"RF_DATA",
	"FIRMWARE",
	"SCRATCH_DBG_LOGGER",
	"VSPA_FW",
	"VSPA_OVERLAY",
	"SCRATCH_END",
};

ssize_t gul_device_dump(struct gul_dev *dev, char *buf)
{
	int i = 0, scr_i, fuse;
	u16 linkstat, linkcap;
	struct gul_hif *hif = dev->hif;
	struct vspa_device **max_vspadev = NULL;
	modinfo_t mi;

	fuse = get_fuse_val(dev);

	sprintf(&buf[strlen(buf)],
		" Modem ID=%d, Name:%s PCI-ID:%x, PCI_ADDR:%s\n",
		dev->id, dev->name, dev->pdev->device, g_gul_global[dev->id].dev_name);

	sprintf(&buf[strlen(buf)], " ARM HOST SVR(0x%x)-Unique-ID=0x%x\n",
			svr_family, fact_id);

	linkstat = pci_current_link_speed(dev->pdev);
	sprintf(&buf[strlen(buf)],
		" LA12XX PCI (Gen3) Link Width:X%x Current:X%x(%s)\n",
		pcie_get_width_cap(dev->pdev),
		((linkstat & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT),
		PCI_EXP_SPEED2STR(linkstat & PCI_EXP_LNKSTA_CLS));

	if (hif->pcie2_link) {
		linkcap = readl(dev->mem_regions[GUL_MEM_REGION_CCSR].vaddr + get_pci_linkcap(1));
		linkstat = (readl(dev->mem_regions[GUL_MEM_REGION_CCSR].vaddr + get_pci_curr_linkstat(1)) >> 16);
		sprintf(&buf[strlen(buf)],
			 " LA12XX PCI-2 (Gen3) Link Width:X%x Current:X%x(%s)\n",
			((linkcap & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT),
			((linkstat & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT),
			PCI_EXP_SPEED2STR(linkstat & PCI_EXP_LNKSTA_CLS));
	} else
		sprintf(&buf[strlen(buf)], " LA12XX PCI-2 (Gen3): Not enabled\n");

	if (fuse == FUSE_INVALID)
		sprintf(&buf[strlen(buf)], " Ver: 0x%x, Fuse: 0x0!!!", gul_ep_get_svr());
	else
		sprintf(&buf[strlen(buf)], " Ver: 0x%x[%s:%s], Fuse: 0x%x [%s] %s %s\n",
		gul_ep_get_svr(), SOC[fuse & 0xF][0],
		((g_gul_global[dev->id].soc_version &  GEUL_SVR_REVB_VAL) == GEUL_SVR_REVB_VAL) ? "B0" : "A0",
		fuse,
		SOC_DCS[get_hsdcs_support(gul_ep_get_svr())],
		SOC[fuse & 0xF][2], SOC[fuse & 0xF][4]);


	sprintf(&buf[strlen(buf)], " e200: (%s)\n", dev->fw_name);
	if (!vspa_disable) {
		max_vspadev = (struct vspa_device **)dev->vspa_priv;
		sprintf(&buf[strlen(buf)], " VSPA: (%s)\n", max_vspadev[0]->eld_filename);
	}

	sprintf(&buf[strlen(buf)], " DCS(LS): %s",
		g_gul_global[dev->id].lsdcs_load_status ? "ON" : "OFF");
	if (g_gul_global[dev->id].lsdcs_load_status) {
		sprintf(&buf[strlen(buf)],
			" ADC SPS=%d, DAC SPS=%d, lsadc_mask 0x%x, lsdac_mask 0x%x\n",
			ls_adc_sps, ls_dac_sps, lsadc_mask, lsdac_mask);
	}

	sprintf(&buf[strlen(buf)], " DCS(HS): %s",
		g_gul_global[dev->id].hsdcs_load_status ? "ON" : "OFF");
	if (g_gul_global[dev->id].hsdcs_load_status) {
		sprintf(&buf[strlen(buf)],
			" ADC & DAC SPS=%d, hsadc_mask 0x%x, hsdac_mask 0x%x\n",
			hif->hsdcs_sps, hsadc_mask, hsdac_mask);
	}

	gul_modinfo_get(dev, &mi);

	sprintf(&buf[strlen(buf)], " TBGen1 Freq = %u KHz (%s)\n", mi.clk_info.tbgen1_freq,
		mi.clk_info.tbgen1_src);
	sprintf(&buf[strlen(buf)], " TBGen2 Freq = %u KHz (%s)\n", mi.clk_info.tbgen2_freq,
		mi.clk_info.tbgen2_src);
	if (disable_sideband)
		sprintf(&buf[strlen(buf)], "Sideband signals disabled\n");
	else
		sprintf(&buf[strlen(buf)], "Sideband signals enabled\n");

	sprintf(&buf[strlen(buf)], "\n Scratch Buf:Used = %lluMB, Total = %ldMB\n",
		IN_MB(dev->scratch_allocator.mod_phys_current
			- dev->scratch_allocator.mod_phys),
		IN_MB(dev->scratch_allocator.size));

	sprintf(&buf[strlen(buf)], " (CCSR) BAR:%d  addr:0x%llx len:0x%llx\n",
			i, dev->mem_regions[GUL_MEM_REGION_CCSR].phys_addr,
			(u64)dev->mem_regions[GUL_MEM_REGION_CCSR].size);
	sprintf(&buf[strlen(buf)], " (DCSR) BAR:%d  addr:0x%llx len:0x%llx\n",
			i, dev->mem_regions[GUL_MEM_REGION_DCSR].phys_addr,
			(u64)dev->mem_regions[GUL_MEM_REGION_DCSR].size);
	sprintf(&buf[strlen(buf)], " (PEBM) BAR:%d  addr:0x%llx len:0x%llx\n",
			i, dev->mem_regions[GUL_MEM_REGION_PEBM].phys_addr,
			(u64)dev->mem_regions[GUL_MEM_REGION_PEBM].size);
	sprintf(&buf[strlen(buf)], " (FECA) BAR:%d  addr:0x%llx len:0x%llx\n",
			i, dev->mem_regions[GUL_MEM_REGION_FECA].phys_addr,
			(u64)dev->mem_regions[GUL_MEM_REGION_FECA].size);
	sprintf(&buf[strlen(buf)], " (HIF) Start:  addr:0x%llx len:0x%x\n",
			mi.hif.host_phy_addr, mi.hif.size);
	if(mi.modem_host_uart)
		sprintf(&buf[strlen(buf)], " Modem log to Host Status: Enable\n");
	else
		sprintf(&buf[strlen(buf)], " Modem log to Host Status: Disable\n");

	sprintf(&buf[strlen(buf)], "\n Scratch:  Phy addr:0x%llx Size:0x%llx (%lldMB)\n",
			g_gul_global[dev->id].scratch_buf_phys_addr,
			(u64)g_gul_global[dev->id].scratch_buf_size,
			IN_MB((u64)g_gul_global[dev->id].scratch_buf_size));
	sprintf(&buf[strlen(buf)],
		" %s| %s|%s| %s\n", "Region", "Host Phy Addr", "Modem Phy Addr", "Size");
	for (scr_i = 0; scr_i < GUL_SCRATCH_END; scr_i++) {
		sprintf(&buf[strlen(buf)], " %5d | 0x%llx |   0x%x | 0x%x (%dMB) %s\n",
			scr_i, mi.scratchregions[scr_i].host_phy_addr,
			mi.scratchregions[scr_i].modem_phy_addr,
			mi.scratchregions[scr_i].size,
			IN_MB(mi.scratchregions[scr_i].size),
			scratch_buf_id_str[scr_i]);
	}
	sprintf(&buf[strlen(buf)], "\n IPC:%s%s\n",
			GUL_IPC_DEVNAME_PREFIX, dev->name);
	/* The log buf should not go beyond 4K size, in case of 4 modems it may cross
		the PAGE_SIZE, so reducing the output when using 4 or more modems*/
	if (modem_num < 4)
		tti_device_dump(dev->id, buf);
	tvd_device_dump(dev->id, buf);
	sprintf(&buf[strlen(buf)], "\n %s", "-*-*-*-");
	sprintf(&buf[strlen(buf)], "%ld\n", strlen(buf));

	return strlen(buf);
}

ssize_t gul_show_global_status(char *buf)
{
	struct list_head *ptr;
	struct gul_dev *dev = NULL;

	sprintf(buf, "*%s\n", VERSION);
#ifdef LA1224
	if (disable_sideband)
		sprintf(&buf[strlen(buf)], " DAF ? -%c\n",
			gul_get_host_board_rev());
	else
		sprintf(&buf[strlen(buf)], " NXP LA1224-RDB Rev-%c\n",
			gul_get_host_board_rev());

#endif

	sprintf(&buf[strlen(buf)], " No. of LA12xx Devices Detected = %d\n",
		gul_dev_id_g);

	list_for_each(ptr, &pcidev_list) {
		dev = list_entry(ptr, struct gul_dev, list);
		if (g_gul_global[dev->id].active)
			gul_device_dump(dev, buf);
			/* if buffer has already crossed 4K or if number of modems are more than 2
			 * and buffer is already around 3K, better to stop.*/
		if (strlen(buf) > PAGE_SIZE || ((modem_num > 2) && strlen(buf) > 3000))
			break;
	}

	sprintf(&buf[strlen(buf)], " %s", "\n");

	return strlen(buf) > PAGE_SIZE ? PAGE_SIZE : strlen(buf);
}
EXPORT_SYMBOL_GPL(gul_show_global_status);

void gul_dev_reset_interrupt_capability(struct gul_dev *gul_dev)
{
	if (GUL_CHK_FLG(gul_dev->flags, GUL_FLG_PCI_MSI_EN)) {
		pci_disable_msi(gul_dev->pdev);
		GUL_CLR_FLG(gul_dev->flags, GUL_FLG_PCI_MSI_EN);
	}
}

void enable_all_msi(struct gul_dev *gul_dev)
{
	u32 __iomem *pcie_vaddr, *pcie_msi_control;
	u32 val;

	pcie_vaddr = (u32 *)(gul_dev->mem_regions[GUL_MEM_REGION_CCSR]
				.vaddr + PCIE_RHOM_DBI_BASE
				+ PCIE_MSI_BASE);

	val = ioread32(pcie_vaddr);
	dev_dbg(gul_dev->dev, "MSI Capability: Control Reg. -> value = %x\n",
				val);
	pcie_msi_control = (u32 *)(gul_dev->mem_regions[GUL_MEM_REGION_CCSR]
				.vaddr + PCIE_RHOM_DBI_BASE
				+ PCIE_MSI_CONTROL);

	iowrite8(0xb6, pcie_msi_control);
	val = ioread32(pcie_vaddr);
	dev_dbg(gul_dev->dev, "MSI Capability: Control Reg. -> value = %x\n",
				val);
}

int gul_dev_get_msi(struct gul_dev *gul_dev)
{
	int index, found = 0;
	int core_id = smp_processor_id();

	dev_dbg(gul_dev->dev, "requested core_id %d\n", core_id);
	for (index = 0; index < GUL_MSI_MAX_CNT; index++) {
		if (gul_dev->irq[index].free == GUL_MSI_IRQ_FREE && gul_dev->irq[index].core_id == core_id) {
			found = 1;
			break;
		}
	}
	if (!found) {
		for (index = 0; index < GUL_MSI_MAX_CNT; index++) {
			if (gul_dev->irq[index].free == GUL_MSI_IRQ_FREE) {
				found = 1;
				break;
			}
		}
	}

	if (found) {
		gul_dev->irq[index].free = GUL_MSI_IRQ_BUSY;
		dev_dbg(gul_dev->dev, "irq_val %d, assigned core_id %d\n",
			gul_dev->irq[index].irq_val, gul_dev->irq[index].core_id);
		return index;
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(gul_dev_get_msi);

void gul_dev_put_msi(struct gul_dev *gul_dev, int index)
{
	if (index >= 0 && index < GUL_MSI_MAX_CNT) {
		if (gul_dev->irq[index].free == GUL_MSI_IRQ_BUSY) {
			gul_dev->irq[index].free = GUL_MSI_IRQ_FREE;
		} else
			dev_err(gul_dev->dev, "%s: MSI already free\n", __func__);
	}
}
EXPORT_SYMBOL_GPL(gul_dev_put_msi);

/*
 * gul_dev_set_interrupt_capability - set MSI or MSI-X if supported
 *
 * Attempt to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 */
int gul_dev_set_interrupt_capability(struct gul_dev *gul_dev, int mode)
{
	int ret = 0, i = 0;
	struct gul_mem_region_info *ccsr_region;

	/* Check whether the device has MSIx cap */
	switch (mode) {
	case PCI_INT_MODE_MULTIPLE_MSI:
		enable_all_msi(gul_dev);
		ret = pci_alloc_irq_vectors_affinity(gul_dev->pdev,
				MIN_MSI_ITR_LINES,
				GUL_MSI_MAX_CNT,
				PCI_IRQ_MSI, NULL);
		/* Deprecated function
		 *  ret = pci_enable_msi_range(gul_dev->pdev,
		 *  MIN_MSI_ITR_LINES,
		 *  GUL_MSI_MAX_CNT);
		 */

		if (ret < GUL_MSI_MAX_CNT) {
			dev_err(gul_dev->dev,
				"Cannot complete request for multiple MSI");
			goto msi_error;
		} else {
			dev_info(gul_dev->dev,
				 "%d MSI successfully created\n", ret);
		}
		GUL_SET_FLG(gul_dev->flags, GUL_FLG_PCI_MSI_EN);

		ccsr_region = &gul_dev->mem_regions[GUL_MEM_REGION_CCSR];

		dev_info(gul_dev->dev, "CCSR: vaddr %px, size %d\n",
			 ccsr_region->vaddr, (int)ccsr_region->size);

		/* out bound for MSI */
		for (i = 0; i < GUL_MSI_MAX_CNT; i++) {
			gul_dev->irq[i].msi_val = i;
			gul_dev->irq[i].irq_val = (gul_dev->pdev->irq + i);
			gul_dev->irq[i].free = GUL_MSI_IRQ_FREE;
			gul_dev->irq[i].core_id = i % num_possible_cpus();
			dev_dbg(gul_dev->dev, "irq_val %d, core_id %d\n", gul_dev->irq[i].irq_val, gul_dev->irq[i].core_id);
		}

		gul_dev->irq_count = GUL_MSI_MAX_CNT;
		break;

	case PCI_INT_MODE_MSIX:
		dev_warn(gul_dev->dev,
			"Unable to support MSIX for GUL\n");
		__attribute__((__fallthrough__));
		/* Fall through */
	case PCI_INT_MODE_MSI:
		if (!pci_enable_msi(gul_dev->pdev)) {
			GUL_SET_FLG(gul_dev->flags, GUL_FLG_PCI_MSI_EN);
			gul_dev->irq[MSI_IRQ_MUX].irq_val = gul_dev->pdev->irq;
			gul_dev->irq_count = 1;
		} else {
			dev_warn(gul_dev->dev,
				 "Failed to init MSI, fall bk to legacy\n");
			goto msi_error;
		}
		__attribute__((__fallthrough__));
		/* Fall through */
	case PCI_INT_MODE_LEGACY:
		gul_dev->irq[MSI_IRQ_MUX].irq_val = gul_dev->pdev->irq;
		gul_dev->irq_count = 1;
		break;

	case PCI_INT_MODE_NONE:
		break;
	}

	return 0;

msi_error:
	return -EINTR;
}

static void gul_dev_free(struct gul_dev *gul_dev)
{
	list_del(&gul_dev->list);

	pci_set_drvdata(gul_dev->pdev, NULL);
	kfree(gul_dev);
}

static int pcidev_tune_caps(struct pci_dev *pdev)
{
	struct pci_dev *parent;
	u16 pcaps, ecaps, ctl, linkstat;
	int rc_sup, ep_sup;

	/* Find out supported and configured values for parent (root) */
	parent = pdev->bus->self;
	if (parent->bus->parent) {
		dev_err(&pdev->dev, "Parent not root\n");
		return -EINVAL;
	}

	if (!pci_is_pcie(parent) || !pci_is_pcie(pdev))
		return -EINVAL;

	pcie_capability_read_word(parent, PCI_EXP_DEVCAP, &pcaps);
	pcie_capability_read_word(pdev, PCI_EXP_DEVCAP, &ecaps);

	/* Find max payload supported by root, endpoint */
	rc_sup = pcaps & PCI_EXP_DEVCAP_PAYLOAD;
	ep_sup = ecaps & PCI_EXP_DEVCAP_PAYLOAD;
	dev_info(&pdev->dev, "PCI max payload size  rc:%d ep:%d\n",
			128 * (1<<rc_sup), 128 * (1<<ep_sup));
	if (rc_sup > ep_sup)
		rc_sup = ep_sup;

	pcie_capability_clear_and_set_word(parent, PCI_EXP_DEVCTL,
					   PCI_EXP_DEVCTL_PAYLOAD, rc_sup << 5);

	pcie_capability_clear_and_set_word(pdev, PCI_EXP_DEVCTL,
					   PCI_EXP_DEVCTL_PAYLOAD, rc_sup << 5);

	pcie_capability_read_word(pdev, PCI_EXP_DEVCTL, &ctl);
	dev_dbg(&pdev->dev, "MAX payload size is %dB, MAX read size is %dB.\n",
			128 << ((ctl & PCI_EXP_DEVCTL_PAYLOAD) >> 5),
			128 << ((ctl & PCI_EXP_DEVCTL_READRQ) >> 12));

	linkstat = pci_current_link_speed(pdev);
	dev_info(&pdev->dev," PCI Link Width:X%x Current:X%x(%s)\n",
		pcie_get_width_cap(pdev),
		((linkstat & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT),
		PCI_EXP_SPEED2STR(linkstat & PCI_EXP_LNKSTA_CLS));


	return 0;
}


static struct gul_dev *gul_pci_priv_init(struct pci_dev *pdev, int gul_id)
{
	struct gul_dev *gul_dev = NULL;
	int i, rc = 0;
	char dn_modem_name[IFNAMSIZ];

	gul_dev = kzalloc(sizeof(struct gul_dev), GFP_KERNEL);
	if (!gul_dev) {
		dev_err(&pdev->dev, "failed to kzalloc for pci device!\n");
		goto out;
	}

	gul_dev->dev = &pdev->dev;
	gul_dev->pdev = pdev;
	gul_dev->id = gul_id;
	gul_dev_id[gul_id].pdev = pdev;
	gul_dev_id[gul_id].id = gul_id;
	gul_dev_id[gul_id].is_pci_dev_removed = 0;
	gul_dev_id[gul_id].remove_in_progress = 0;

	sprintf(&gul_dev->name[0], "%s%d", gul_dev_name_prefix_g, gul_dev->id);
	/* Get the BAR resources and remap them into the driver memory */

	g_gul_global[gul_dev->id].active = 1;
	sprintf(g_gul_global[gul_dev->id].dev_name, "%s",
		dev_name(gul_dev->dev));

	dev_info(gul_dev->dev, "Modem Init - %s !\n", gul_dev->name);

	sprintf(&dn_modem_name[0], "modem%d", gul_dev->id);
	gul_dev->dn_modem = of_find_node_by_name(NULL, dn_modem_name);
	if (!gul_dev->dn_modem) {
#if defined (LA1224)
		dev_err(gul_dev->dev,
			"No Multi modem DTB, fallback to single modem mode\n");
#endif
		/*
		 * FIXME : This is for DTB backward compatibility.
		 * With old DTB, same entries read for second and
		 * above modems. So, only first modem will work.
		 */
		gul_dev->dn_modem = NULL;
	}

	get_factory_unique_id();
	dev_info(gul_dev->dev, "Processor Unique ID: 0x%x\n", fact_id);

	/*
	 * Inverness: BAR0 - CCSR, BAR1 - FlexSPI NOR, BAR2 - PEBM
	 * modem memory is kept in GUL_MEM_REGION_PEBM mem_region
	 */
	for (i = GUL_MEM_REGION_CCSR; i < GUL_MEM_REGION_BAR_END; i++) {
		/* Read the hardware address */
		switch (i) {
		case GUL_MEM_REGION_CCSR:
			gul_dev->mem_regions[i].phys_addr =
			pci_resource_start(pdev, i); //BAR0
			gul_dev->mem_regions[i].phys_addr +=
				GUL_MEM_REGION_CCSR_OFFSET;
			gul_dev->mem_regions[i].size =
				GUL_MMAP_CCSR_OFFSET; //64MB
			break;

		case GUL_MEM_REGION_DCSR:
			if (GUL_MEM_REGION_DCSR_OFFSET) {
				gul_dev->mem_regions[i].phys_addr =
				pci_resource_start(pdev, 0); //BAR0 for LA12xx
			} else {
				gul_dev->mem_regions[i].phys_addr =
				pci_resource_start(pdev, i);//BAR1
			}
			gul_dev->mem_regions[i].phys_addr +=
				GUL_MEM_REGION_DCSR_OFFSET;
			gul_dev->mem_regions[i].size =
				GUL_MMAP_DCSR_SIZE; //1 MB
			break;

		case GUL_MEM_REGION_PEBM:
			gul_dev->mem_regions[i].phys_addr =
				pci_resource_start(pdev, i);//BAR2
			gul_dev->mem_regions[i].phys_addr +=
				GUL_MEM_REGION_PEBM_OFFSET;
			gul_dev->mem_regions[i].size =
				GUL_MMAP_PEBM_SIZE; //1 MB
			break;

		case GUL_MEM_REGION_FECA:
			gul_dev->mem_regions[i].phys_addr =
				pci_resource_start(pdev, 2);//BAR2
			gul_dev->mem_regions[i].phys_addr +=
				GUL_MEM_REGION_FECA_OFFSET;
			gul_dev->mem_regions[i].size =
				GUL_MEM_REGION_FECA_MEM_SIZE; //32 MB
			break;

		default:
			dev_err(gul_dev->dev, "No region Mapped\n");
			break;
		}
		dev_info(gul_dev->dev, "BAR:%d  addr:0x%llx len:0x%llx\n",
			 i, gul_dev->mem_regions[i].phys_addr,
			 (u64)gul_dev->mem_regions[i].size);
	}

	rc = gul_map_mem_regions(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "Failed to map mem regions, err %d\n",
			rc);
		goto out;
	}

	rc = gul_dev_set_interrupt_capability(gul_dev,
				PCI_INT_MODE_MULTIPLE_MSI);
	if (rc < 0) {
		dev_err(gul_dev->dev, "Cannot set the capability of device\n");
		goto out;
	}

	list_add_tail(&gul_dev->list, &pcidev_list);

out:
	if (rc) {
		if (gul_dev) {
			gul_unmap_mem_regions(gul_dev);
			kfree(gul_dev);
		}
		gul_dev = NULL;
	}

	return gul_dev;
}

/**
 * gul_remove_pci_dev - kthread context to remove gul dev
 * @arg: input argument, pdev
 *
 * Return:
 * 0 if controller is removed from pci subsystem.
 * -1 for other case.
 */
static int gul_remove_pci_dev(void *arg)
{
	struct gul_modem_dev_id *temp_dev_id = (struct gul_modem_dev_id *)arg;
	struct pci_dev *pdev = temp_dev_id->pdev;

	if (!pdev)
		return -1;

	pci_stop_and_remove_bus_device_locked(pdev);

	put_device(&pdev->dev);
	temp_dev_id->is_pci_dev_removed = 1;
	temp_dev_id->remove_in_progress = 0;
	return 0;
}

static int gul_pcidev_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	int i, rc = 0, dev_id = 0;
	struct gul_dev *gul_dev = NULL;
	struct task_struct *p;

	dev_info(&pdev->dev, "SHA ID : %s\n", VERSION);

	i = get_gul_dev_id_pcidevname(&pdev->dev);
	/* Not allowed to create it */
	if (i == -1) {
		dev_err(&pdev->dev,
			"Not ok to create %s as LA12xx devices!\n",
			dev_name(&pdev->dev));
		return 0;
	}

	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "failed to enable\n");
		goto err1;
	}

	rc = pci_request_regions(pdev, driver_name);
	if (rc) {
		dev_err(&pdev->dev, "failed to request pci regions\n");
		goto err2;
	}

	pci_set_master(pdev);

	rc = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (rc) {
		rc = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (rc) {
			dev_err(&pdev->dev, "Could not set PCI Mask\n");
			goto err3;
		}
	}
	pcidev_tune_caps(pdev);

	/*Initialize gul_dev from information obtained from pci_dev*/
	gul_dev = gul_pci_priv_init(pdev, i);
	if (!gul_dev) {
		rc = -ENOMEM;
		dev_err(&pdev->dev, "gul_pci_priv_init failed, err %d\n", rc);
		goto err4;
	}
	dev_dbg(&pdev->dev, "%s gul_dev 0x%px, name %s\n",
				 __func__, gul_dev, gul_dev->name);

	dev_id = gul_dev->id;
	gul_dev->class = gul_class;
#if defined(ERRATA_A010867)
	rc = gul_errata_a010867(gul_dev);
	/**
	 * To support boards other than la1224rdb,
	 * continue even if GPIO pin not defined in DTB.
	 */
	if (rc && (rc != -ENODEV)) {
		dev_err(gul_dev->dev, "Failed to toggle GPIO pin (ERRATA A-010867\r\n");
		goto err5;
	}
#endif
	rc = gul_base_probe(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "gul_base_probe failed, err %d\n", rc);
		goto err5;
	}

	pci_set_drvdata(pdev, gul_dev);

	return rc;

err5:
	gul_unmap_mem_regions(gul_dev);
	gul_dev_reset_interrupt_capability(gul_dev);

err4:
	if (gul_dev)
		gul_dev_free(gul_dev);

err3:
	pci_release_regions(pdev);

err2:
	pci_disable_device(pdev);
	get_device(&pdev->dev);

	/*Remove the Host */
	p = kthread_run(gul_remove_pci_dev, &gul_dev_id[dev_id],
		"gul_remove_pci_dev_%d", dev_id);
	if (IS_ERR(p))
		dev_err(&pdev->dev, "gul_remove_pci_dev failed, err\n");
	else
		dev_info(&pdev->dev, "gul_remove_pci_dev success\n");

err1:
	return rc;
}

static void gul_pcidev_remove(struct pci_dev *pdev)
{
	struct gul_dev *gul_dev = pci_get_drvdata(pdev);
	struct task_struct *p;

	if (!gul_dev || gul_dev_id[gul_dev->id].remove_in_progress)
		return;

	gul_dev_id[gul_dev->id].remove_in_progress = 1;
	gul_base_remove(gul_dev);
	gul_unmap_mem_regions(gul_dev);
	gul_dev_reset_interrupt_capability(gul_dev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	wdog_set_modem_status(gul_dev->id, WDOG_MODEM_NOT_READY);
	wdog_set_pci_domain_nr(gul_dev->id, PCI_DOMAIN_NR_INVALID);

	/* Hold the device till thread execution complete */
	get_device(&pdev->dev);

	/*Remove the Host */
	p = kthread_run(gul_remove_pci_dev, &gul_dev_id[gul_dev->id],
		"gul_remove_pci_dev_%d", gul_dev->id);
	if (IS_ERR(p))
		dev_err(&pdev->dev, "gul_remove_pci_dev failed, err\n");
	else
		dev_info(&pdev->dev, "gul_remove_pci_dev success\n");

	gul_dev_free(gul_dev);
}

/**
 * gul_pcidev_reset_device - remove and rescan the requested pcie device
 * @arg: gul_id - pcie deviceid
 */
void gul_pcidev_reset_device(unsigned int gul_id)
{
	struct gul_modem_dev_id *temp_dev_id =
			(struct gul_modem_dev_id *)&gul_dev_id[gul_id];
	struct pci_dev *pdev = temp_dev_id->pdev;
	struct pci_bus *b = NULL;

	pci_stop_and_remove_bus_device_locked(pdev);
	wdog_reset_modem_ext(gul_id);
	usleep_range(TIMEGAP_USEC_MIN, TIMEGAP_USEC_MAX);

	pci_lock_rescan_remove();
	while ((b = pci_find_next_bus(b)) != NULL)
		pci_rescan_bus(b);

	pci_unlock_rescan_remove();
}
EXPORT_SYMBOL_GPL(gul_pcidev_reset_device);

static const struct of_device_id fsl_guts_of_match[] = {
	{ .compatible = "fsl,ls1043a-dcfg", },
	{ .compatible = "fsl,ls2080a-dcfg", },
	{ .compatible = "fsl,ls1088a-dcfg", },
	{ .compatible = "fsl,ls1012a-dcfg", },
	{ .compatible = "fsl,ls1046a-dcfg", },
	{ .compatible = "fsl,lx2160a-dcfg", },
	{ .compatible = "fsl,ls1028a-dcfg"},
	{}
};

static u32 fsl_get_svr(void)
{
	const struct of_device_id *match;
	struct ccsr_guts __iomem *regs;
	struct device_node *np;
	bool little_endian;
	u32 svr = 0;

	np = of_find_matching_node_and_match(NULL, fsl_guts_of_match, &match);
	if (!np)
		return 0;

	regs = of_iomap(np, 0);
	if (!regs) {
		of_node_put(np);
		return -ENOMEM;
	}

	little_endian = of_property_read_bool(np, "little-endian");
	if (little_endian)
		svr = ioread32(&regs->svr);
	return svr;
}

/* Deprecated macro */
//static DEFINE_PCI_DEVICE_TABLE(gul_pcidev_ids) = {
static const struct pci_device_id gul_pcidev_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, PCI_DEVICE_ID_GUL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, PCI_DEVICE_ID_LA1235_B0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, PCI_DEVICE_ID_LA1238_B0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, PCI_DEVICE_ID_LA1234_LA1201_B0) },
	{ 0 },
};

static struct pci_driver gul_pcidev_driver = {
	.name		= "NXP-LA12xx-Driver",
	.id_table	= gul_pcidev_ids,
	.probe		= gul_pcidev_probe,
	.remove		= gul_pcidev_remove
};

static int __init gul_pcidev_init(void)
{
	int i, err = 0;
	int gul_scratch_buf_size;

	pr_info("NXP PCIe LA12xx Driver.\n");


	if (pci_addr_count > MAX_MODEM) {
		pr_err("ERR %s: maximum LA12xx device supported is only %d\n",
			       __func__, MAX_MODEM);
			err = -EINVAL;
			goto out;
	}

	/* reset the modem_num and base it on pci_addr_array */
	if (pci_addr_count)
		modem_num = pci_addr_count;

	for (i = 0; i < pci_addr_count && i < MAX_MODEM; i++) {
		if (pci_addr_array[i]) {
			pr_notice("Module ARG: PCI addr parsed: %s\n",
				pci_addr_array[i]);
			sprintf(g_gul_global[i].dev_name, "%s",
				pci_addr_array[i]);
			gul_dev_id_g++;
		}
	}

	if (!(global_scratch_buf_size && scratch_buf_phys_addr)) {
		pr_err("ERR %s: Scratch buf values are not correct\n",
		       __func__);
		err = -EINVAL;
		goto out;
	}

	svr_family = fsl_get_svr() & SVR_FAMILY_MASK;
	pr_info("NXP %s svr=0x%x family device detected\n",
			get_host_svr_name(svr_family),
			svr_family);
	if ((svr_family == SVR_LS1046A_FAMILY)
		|| (svr_family == SVR_LS1043A_FAMILY)) {
		dma_addr_t dpaa_addr = get_qman_pfdr_addr();
		size_t dpaa_sz = get_qman_pfdr_size();

		/* LS1046A or LS1043A family
		 * the scratch_buf_phys_addr+global_scratch_buf_size should be greater
		 *  qman and bman allocated address space
		 */
		if ((dpaa_addr+dpaa_sz) > scratch_buf_phys_addr) {
			pr_err("\nERR: scratch_buf_phys_addr (0x%llx) is conflicting with "
				"qman-pfdr address (0x%llx) +size (0x%lx)",
				scratch_buf_phys_addr, dpaa_addr, dpaa_sz);
			err = -EINVAL;
			goto out;
		}
		dpaa_addr = get_qman_fqd_addr();
		dpaa_sz = get_qman_fqd_size();
		if ((dpaa_addr+dpaa_sz) > scratch_buf_phys_addr) {
			pr_err("\nERR: scratch_buf_phys_addr (0x%llx) is conflicting with "
				"qman-fqd address (0x%llx) +size (0x%lx)",
				scratch_buf_phys_addr, dpaa_addr, dpaa_sz);
			err = -EINVAL;
			goto out;
		}
		dpaa_addr = get_bman_fbpr_addr();
		dpaa_sz = get_bman_fbpr_size();
		if ((dpaa_addr+dpaa_sz) > scratch_buf_phys_addr) {
			pr_err("\nERR: scratch_buf_phys_addr (0x%llx) is conflicting with "
				"bman-fbpr address (0x%llx) +size (0x%lx)",
				scratch_buf_phys_addr, dpaa_addr, dpaa_sz);
			err = -EINVAL;
			goto out;
		}
	}

	modem_share_buf_size = ALIGN(modem_share_buf_size, PAGE_SIZE);
	modem_rf_data_size = ALIGN(modem_rf_data_size, PAGE_SIZE);

	/* usages per modem */
	gul_scratch_buf_size = GUL_MAX_IMAGE_SIZE + GUL_EP_LOGGER_SIZE
			+ GUL_VSPA_SCRATCH_BUF_MAX_SIZE
			+ GUL_VSPA_OVERLAY_SIZE
			+ modem_share_buf_size
			+ modem_rf_data_size;

	/* if modem host data is not specified */
	if (modem_host_data_size == 0) {
		int size_per_modem = global_scratch_buf_size/modem_num;

		if (gul_scratch_buf_size < size_per_modem) {
			/* assign the remaining scratch buffer to host */
			modem_host_data_size = size_per_modem - gul_scratch_buf_size;
			/*todo - fix issue by reducing it by 1 MB */
			if (modem_host_data_size > 0x100000)
				modem_host_data_size -= 0x100000;
		}
	}
	modem_host_data_size = ALIGN(modem_host_data_size, PAGE_SIZE);

	gul_scratch_buf_size += modem_host_data_size;
	gul_scratch_buf_size = ALIGN(gul_scratch_buf_size, PAGE_SIZE);

	if ((modem_num*gul_scratch_buf_size) > global_scratch_buf_size) {
		pr_err("ERR %s: scratch buffer is not sufficient for %d modem Required=0x%x - Given=0x%x\n",
			       __func__, modem_num,
			       modem_num*gul_scratch_buf_size,
			       global_scratch_buf_size);
			err = -EINVAL;
			goto out;
	}

	if (!(strlen(vspa_fw_name_prefix))) {
		pr_err("ERR %s: alt_vspa_fw_name_prefix empty", __func__);
		err = -EINVAL;
		goto out;
	}

	gul_class = class_create(THIS_MODULE, driver_name);
	if (IS_ERR(gul_class)) {
		pr_err("%s:%d Error in creating (%s) class\n",
			__func__, __LINE__, driver_name);
		goto out;
	}

	gul_init_global_sysfs();

	err = gul_subdrv_mod_init();
	if (err)
		goto out1;
	err = init_tti_dev();
	if (err)
		goto out1;

	if (hsdcs_enable == 1 && warmup_timeout != 0) {
		for (i = 0; i < modem_num && i < MAX_MODEM; i++) {
			warmup_flag[i] = 1;
			gul_set_warmup_status(i, GUL_WARMUP_IN_PROGRESS);
		}
	}

	err = pci_register_driver(&gul_pcidev_driver);
	if (err) {
		pr_err("%s:%d pci_register_driver() failed!\n",
			__func__, __LINE__);
	}

	return err;
out1:
	gul_remove_global_sysfs();
	if (gul_class)
		class_destroy(gul_class);
	gul_class = NULL;
out:
	return err;
}

static void __exit gul_pcidev_exit(void)
{

	pci_unregister_driver(&gul_pcidev_driver);
	gul_subdrv_mod_exit();
	remove_tti_dev();
	class_destroy(gul_class);
	gul_remove_global_sysfs();

	pr_info("Exit from NXP PCIe LA12xx driver\n");
}

module_init(gul_pcidev_init);
module_exit(gul_pcidev_exit);

module_param(warmup_temp, uint, 0400);
MODULE_PARM_DESC(warmup_temp, "warmup temprature(°C)");

module_param(warmup_timeout, uint, 0400);
MODULE_PARM_DESC(warmup_timeout, "warmup timeout(IN SECONDS)");

module_param(warmup_poll_intvl, uint, 0400);
MODULE_PARM_DESC(warmup_poll_intvl, "Current temperature logging intervel \
(IN SECONDS) at FRTOS console during warm-up is in progress");

module_param(rfic_disable, int, 0400);
MODULE_PARM_DESC(rfic_disable, "Disable RFIC firmware loading");

module_param(tvd_disable, int, 0400);
MODULE_PARM_DESC(tvd_disable, "Disable Thrermal Driver loading");

module_param(wdog_disable, int, 0400);
MODULE_PARM_DESC(wdog_disable, "Disable Watchdog Driver loading");

module_param(ls_dac_sps, int, 0400);
MODULE_PARM_DESC(ls_dac_sps, "Configure LS DAC sps, 491, 245, 122 (or) 61");

module_param(ls_adc_sps, int, 0400);
MODULE_PARM_DESC(ls_adc_sps, "Configure LS ADC sps, 245, 122 (or) 61");

module_param(lsdcs_disable, int, 0400);
MODULE_PARM_DESC(lsdcs_disable, "Disable LSDCS");

module_param(hsadc_mask, int, 0400);
MODULE_PARM_DESC(hsadc_mask, "HS - ADC enable mask - bit wise");

module_param(hsdac_mask, int, 0400);
MODULE_PARM_DESC(hsdac_mask, "HS - DAC enable mask - bit wise");

module_param(lsadc_mask, int, 0400);
MODULE_PARM_DESC(lsadc_mask, "LS - ADC enable mask - bit wise");

module_param(lsdac_mask, int, 0400);
MODULE_PARM_DESC(lsdac_mask, "LS - ADC enable mask - bit wise");

module_param(hsdcs_enable, int, 0400);
MODULE_PARM_DESC(hsdcs_enable, "Enable HSDCS firmware loading");

module_param(hsdcs_sps, int, 0400);
MODULE_PARM_DESC(hsdcs_sps, "Configure HS DCS sps, 1966 (or) 983");

module_param(tbgen2_disable, int, 0400);
MODULE_PARM_DESC(tbgen2_disable, "Disable TBGen2");

module_param(vspa_disable, int, 0400);
MODULE_PARM_DESC(vspa_disable, "Disable VSPA firmware loading");

module_param(modem_host_uart, int, 0400);
MODULE_PARM_DESC(modem_host_uart, "Enable modem log to host");

module_param(tti_per_dev, int, 0400);
MODULE_PARM_DESC(tti_per_dev, "Number of TTI interrupt lines per LA12xx Device");

module_param(disable_sideband, int, 0400);
MODULE_PARM_DESC(disable_sideband,
		"Diable sideband signal between modem and host");

module_param_named(scratch_buf_size, global_scratch_buf_size, int, 0400);
MODULE_PARM_DESC(scratch_buf_size, "Scratch buffer size for all LA12xx Device");

module_param_named(share_buf_size, modem_share_buf_size, int, 0400);
MODULE_PARM_DESC(share_buf_size, "Modem share buffer size for each LA12xx Device");

module_param_named(rf_data_size, modem_rf_data_size, int, 0400);
MODULE_PARM_DESC(rf_data_size, "RFIC buffer size for each LA12xx Device");

module_param_named(host_data_size, modem_host_data_size, int, 0400);
MODULE_PARM_DESC(host_data_size, "HOST buffer size for each LA12xx Device");

module_param(scratch_buf_phys_addr, ullong, 0400);
MODULE_PARM_DESC(scratch_buf_phys_addr,
	"Scratch buffer start physical address");

module_param(cli_dmesg_on, int, 0400);
MODULE_PARM_DESC(cli_dmesg_on, "Get cli message in dmesg log");

module_param_string(alt_vspa_fw_name_prefix, vspa_fw_name_prefix,
		    sizeof(vspa_fw_name_prefix), 0400);
MODULE_PARM_DESC(alt_vspa_fw_name_prefix,
"Alternative VSP firmware name prefix e.g geul-vspa for geul-vspa*.eld");

module_param_string(alt_firmware_name, firmware_name,
		    sizeof(firmware_name), 0400);
MODULE_PARM_DESC(alt_firmware_name,
"Alternative E200 firmware name e.g geul_e200.elf");

module_param_array(pci_addr_array, charp, &pci_addr_count, 0400);
MODULE_PARM_DESC(pci_addr_array, "pci addr list in ID creation order");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NXP");
MODULE_DESCRIPTION("PCIe LA12xx Endpoint Driver");
MODULE_VERSION(GUL_HOST_SW_VERSION);
