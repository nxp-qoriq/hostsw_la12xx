// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2025 NXP
 */

#include <linux/delay.h>
#include <linux/of_gpio.h>
#include "gul_base.h"
#include "gul_dcs.h"
#include "gul_host_if.h"
#include "gul_tvd_ioctl.h"
#include <linux/kthread.h>
#include <linux/signal.h>
#include <linux/random.h>
#include <linux/ktime.h>
#include <asm/atomic.h>
#include <linux/rcupdate.h>

#if HSDCS_TMON_THREAD_ENABLED
u8 hsdcs_tmon_modem_trigger_inc_recal;
u8 hsdcs_tmon_modem_trigger_full_recal;
static struct task_struct *hsts_tmon;
static struct gul_dev __rcu *dev_list[MAX_MODEM] = {NULL};
#endif
extern struct tvd_dev *g_tvd_dev;
extern int mtd_get_temp_nowait(struct tvd_dev *tvd_dev, uint32_t tvdid,
		enum mtd_temp_sites site, int32_t *mtd_temp, bool single_site);

void hsdcs_note_tstamp_event(struct gul_dev *dev, char *event);
uint32_t tdd_ctrl1_state_g;
uint32_t tdd_ctrl2_state_g;

uint32_t dac_txi_txq_mask;
uint32_t adc_rxi_rxq_mask;

#if HSDCS_CAL_DEBUG
struct
{
	ktime_t tm;  /* event's timestamp */
	char    str[HSDCS_CAL_TS_LEN]; /* event name */
} hsadc_cal_ts[HSDCS_CAL_MAX_EVENTS]; /* stores timestamps of events */
#endif /* HSDCS_CAL_DEBUG */

#ifdef DEBUG_DUMP_TIMESTAMP
ktime_t ktime_get(void);
ktime_t tstart, tend;
s64 time_taken;
#define START_TIME {\
	tstart = ktime_get();\
}

#define STOP_TIME {\
	tend = ktime_get();\
	time_taken = ktime_to_ms(ktime_sub(tend, tstart));\
	printk("%s():Time taken [ %u ] milli sec\n", __func__, (unsigned int)time_taken);\
}
#else
#define START_TIME
#define STOP_TIME
#endif

#if DEBUG_SHOW_SCFG_REG_CONFIG
static void show_scfg_cfg_ctrl_regs(struct gul_dev *gul_dev)
{
	uint32_t *scfg_ctrl = NULL;

	/* print SCFG Registers */
	scfg_ctrl = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL6_OFFSET);
	dev_info(gul_dev->dev, "DCS HS: (scfg6 = 0x%x)\n", readl(scfg_ctrl));
	scfg_ctrl = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL5_OFFSET);
	dev_info(gul_dev->dev, "DCS HS: (scfg5 = 0x%x)\n", readl(scfg_ctrl));
	scfg_ctrl = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL4_OFFSET);
	dev_info(gul_dev->dev, "DCS HS: (scfg4 = 0x%x)\n", readl(scfg_ctrl));
	scfg_ctrl = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL3_OFFSET);
	dev_info(gul_dev->dev, "DCS HS: (scfg3 = 0x%x)\n", readl(scfg_ctrl));
	scfg_ctrl = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL2_OFFSET);
	dev_info(gul_dev->dev, "DCS HS: (scfg2 = 0x%x)\n", readl(scfg_ctrl));
	scfg_ctrl = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL1_OFFSET);
	dev_info(gul_dev->dev, "DCS HS: (scfg1 = 0x%x)\n", readl(scfg_ctrl));
	scfg_ctrl = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL0_OFFSET);
	dev_info(gul_dev->dev, "DCS HS: (scfg0 = 0x%x)\n", readl(scfg_ctrl));

}
#endif /* DEBUG_SHOW_SCFG_REG_CONFIG */

int hsdcs_check_clk_983(struct gul_dev *gul_dev)
{
	uint32_t *dcs_pll_addr = NULL;
	uint32_t pll_val = 0;

	dcs_pll_addr = (uint32_t *) (gul_dev->mem_regions
		[GUL_MEM_REGION_CCSR].vaddr +
		DCS_CLK_GEN_OFFSET + DCS_PLLCR0_OFFSET);

	pll_val = readl(dcs_pll_addr);
	if ((pll_val & (1 << 14)) && (pll_val & (1<<15))) {
		dev_dbg(gul_dev->dev, "HSDCS:PLLCLK 983MHz,Reg=%x\n", pll_val);
		return 1;
	}

	dev_dbg(gul_dev->dev, "HSDCS:PLLCLK 1966MHz,Reg=%x\n", pll_val);
	return 0;
}

static void hsdcs_release_core(struct gul_dev *gul_dev)
{
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct hsdcs_regs_map *hsdcs_regs =
		(struct hsdcs_regs_map *) dcsdev->hsdcs_regs;
	u32 val;

	val = DCSCORE_FREEZE | DCSCORE_RESET;
	writel(val, &hsdcs_regs->dcs_ctrl);
	dev_dbg(gul_dev->dev, "%s: dcs_ctrl 0x%px:0x%x\n", __func__,
			&hsdcs_regs->dcs_ctrl,
			readl(&hsdcs_regs->dcs_ctrl));
	udelay(10);
	val = DCSCORE_FREEZE;
	writel(val, &hsdcs_regs->dcs_ctrl);
	dev_dbg(gul_dev->dev, "%s: dcs_ctrl 0x%px:0x%x\n", __func__,
			&hsdcs_regs->dcs_ctrl,
			readl(&hsdcs_regs->dcs_ctrl));
	dma_wmb();
	udelay(500);
	val = 0;
	writel(val, &hsdcs_regs->dcs_ctrl);

	dev_dbg(gul_dev->dev, "%s: dcs_ctrl 0x%px:0x%x\n", __func__,
			&hsdcs_regs->dcs_ctrl,
			readl(&hsdcs_regs->dcs_ctrl));
	dev_dbg(gul_dev->dev, "[Rel: RMV] dcs_ctrl=%x\n",
		readl(&hsdcs_regs->dcs_ctrl));
}

static void hsdcs_pre_init_fw(struct gul_dev *gul_dev)
{
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct hsdcs_regs_map *hsdcs_regs =
		(struct hsdcs_regs_map *) dcsdev->hsdcs_regs;
	u32 val;

	val = DCSCORE_FREEZE | DCSCORE_PROGRAM_MODE | DCSCORE_PROGRAM_RESET;
	writel(val, &hsdcs_regs->dcs_ctrl);
	dev_dbg(gul_dev->dev, "%s: dcs_ctrl 0x%px:0x%x\n", __func__,
			&hsdcs_regs->dcs_ctrl,
			readl(&hsdcs_regs->dcs_ctrl));
	udelay(10);
	dma_wmb();
	val = DCSCORE_FREEZE | DCSCORE_PROGRAM_MODE;
	writel(val, &hsdcs_regs->dcs_ctrl);

	dev_dbg(gul_dev->dev, "%s: dcs_ctrl 0x%px:0x%x\n", __func__,
			&hsdcs_regs->dcs_ctrl,
			readl(&hsdcs_regs->dcs_ctrl));

	/* tell where in DCS memory read/write accesses to occur */
	writel(DCSCORE_PROG_START_VAL, &hsdcs_regs->dcs_prog_strt_addr);
	dev_dbg(gul_dev->dev, "[RMV] dcs_prog_start_addr=%x\n",
		hsdcs_regs->dcs_prog_strt_addr);
	dma_wmb();
}

#if DEBUG_HSDCS_DUMP_REG

#define HSDCS_PRINT_REG(offset, count) \
	for (i = 0; i < count; i = i+4) {\
		reg_addr = (u32 *) ((u64) hsdcs_regs + offset + i);\
		dev_info(gul_dev->dev, "0x%x:0x%x\n", \
			(DCS_HS_CCSR_OFFSET + offset + i), \
				readl(reg_addr));\
	}

static void hsdcs_dump_reg(struct gul_dev *gul_dev)
{
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct hsdcs_regs_map *hsdcs_regs =
		(struct hsdcs_regs_map *) dcsdev->hsdcs_regs;
	u32 i, *reg_addr;

	dev_info(gul_dev->dev, "%s:---Start---\n", __func__);

	HSDCS_PRINT_REG(0, 120)

	writel(0x01, &hsdcs_regs->adc_dac_reg_ctrl);
	udelay(100);
	if (!readl(&hsdcs_regs->adc_dac_reg_gr_status)) {
		dev_err(gul_dev->dev, "%s:adc_dac_reg_gr_status,bus access denied\n",
				__func__);
		return;
	}

	HSDCS_PRINT_REG(0x80, 108)
	HSDCS_PRINT_REG(0x100, 108)
	HSDCS_PRINT_REG(0x180, 100)
	HSDCS_PRINT_REG(0xe00, 68)
	HSDCS_PRINT_REG(0xe80, 68)
	HSDCS_PRINT_REG(0x604, 96)
	HSDCS_PRINT_REG(0xc04, 96)
	HSDCS_PRINT_REG(0x200, 240)
	HSDCS_PRINT_REG(0x400, 240)
	HSDCS_PRINT_REG(0x800, 240)
	HSDCS_PRINT_REG(0xa00, 240)
	HSDCS_PRINT_REG(0xf00, 156)
	HSDCS_PRINT_REG(0xfc0, 12)

	writel(0x0, &hsdcs_regs->adc_dac_reg_ctrl);
	dma_wmb();
	udelay(100);
	if (readl(&hsdcs_regs->adc_dac_reg_gr_status)) {
		dev_err(gul_dev->dev, "%s:adc_dac_reg_gr_statusbus not freed\n",
				__func__);
		return;
	}

	dev_info(gul_dev->dev, "%s:---End---\n", __func__);
}
#endif /* DEBUG_HSDCS_DUMP_REG */

#if HSDCS_FW_VERIFICATION
static int hsdcs_verify_fw_load(struct gul_dev *gul_dev, const u8 *fw_data,
		int size)
{
	uint32_t *data_local, read_data;
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct hsdcs_regs_map *hsdcs_regs =
		(struct hsdcs_regs_map *) dcsdev->hsdcs_regs;

	/* halt the HSDCS core */
	hsdcs_pre_init_fw(gul_dev);

	size = ALIGN(size, sizeof(uint32_t));
	data_local = (uint32_t *) fw_data;
	while (size > 0) {
		/* Reading WORD size data from DCS SRAM to local variable */

		read_data = readl(&hsdcs_regs->dcs_prog_rd_data);

		/* Verify data with loaded data */
		if (*data_local != read_data) {
			dev_err(gul_dev->dev, "%s: fail, off 0x%lx\n",
				__func__, ((u8 *)data_local - (u8 *)fw_data));
			dev_err(gul_dev->dev, "%s: data [0x%x != 0x%x\n",
				__func__, *data_local, read_data);
			return -ENOEXEC;
		}
		dev_dbg(gul_dev->dev, "data_local = %x, read_data=%x\n",
				*data_local, read_data);
		data_local++;
		size -= sizeof(uint32_t);
	}

	hsdcs_release_core(gul_dev);
	return 0;
}
#endif /* HSDCS_FW_VERIFICATION */

static int hsdcs_load_firmware(struct gul_dev *gul_dev, const u8 *fw_data,
	       int size)
{
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct hsdcs_regs_map *hsdcs_regs =
		(struct hsdcs_regs_map *) dcsdev->hsdcs_regs;
	uint32_t *data = NULL, offset;
	int l_size = size;

	/* Make calibration delay counter aware of clock frequency */
	if (hsdcs_check_clk_983(gul_dev))
		writel(0x03d8, &hsdcs_regs->dcs_mailbox_reg2); //983
	else
		writel(0x07ae, &hsdcs_regs->dcs_mailbox_reg2); //1966

	dma_wmb();

	offset = &hsdcs_regs->dcs_prog_wrt_data - &hsdcs_regs->ip_ver_info;

	dev_dbg(gul_dev->dev, "%s: dcs_prog_wrt_data 0x%px, id 0x%px\n",
			__func__, &hsdcs_regs->dcs_prog_wrt_data,
			&hsdcs_regs->ip_ver_info);
	dev_dbg(gul_dev->dev, "%s: prog_wr_data reg 0x%x\n", __func__,
			offset);
	dev_dbg(gul_dev->dev, "%s: data 0x%px, siz %d\n",
		__func__, fw_data, size);
	l_size = ALIGN(l_size, sizeof(uint32_t));
	data = (uint32_t *) fw_data;

	hsdcs_pre_init_fw(gul_dev);
	while (l_size > 0) {
		writel(*data, &hsdcs_regs->dcs_prog_wrt_data);
		dma_wmb();
		udelay(200);
		data++;
		l_size -= sizeof(uint32_t);
	}
	mdelay(10);
	hsdcs_release_core(gul_dev);
	return 0;
}

static int hsdcs_fw_load_verify(struct gul_dev *gul_dev,
			     const u8 *fw_data, int size)
{
	if (hsdcs_load_firmware(gul_dev, fw_data, size)) {
		dev_err(gul_dev->dev, "%s: Failed\n", __func__);
		return -ENOMEM;
	}

#if HSDCS_FW_VERIFICATION
	if (hsdcs_verify_fw_load(gul_dev, fw_data, size)) {
		dev_err(gul_dev->dev, "%s: Firmware verification failed\n",
				__func__);
		return -EILSEQ; /* Illegal Byte Sequence */
	}
	else
		dev_info(gul_dev->dev, "%s: Firmware verification passed\n",
				__func__);

#endif /* HSDCS_FW_VERIFICATION */
	return 0;
}

static int hsdcs_write_firmware(struct gul_dev *gul_dev)
{
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct gul_mem_region_info *dcs_region;
	int ret = 0, fw_size;

	dcs_region = scratch_buf_allocator(gul_dev, GUL_FIRMWARE,
			GUL_MAX_IMAGE_SIZE);
	if (!dcs_region) {
		dev_err(gul_dev->dev, "DCS HS:  FW region not found\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = gul_udev_load_firmware(gul_dev, dcs_region->vaddr,
			dcs_region->size, dcsdev->name, &fw_size);
	if (ret < 0) {
		dev_err(gul_dev->dev, "%s udev firmware request failed\n",
				__func__);
		ret = -ENOENT;
		goto out;
	}

	ret = hsdcs_fw_load_verify(gul_dev, dcs_region->vaddr, fw_size);
	if (ret < 0) {
		dev_err(gul_dev->dev, "DCS HS: Firmware load Failed: %d\n",
					ret);
		return ret;
	}

out:
	return ret;
}

#if ENABLE_PCLK_AXIQ_CLK
static int hsdcs_enable_pclk(struct gul_dev *gul_dev)
{
	uint32_t *scfg_config_ctrl6 = NULL;
	uint32_t clk_status = 0;

	/* Set SCFG Registers Base Address */
	scfg_config_ctrl6 = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL6_OFFSET);
	if (!scfg_config_ctrl6) {
		dev_err(gul_dev->dev, "DCS HS: SCFG register access failed\n");
		return -ENODEV;
	}

	/* Enabling the PCLK */
	clk_status = readl(scfg_config_ctrl6);
	dev_dbg(gul_dev->dev, "DCS HS: SCFG_CONFIG_CTRL6 value = 0x%x\n",
			clk_status);

	if (clk_status & DISABLE_DCS_HS_PCLK) {
		dev_dbg(gul_dev->dev, "DCS HS: PCLK is Disabled\n");
		clk_status &= ~(DISABLE_DCS_HS_PCLK);
		writel(clk_status, scfg_config_ctrl6);
	} else
		dev_dbg(gul_dev->dev, "DCS HS: PCLK is already enabled\n");

	dev_dbg(gul_dev->dev, "DCS HS: After PCLK ENABLE scfg_ctrl6 = 0x%x\n",
			readl(scfg_config_ctrl6));
	return 0;
}

static int hsdcs_enable_axiq_clk(struct gul_dev *gul_dev)
{
	uint32_t *scfg_config_ctrl6 = NULL;
	uint32_t clk_status = 0;

	/* Set SCFG Registers Base Address */
	scfg_config_ctrl6 = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL6_OFFSET);
	if (!scfg_config_ctrl6) {
		dev_err(gul_dev->dev, "DCS HS: SCFG register access failed\n");
		return -ENODEV;
	}

	/* Enabling AXIQ_H Clk */
	clk_status = readl(scfg_config_ctrl6);
	dev_dbg(gul_dev->dev, "DCS HS: SCFG_CONFIG_CTRL6 value = 0x%x\n",
			clk_status);

	if (clk_status & DISABLE_DCS_HS_AXIQ_CLK) {
		dev_dbg(gul_dev->dev, "DCS HS: AXIQ Clock Disabled\n");
		clk_status &= ~(DISABLE_DCS_HS_AXIQ_CLK);
		writel(clk_status, scfg_config_ctrl6);
	} else
		dev_dbg(gul_dev->dev, "DCS HS: AXIQ Clk is enabled already\n");

	dev_dbg(gul_dev->dev, "DCS HS: AXIQ_CLK Enabled (scfg_ctrl6 = 0x%x)\n",
				readl(scfg_config_ctrl6));
	return 0;
}

static int hsdcs_enable_pclk_axiq(struct gul_dev *gul_dev)
{
	int rc;

	rc = hsdcs_enable_pclk(gul_dev);
	if (rc)
		return rc;

	return hsdcs_enable_axiq_clk(gul_dev);
}
#endif

ssize_t hsdcs_show_stats(struct gul_dev *gul_dev, char *buf)
{
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	int last_act = 0, org_act = 0;

	sprintf(&buf[strlen(buf)], " %s", "\n");

	sprintf(&buf[strlen(buf)], " Incr Recalibration enabled    : %s\n",
		 (dcsdev->hsdcs_inc_recal_enabled)?"Yes":"No");
	if (dcsdev->hsdcs_inc_recal_enabled)
	{
		sprintf(&buf[strlen(buf)], " Incr Recalibrated Temperature : %dC\n",
		  dcsdev->ihsdcs_lastime_inc_cal_temp);
		sprintf(&buf[strlen(buf)], " Incr Recalibration Count      : %u\n",
		  dcsdev->dcs_info.fastrecal_cnt);
	}
	sprintf(&buf[strlen(buf)], " %s", "\n");

	sprintf(&buf[strlen(buf)], " Full Recalibration enabled    : %s\n",
		 (dcsdev->hsdcs_full_recal_enabled)?"Yes":"No");
	if (dcsdev->hsdcs_full_recal_enabled)
	{
		sprintf(&buf[strlen(buf)], " Full Recalibrated Temperature : %dC\n",
		  dcsdev->ihsdcs_lastime_full_cal_temp);
		sprintf(&buf[strlen(buf)], " Full Recalibration Count      : %u\n",
		  dcsdev->dcs_info.fullrecal_cnt);
	}
	sprintf(&buf[strlen(buf)], " %s", "\n");

	sprintf(&buf[strlen(buf)], " Temperature Compensation:\n");
	hsdcs_read_temp_compensation(gul_dev, &last_act, &org_act);
	sprintf(&buf[strlen(buf)], " Calibrated: %dC\n", org_act);
	sprintf(&buf[strlen(buf)], " Acted     : %dC\n", last_act);
	sprintf(&buf[strlen(buf)], " Count     : %u\n",
		 dcsdev->dcs_info.temp_comp_cnt);

	sprintf(&buf[strlen(buf)], " %s", "\n");
	sprintf(&buf[strlen(buf)], " Current Temperature: %dC\n", dcsdev->ihsdcs_current_temp);
	sprintf(&buf[strlen(buf)], " %s", "\n");

	return strlen(buf);
}
EXPORT_SYMBOL_GPL(hsdcs_show_stats);

int hsdcs_adc_full_recal(struct gul_dev *gul_dev)
{
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct hsdcs_regs_map *regs =
		(struct hsdcs_regs_map *) dcsdev->hsdcs_regs;
	u32 val = 0, retries = HSDCS_RETRIES, cal_status, val2 = 0;
	int ret = 0;

	START_TIME
	hsdcs_adc_low_pow_conf(gul_dev, DISABLE_LOWPWR, TBGEN_STORE);
	val2 = readl(&regs->dcs_mailbox_reg1);
	HSDCS_SET_BIT_POS(val2, HSADC_CAL_BIT_POS_0);
	HSDCS_SET_BIT_POS(val2, HSADC_CAL_BIT_POS19);//full recal select FW 2.20.0
	HSDCS_CLEAR_BIT_POS(val2, HSADC_CAL_BIT_POS20);
	hsdcs_note_tstamp_event(gul_dev, "Full recal: set mbox_reg1 (bit0 1, bit19 1, bit20 0");
	writel(val2, &regs->dcs_mailbox_reg1);
	dma_wmb();
	udelay(HSDCS_WRITE_MIN_TIMEWAIT);
	while (retries) {
		mdelay(HSDCS_TIMEOUT_MS);
		retries--;
		dma_rmb();
		hsdcs_note_tstamp_event(gul_dev, "get mbox_reg0.bit0");
		val = readl(&regs->dcs_mailbox_reg0_resp);
		if ((val & HSDCS_REG0_RESP_ACK)) {
			dev_dbg(gul_dev->dev, "[Set] : Ack Init Cal\n");
			break;
		}
	}
	if (!(val & HSDCS_REG0_RESP_ACK)) {
		dev_err(gul_dev->dev, "%s:01 Init done not set, 0x%x\n",
			__func__, val);
		ret = -EBUSY;
		goto out;
	}

	hsdcs_note_tstamp_event(gul_dev, "set mbox_reg1.bit0 = 0");
	HSDCS_CLEAR_BIT_POS(val2, HSADC_CAL_BIT_POS_0);
	writel(val2, &regs->dcs_mailbox_reg1);
	dma_wmb();
    mdelay(HSDCS_TIMEOUT_FULL_RECAL);

	retries = HSDCS_RETRIES;
	/* Checking for init_done bit */
	while (retries) {
		mdelay(HSDCS_TIMEOUT_MS);
		retries--;
		dma_rmb();
		hsdcs_note_tstamp_event(gul_dev, "get mbox_reg0.bit4");
		val = readl(&regs->dcs_mailbox_reg0_resp);
		if ((val & HSDCS_REG0_RESP_INIT_DONE))
			break;
	}
	if (!(val & HSDCS_REG0_RESP_INIT_DONE)) {
		dev_err(gul_dev->dev, "%s:10 Init done not set, 0x%x\n",
			__func__, val);
		ret = -EBUSY;
		goto out;
	}

	retries = HSDCS_RETRIES;
	while (retries) {
		mdelay(HSDCS_TIMEOUT_MS);
		retries--;
		dma_rmb();
		hsdcs_note_tstamp_event(gul_dev, "get reg2_resp for 0xff");
		val = readl(&regs->dcs_mailbox_reg2_resp);
		if ((val & HSDCS_BYTE_MASK) == 0)
		break;
	}

	retries = HSDCS_RETRIES;
	while (retries) {
		mdelay(HSDCS_TIMEOUT_MS);
		retries--;
		dma_rmb();
		hsdcs_note_tstamp_event(gul_dev, "get cal_comp_stat ");
		cal_status = readl(&regs->adc_cal_comp_stat);
		if ((cal_status & adc_rxi_rxq_mask)== adc_rxi_rxq_mask)
			break;
	}
	if ((val & adc_rxi_rxq_mask)
		||
		!((cal_status & adc_rxi_rxq_mask) == adc_rxi_rxq_mask)) {
		dev_err(gul_dev->dev,
			"Error !!! DCS HS: ADC Full Re-cal Failed. 0x50[0x%x],0x18[0x%x]\n",
			val, cal_status);
		ret = -EBUSY;
		goto out;
	} else {
		dev_dbg(gul_dev->dev, "DCS HS: ADC Full Re-cal Passed, 0x50[0x%x],0x18[0x%x]\n",
			val, cal_status);
	}

out:
	hsdcs_adc_low_pow_conf(gul_dev, ENABLE_LOWPWR, TBGEN_RESTORE);
	hsdcs_note_tstamp_event(gul_dev, NULL);
	STOP_TIME
	dcsdev->dcs_info.fullrecal_cnt++;
	return ret;
}

/* hsdcs_get_curr_temperature(): A utility func to get the temperature from
   TVD driver and to perform range check on the returned temperature from TVD.

   Inputs:
   -- modem id
   -- a non-NULL ptr to int32. This function updates the temperature here.
   -- str, ptr to a printable string that tells in what context caller invoked this
      func. Used only for pritning purpose

   Output:
   Returns 0: if all good. gul_tmp is updated in this case
   Return -1: if either mtd_get_temp_nowait() returns err or if
   mtd_get_temp_nowait() returns a temperature beyond the supported range for
   LA12xx. gul_tmp is not updated in this case
*/
int hsdcs_get_curr_temperature(struct gul_dev *gul_dev,
				uint32_t modem_id,
				int32_t *gul_tmp,
				const char *str)
{
	if (mtd_get_temp_nowait(g_tvd_dev, modem_id, VSPA_TEMP, gul_tmp, true) < 0) {
		dev_err(gul_dev->dev, "Error %s(): temperature API failed (%s)!!\n", __func__, str);
		return -1;
	}

	if ((*gul_tmp < HSDCS_TEMP_THRESHOLD_LOW) || (*gul_tmp > HSDCS_TEMP_THRESHOLD_HIGH))
	{
			dev_err(gul_dev->dev,
			"DCS HS: Error (%s) !!! LA12xx temperature is %d C, "
			"beyond the supported range of %d C to %d C!!\n", str, *gul_tmp,
			HSDCS_TEMP_THRESHOLD_LOW, HSDCS_TEMP_THRESHOLD_HIGH);
		return -1;
	}

	dev_dbg(gul_dev->dev, "DCS HS: LA12xx vspa-site temperature : %d C (%s)\n", *gul_tmp, str);
	return 0;
} /* int hsdcs_get_curr_temperature() */

#if HSDCS_THERMAL_COMPENSATION
void hsdcs_write_temp_compensation(struct gul_dev *gul_dev)
{
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct hsdcs_regs_map *regs =
		(struct hsdcs_regs_map *) dcsdev->hsdcs_regs;
	u32 val;
	int32_t cur_temp = 0;

	val = readl(&regs->dcs_mailbox_reg2);
	val &= HSADC_TEMP_COMP_READ_MASK;

	if (hsdcs_get_curr_temperature(gul_dev, gul_dev->id, &cur_temp, "write temp comp") < 0)
		return;

	cur_temp += HSADC_TEMP_CONV_KELVIN;
	cur_temp = cur_temp << HSADC_TEMP_COMP_BIT_POS;
	val |= cur_temp;
	writel(val, &regs->dcs_mailbox_reg2);
	dma_wmb();
	dcsdev->dcs_info.temp_comp_cnt++;
}

void hsdcs_read_temp_compensation(struct gul_dev *gul_dev, int *last_act, int *org_act)
{
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct hsdcs_regs_map *regs =
		(struct hsdcs_regs_map *) dcsdev->hsdcs_regs;
	u32 val;
	int last_acted_temp;
	int org_cal_temp;

	val = readl(&regs->dcs_mailbox_reg0_resp);
	last_acted_temp = ((val & HSADC_TEMP_COMP_ACTED_READ_MASK) >> HSADC_TEMP_COMP_ACTED_READ_BITPOS);
	if (last_acted_temp)
		last_acted_temp -= HSADC_TEMP_CONV_KELVIN;
	org_cal_temp = ((val & HSADC_TEMP_COMP_ORG_CALC_MASK) >> HSADC_TEMP_COMP_ORG_CALC_READ_BITPOS);
	org_cal_temp -= HSADC_TEMP_CONV_KELVIN;
	dev_dbg(gul_dev->dev, "%s():last acted temp:%d org cal temp: %d\n\n", __func__, last_acted_temp, org_cal_temp);
	*last_act = last_acted_temp;
	*org_act = org_cal_temp;
}
#endif

void hsdcs_adc_low_pow_conf(struct gul_dev *gul_dev, u8 low_enb_dis, u8 tbgen_rw)
{
	uint32_t *ptbgen2_conf_ctrl1 = NULL;
	uint32_t *ptbgen2_conf_ctrl2 = NULL;
	uint32_t tdd_ctrl1 = 0, tdd_ctrl2 = 0;

	ptbgen2_conf_ctrl1 = (uint32_t *) (gul_dev->mem_regions
		[GUL_MEM_REGION_CCSR].vaddr + HSADC0_LOW_PWR_TBGEN2_CTRL);
	ptbgen2_conf_ctrl2 = (uint32_t *) (gul_dev->mem_regions
		[GUL_MEM_REGION_CCSR].vaddr + HSADC1_LOW_PWR_TBGEN2_CTRL);

	tdd_ctrl1 = readl(ptbgen2_conf_ctrl1);
	tdd_ctrl2 = readl(ptbgen2_conf_ctrl2);

	if (tbgen_rw == TBGEN_STORE)
	{
		dev_dbg(gul_dev->dev, "%s(): tbgen store\n\n", __func__);
		tdd_ctrl1_state_g = tdd_ctrl1;
		tdd_ctrl2_state_g = tdd_ctrl2;
	}

	if (low_enb_dis == ENABLE_LOWPWR)
	{
		dev_dbg(gul_dev->dev, "%s(): enable lp\n\n", __func__);
		/* Check and Set the Low Pwr. if it is not set */
		if (!(HSDCS_CHECK_BIT_POS(tdd_ctrl1, HSADC_LOW_PWR_TBGEN2_CFG_BIT_POS)))
		{
			dev_dbg(gul_dev->dev, "%s(): enable lp adc1\n\n", __func__);
			HSDCS_SET_BIT_POS(tdd_ctrl1, HSADC_LOW_PWR_TBGEN2_CFG_BIT_POS);
			writel(tdd_ctrl1, ptbgen2_conf_ctrl1);
		}
		if (!(HSDCS_CHECK_BIT_POS(tdd_ctrl2, HSADC_LOW_PWR_TBGEN2_CFG_BIT_POS)))
		{
			dev_dbg(gul_dev->dev, "%s(): enable lp adc2\n\n", __func__);
			HSDCS_SET_BIT_POS(tdd_ctrl2, HSADC_LOW_PWR_TBGEN2_CFG_BIT_POS);
			writel(tdd_ctrl2, ptbgen2_conf_ctrl2);
		}
	} else /* DISABLE_LOWPWR */ {
		dev_dbg(gul_dev->dev, "%s(): disable lp\n\n", __func__);
		/* Check and clear the Low Power */
		if ((HSDCS_CHECK_BIT_POS(tdd_ctrl1, HSADC_LOW_PWR_TBGEN2_CFG_BIT_POS)))
		{
			dev_dbg(gul_dev->dev, "%s(): disable lp adc1\n\n", __func__);
			HSDCS_CLEAR_BIT_POS(tdd_ctrl1, HSADC_LOW_PWR_TBGEN2_CFG_BIT_POS);
			writel(tdd_ctrl1, ptbgen2_conf_ctrl1);
		}
		if ((HSDCS_CHECK_BIT_POS(tdd_ctrl2, HSADC_LOW_PWR_TBGEN2_CFG_BIT_POS)))
		{
			dev_dbg(gul_dev->dev, "%s(): disable lp adc2\n\n", __func__);
			HSDCS_CLEAR_BIT_POS(tdd_ctrl2, HSADC_LOW_PWR_TBGEN2_CFG_BIT_POS);
			writel(tdd_ctrl2, ptbgen2_conf_ctrl2);
		}
	}
	dma_wmb();

	if (tbgen_rw == TBGEN_RESTORE)
	{
		dev_dbg(gul_dev->dev, "%s(): tbgen restore\n\n", __func__);
		tdd_ctrl1 = readl(ptbgen2_conf_ctrl1);
		if (tdd_ctrl1 != tdd_ctrl1_state_g)
		{
			dev_dbg(gul_dev->dev, "%s(): tbgen2 restore1 cur:0x%x prv:0x%x\n\n",
				__func__, tdd_ctrl1, tdd_ctrl1_state_g);
			writel(tdd_ctrl1_state_g, ptbgen2_conf_ctrl1);
		}

		tdd_ctrl2 = readl(ptbgen2_conf_ctrl2);
		if (tdd_ctrl2 != tdd_ctrl2_state_g)
		{
			dev_dbg(gul_dev->dev, "%s(): tbgen2 restore2 cur:0x%x prv:0x%x\n\n",
				__func__, tdd_ctrl2, tdd_ctrl2_state_g);
			writel(tdd_ctrl2_state_g, ptbgen2_conf_ctrl2);
		}
		dma_wmb();
	}

	return;
}

/* hsdcs_ping_firmware(): checks if the HS-DCS firmware is alive */
void  hsdcs_ping_firmware(struct dcs_dev *dcs_dev)
{
#if HSDCS_PING_FW

struct hsdcs_regs_map *regs = dcs_dev->hsdcs_regs;
struct gul_dev *gul_dev = dcs_dev->gul_dev;
u32 val;
	val = readl(&regs->dcs_mailbox_reg1);
	val |= HSDCS_PING_FW_BIT;
	writel(val, &regs->dcs_mailbox_reg1);

	mdelay(HSDCS_PING_ASSERT_DELAY);
	val = readl(&regs->dcs_mailbox_reg0_resp);
	if (val & 1) {
		dev_info(gul_dev->dev, "HSDCS PING-assert Ack received (reg0_resp = 0x%x\n", val);

		val = readl(&regs->dcs_mailbox_reg1);
		val &= ~HSDCS_PING_FW_BIT;
		writel(val, &regs->dcs_mailbox_reg1);

		mdelay(HSDCS_PING_DEASSERT_DELAY);
		val = readl(&regs->dcs_mailbox_reg0_resp);
		if ((val & 1) == 0)
			dev_info(gul_dev->dev, "HSDCS PING-deassert Ack received (reg0_resp = 0x%x)\n", val);
		else
			dev_err(gul_dev->dev, "HSDCS PING-deassert Ack NOT received (reg0_resp = 0x%x)\n", val);
	}
	else
		dev_err(gul_dev->dev, "HSDCS PING-assert Ack NOT received (reg0_resp = 0x%x)\n", val);

#endif /* HSDCS_PING_FW */
	return;
} /*  hsdcs_ping_firmware(struct dcs_dev *dcs_dev) */

/*
   hsdcs_dump_cal_regs(): dumps HS-ADC cal related registers.
   second argument: a caller proivded comment to be printed
   Also invokes 'ping' to FW.
 */
static void hsdcs_dump_cal_regs(struct dcs_dev *dcs_dev, char *str)
{

#if HSDCS_CAL_DEBUG

u32 dcs_mailbox_reg0_resp, dcs_mailbox_reg1, dcs_mailbox_reg2_resp, adc_cal_comp_stat;
struct hsdcs_regs_map *regs = dcs_dev->hsdcs_regs;
struct gul_dev *gul_dev = dcs_dev->gul_dev;

	dcs_mailbox_reg0_resp = readl(&regs->dcs_mailbox_reg0_resp);
	dcs_mailbox_reg1 = readl(&regs->dcs_mailbox_reg1);
	dcs_mailbox_reg2_resp = readl(&regs->dcs_mailbox_reg2_resp);
	adc_cal_comp_stat = readl(&regs->adc_cal_comp_stat);

	dev_info(gul_dev->dev, "%s -- dumping registers:\n", str);
	dev_info(gul_dev->dev, "\tREG0_RESP value = 0x%x\n", dcs_mailbox_reg0_resp);
	dev_info(gul_dev->dev, "\tMAILBOX_REG1 value = 0x%x\n", dcs_mailbox_reg1);
	dev_info(gul_dev->dev, "\tMAILBOX_REG2_RESP value = 0x%x\n", dcs_mailbox_reg2_resp);
	dev_info(gul_dev->dev, "\tCAL_COMPLETE_STAT value = 0x%x\n", adc_cal_comp_stat);

	hsdcs_ping_firmware(dcs_dev);

#endif // HSDCS_CAL_DEBUG

	return;
} /* hsdcs_dump_cal_regs() */

/*
   hsdcs_note_tstamp_event(): records timestamp of invocation of this funciton
   for later/offline pritning the lhe prevously logged event with their
   timestamps. User can log multiple events and then can print the log later.
   When this function is called with non-NULL second arg, the funciton
   logs the current
   timestamp and the corresponding event name (event name is passed
   by caller in second arg of this func). Logging is done in the
   host memory.
   To print the event log (along with their timestamps) on console,
   call this function with second arg as NULL ptr. Once the log printed,
   the log memory is ready to log events afresh.

*/
void hsdcs_note_tstamp_event(struct gul_dev *dev, char *event)
{
#if HSDCS_CAL_DEBUG
	static int count = 0;
	int i = 0;

	if (event != NULL) {
		if (count < HSDCS_CAL_MAX_EVENTS) {
		strncpy(hsadc_cal_ts[count].str, event, HSDCS_CAL_TS_LEN);
		hsadc_cal_ts[count++].tm = ktime_get();
		}
	}
	else {

		dev_info(dev->dev, "Dumping timestamps of HS-ADC cal register access events:\n");
		dev_info(dev->dev, "==========================================================\n");
		dev_info(dev->dev, "Event#	details     		timegap (wrt prev row) us\n");
		dev_info(dev->dev, "==========================================================\n");
		dev_info(dev->dev, "%02d: %20s: 	NA\n", i, hsadc_cal_ts[i].str);
		for (i = 1; i < count; i++)
		{
			dev_info(dev->dev, "%02d: %20s: 	%lli us (ie, approx %lli ms)\n", i,
					hsadc_cal_ts[i].str,
					ktime_to_us(ktime_sub(hsadc_cal_ts[i].tm, hsadc_cal_ts[i-1].tm)),
					ktime_to_us(ktime_sub(hsadc_cal_ts[i].tm, hsadc_cal_ts[i-1].tm))/1000
					);
		}
		dev_info(dev->dev, "==========================================================\n");
		count = 0;

	}
#endif
	return;
}

static int hsdcs_do_initial_cal(struct dcs_dev *dcs_dev)
{
u32 cal_status, val, val2, retries = HSDCS_RETRIES, ret = 0;
struct hsdcs_regs_map *regs = dcs_dev->hsdcs_regs;
struct gul_dev *gul_dev = dcs_dev->gul_dev;
struct gul_hif *hif = dcs_dev->gul_dev->hif;

	/* Initiate Calibration */
	val2 = readl(&regs->dcs_mailbox_reg1);
	HSDCS_SET_BIT_POS(val2, HSADC_CAL_BIT_POS_0);
	writel(val2, &regs->dcs_mailbox_reg1);
	hsdcs_note_tstamp_event(gul_dev, "Initial cal: set mbox_reg1.bit0 = 1");
	dma_wmb();
	udelay(10);
	while (retries) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(
					HSDCS_TIMEOUT));
		retries--;
		dma_rmb();
		val = readl(&regs->dcs_mailbox_reg0_resp);
		hsdcs_note_tstamp_event(gul_dev, "get mbox_reg0.bit0");
		if ((val & HSDCS_REG0_RESP_ACK)) {
			dev_dbg(gul_dev->dev, "[Set] : Ack Init Cal\n");
			break;
		}
	}
	if (!(val & HSDCS_REG0_RESP_ACK)) {
		dev_err(gul_dev->dev, "%s:01 Init Ack not set, 0x%x\n",
			__func__, val);
		ret = -EBUSY;
		goto out;
	}

	hsdcs_note_tstamp_event(gul_dev, "set mbox_reg1.bit0 = 0");
	HSDCS_CLEAR_BIT_POS(val2, HSADC_CAL_BIT_POS_0);
	writel(val2, &regs->dcs_mailbox_reg1);

	mdelay(HSDCS_TIMEOUT_INITIAL_CAL);
	dma_wmb();

	retries = HSDCS_RETRIES;
	/* Checking for init_done bit */
	while (retries) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(
					HSDCS_TIMEOUT));
		retries--;
		dma_rmb();
		hsdcs_note_tstamp_event(gul_dev, "get mbox_reg0.bit4");
		val = readl(&regs->dcs_mailbox_reg0_resp);
		if ((val & HSDCS_REG0_RESP_INIT_DONE))
			break;
	}
	if (!(val & HSDCS_REG0_RESP_INIT_DONE)) {
		dev_err(gul_dev->dev, "%s:10 Init done not set, 0x%x\n",
			__func__, val);
		ret = -EBUSY;
		goto out;
	} else if (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL) {
		/* Enable LDO to allow putting ADCs in low power mode using
		 * tbgen controlled signals*/
		uint32_t low_pwr_cfg;

		low_pwr_cfg = readl(&regs->low_pwr_cfg);
		low_pwr_cfg = low_pwr_cfg | HSADC0_LOW_PWR_CFG_LDO;
		low_pwr_cfg = low_pwr_cfg | HSADC1_LOW_PWR_CFG_LDO;
		writel(low_pwr_cfg, &regs->low_pwr_cfg);
		dma_wmb();
	}
	dev_dbg(gul_dev->dev, "DCS HS: ADC Init done, REG0_RESP 0x%x\n", val);

	retries = HSDCS_RETRIES;
	/* Wait For ADC cal to complete */
	while (retries) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(
					HSDCS_TIMEOUT));
		retries--;
		dma_rmb();
		hsdcs_note_tstamp_event(gul_dev, "get reg2_resp for 0xff");
		val = readl(&regs->dcs_mailbox_reg2_resp);
		if ((val & 0xff) == 0)
			break;
	}

	retries = HSDCS_RETRIES;
	while (retries) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(
					HSDCS_TIMEOUT));
		retries--;
		dma_rmb();
		hsdcs_note_tstamp_event(gul_dev, "get cal_comp_stat ");
		cal_status = readl(&regs->adc_cal_comp_stat);
		if ((cal_status & HSDCS_BIT0F_MASK))
			break;
	}

	if ((val & HSDCS_BYTE_MASK)
		||
		!((cal_status & HSDCS_BIT0F_MASK) == adc_rxi_rxq_mask)) {
		dev_err(gul_dev->dev,
			"DCS HS:ADC Initial Cal Failed. 0x50:[0x%x],0x18:[0x%x]\n",
			val, cal_status);
		ret = -EBUSY;
		goto out;
	}

out:
	hsdcs_note_tstamp_event(gul_dev, NULL);
	return ret;

} /* hsdcs_do_initial_cal() */

static int hsdcs_init_adc(struct dcs_dev *dcs_dev)
{
	struct hsdcs_regs_map *regs = dcs_dev->hsdcs_regs;
	struct gul_hif *hif = dcs_dev->gul_dev->hif;
	struct gul_dev *gul_dev = dcs_dev->gul_dev;
	u32 val;
	u32 initial_cal_retries = HSDCS_INITIAL_CAL_RETRIES;
	u32 *cal_ref;
	int ret = 0, modem_temp = 0;

	if (gul_ep_get_soc_rev() == GEUL_SVR_REVA_VAL) {
		/*Enable ADCs, based on the ADC MASK */
		/*Enable ADCs, enable 16G test stream ADC */
		switch (hsadc_mask) {
		case 0x1:
			writel(ADC_ENABLE_1, &regs->adc_enable_ctrl);
			adc_rxi_rxq_mask = ADC_ENABLE_1;
			break;
		case 0x2:
			writel(ADC_ENABLE_2, &regs->adc_enable_ctrl);
			adc_rxi_rxq_mask = ADC_ENABLE_2;
			break;
		case 0x3:
			/*Enable ADCs, enable 16G test stream ADC */
			writel(ADC_16G_EN_MASK, &regs->adc_enable_ctrl);
			adc_rxi_rxq_mask = ADC_16G_EN_MASK;
			break;
		default:
			dev_err(gul_dev->dev, "\nDCS HS: hsadc mask Invalid 0x%x !!\n",
				hsadc_mask);
			ret = -EBUSY;
			goto out;
		}
		udelay(10);

		/* Request internal ADC/DAC bus */
		writel(0x1, &regs->adc_dac_reg_ctrl);
		udelay(100);
		dma_wmb();

		cal_ref = (u32 *) ((u64) regs + CAL_REF_REG_VAL_RA);
		/* IREF_CTRL: Enable external reference */
		writel(0x03, cal_ref);

		/* Release internal ADC/DAC bus */
		writel(0x0, &regs->adc_dac_reg_ctrl);
		udelay(100);
		dma_wmb();

		/* ADC calibrtion mode */
		writel(0x17f, &regs->dcs_mailbox_reg0);
		dma_wmb();
	} else { /* Rev B0*/
		/* Don't enable 16G test stream ADC.
		 * This will override individual channel enables above
		 */
		switch (hsadc_mask) {
		case 0x1:
			writel(ADC_ENABLE_1, &regs->adc_enable_ctrl);
			adc_rxi_rxq_mask = ADC_ENABLE_1;
			break;
		case 0x2:
			writel(ADC_ENABLE_2, &regs->adc_enable_ctrl);
			adc_rxi_rxq_mask = ADC_ENABLE_2;
			break;
		case 0x3:
			writel(ADC_ENABLE_BOTH, &regs->adc_enable_ctrl);
			adc_rxi_rxq_mask = ADC_ENABLE_BOTH;
			break;
		default:
			dev_err(gul_dev->dev, "\nDCS HS: hsadc mask Invalid 0x%x C !!\n",
				hsadc_mask);
			ret = -EBUSY;
			goto out;
		}
		udelay(10);
		/* ADC calibrtion mode */
		writel(0x1ff, &regs->dcs_mailbox_reg0);
		dma_wmb();

		udelay(100); /* wait for TVD Init */
#if HSDCS_THERMAL_COMPENSATION
		//Enable temperature compensation
		val = readl(&regs->dcs_mailbox_reg1);
		val |= HSADC_TEMP_COMP_EN;
		writel(val, &regs->dcs_mailbox_reg1);
		dma_wmb();
		hsdcs_write_temp_compensation(gul_dev);
#endif
		/* Check for the safe operating temperature */
		if (hsdcs_get_curr_temperature(gul_dev, gul_dev->id, &modem_temp, "Initial-cal") < 0)
		{
			dev_err(gul_dev->dev, "Waiting for %d secs and then recheck .....\n", HSDCS_TEMP_RETRY_WAIT_TIME);
			ssleep(HSDCS_TEMP_RETRY_WAIT_TIME);
			if (hsdcs_get_curr_temperature(gul_dev, gul_dev->id, &modem_temp, "initial-cal") < 0)
			{
				ret = -EINVAL;
				goto out;
			}
		}

	}

	while (initial_cal_retries) {
		ret = hsdcs_do_initial_cal(dcs_dev);
		if (ret == 0)
			break;
		else
			hsdcs_dump_cal_regs(dcs_dev,
			"Error!!! DCS HS: Initial cal failed\n");

		mdelay(10);
		initial_cal_retries--;
	}

	if (ret != 0) {
		hsdcs_dump_cal_regs(dcs_dev, "Error!!! DCS HS: Initial cals failed; device reboot needed ");
		goto out;
	}
	else {
		hsdcs_dump_cal_regs(dcs_dev, "DCS HS:ADC Init: calibration successful");
		if (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL) {
			if (hsdcs_get_curr_temperature(gul_dev, gul_dev->id, &modem_temp, "init-cal") < 0) {
				ret = -EINVAL;
				goto out;
			}
			dcs_dev->ihsdcs_lastime_full_cal_temp = modem_temp;
			dcs_dev->hsdcs_inc_recal_enabled = 1;
			dcs_dev->hsdcs_full_recal_enabled = 1;
		}
	}
	/* Make the tbgen control signals to low power mode by default for both ADC1,ADC2 */
	if (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL)
		hsdcs_adc_low_pow_conf(gul_dev, ENABLE_LOWPWR, TBGEN_STORE);
/*
 * Read the current pc value, if the FW version goes wrong upon
 * multiple reboots.Enable below code, to debug the issue.
 */
#ifdef HSDCS_FW_VER_DEBUG
	retries = HSDCS_RETRIES;
	val = readl(&regs->db_dcs_current_pc);
	while ((!val) && retries) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(
					HSDCS_TIMEOUT));
		retries--;
		dma_rmb();
		val = readl(&regs->db_dcs_current_pc);
	}

	if (val)
		dev_info(gul_dev->dev, "DCS HS: DB DCSCORE Current PC = 0x%x\n",
				val);
	else {
		dev_info(gul_dev->dev, "DCS HS: DB DCSCORE Current PC Failed.\n");
		ret =  -ENOEXEC;
		goto out;
	}
#endif /* HSDCS_FW_VER_DEBUG */

	if (gul_ep_get_soc_rev() == GEUL_SVR_REVA_VAL) {
		/*Disable 16ADC */
		switch (hsadc_mask) {
		case 0x1:
			writel(ADC_ENABLE_1, &regs->adc_enable_ctrl);
			adc_rxi_rxq_mask = ADC_ENABLE_1;
			break;
		case 0x2:
			writel(ADC_ENABLE_2, &regs->adc_enable_ctrl);
			adc_rxi_rxq_mask = ADC_ENABLE_1;
			break;
		case 0x3:
			writel(ADC_ENABLE_BOTH, &regs->adc_enable_ctrl);
			adc_rxi_rxq_mask = ADC_ENABLE_BOTH;
			break;
		default:
			dev_err(gul_dev->dev, "\nDCS HS: hsadc mask Invalid 0x%x C !!\n",
				hsadc_mask);
			ret = -EBUSY;
			goto out;
		}
		dev_info(gul_dev->dev, "%s:ADC en ctrl 0x%x\n",
			__func__, readl(&regs->adc_enable_ctrl));
	}

out:
	return ret;
}

void hsdcs_adc_set_tmon_tri(void)
{
#if HSDCS_TMON_THREAD_ENABLED
	hsdcs_tmon_modem_trigger_inc_recal = 1;
#endif
}

void hsdcs_adc_set_tmon_tri_full(void)
{
#if HSDCS_TMON_THREAD_ENABLED
	hsdcs_tmon_modem_trigger_full_recal = 1;
#endif
}

void hsdcs_adc_en_dis_fullcal(struct dcs_dev *dcsdev, u32 val)
{
	dcsdev->hsdcs_full_recal_enabled = val;
}

void hsdcs_adc_en_dis_inc_cal(struct dcs_dev *dcsdev, u32 val)
{
	dcsdev->hsdcs_inc_recal_enabled = val;
}

int hsdcs_adc_fast_recal(struct gul_dev *gul_dev, u32 chan)
{
	struct dcs_dev *dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct hsdcs_regs_map *regs =
		(struct hsdcs_regs_map *) dcsdev->hsdcs_regs;
	u32 oval, val = 0, retries = HSDCS_RETRIES;
	int ret = 0;
	u32 cal1, cal2;

	hsdcs_adc_low_pow_conf(gul_dev, DISABLE_LOWPWR, TBGEN_STORE);
	START_TIME
	val = readl(&regs->dcs_mailbox_reg2_resp);
	cal1 = (val & 0xffff0000);

	oval = readl(&regs->dcs_mailbox_reg1);
	oval &= HSADC_FAST_RECAL_CLEAR_MASK;

	val = (chan << HSADC_RECALIBRATION_SELECT);
	val |= HSADC_START_RECALIBRATION;
	oval |= val;
	HSDCS_CLEAR_BIT_POS(oval, HSADC_CAL_BIT_POS19);// 0 Incremental recal select, FW 2.20.0
	HSDCS_CLEAR_BIT_POS(oval, HSADC_CAL_BIT_POS20);
	hsdcs_note_tstamp_event(gul_dev, "Incremental/fast recal, set mbox_reg1 bit19 & 20");
	writel(oval, &regs->dcs_mailbox_reg1);
	dma_wmb();

	retries = HSDCS_RETRIES;
	do {
		hsdcs_note_tstamp_event(gul_dev, "get reg2_resp");
		val = readl(&regs->dcs_mailbox_reg2_resp);
		cal2 = (val & 0xffff0000);
		if (cal2 > cal1) {
			break;
		}
		mdelay(HSDCS_TIMEOUT_MS);
		retries--;
		dma_rmb();
		dev_dbg(gul_dev->dev, "%s: cal1: 0x%x , cal2: 0x%x , retry: %d\n",
			__func__, cal1, cal2, retries);
	} while (retries);

	if (cal2 > cal1) {
		hsdcs_note_tstamp_event(gul_dev, "get mbox_reg1");
		val = readl(&regs->dcs_mailbox_reg1);
		val &= ~(val & HSADC_START_RECALIBRATION);
		writel(val, &regs->dcs_mailbox_reg1);
		hsdcs_note_tstamp_event(gul_dev, "set mbox_reg1");
		dma_wmb();
	} else {
		dev_err(gul_dev->dev, "%s:ADC Calibration Failed, 0x%x\n",
			__func__, val);
		ret = -EBUSY;
	}
	STOP_TIME
	hsdcs_adc_low_pow_conf(gul_dev, ENABLE_LOWPWR, TBGEN_RESTORE);
	hsdcs_note_tstamp_event(gul_dev, NULL);
	dcsdev->dcs_info.fastrecal_cnt++;
	return ret;
}


static void hsdcs_init_clk(struct dcs_dev *dcs_dev)
{
	struct hsdcs_regs_map *regs = dcs_dev->hsdcs_regs;
	struct gul_dev *gul_dev = dcs_dev->gul_dev;

	/*Enable IP & clk_d1, clk_d2*/
	writel(CLK_CTRL_REG_VAL, &regs->clk_ctrl);
	dev_dbg(gul_dev->dev, "%s: clk_ctrl(0x%px) 0x%x\n", __func__,
		&regs->clk_ctrl, readl(&regs->clk_ctrl));

	dma_wmb();
	if (tbgen2_disable == 0)
		SET_HIF_HOST_RDY(gul_dev->hif, HIF_HOST_READY_HS_TBGEN2);
}

static int hsdcs_dac1_dac2_rst(struct dcs_dev *dcs_dev)
{
	struct hsdcs_regs_map *hsdcs_regs = dcs_dev->hsdcs_regs;
	struct gul_hif *hif = dcs_dev->gul_dev->hif;
	struct gul_dev *gul_dev = dcs_dev->gul_dev;

	/* Send iqa_bus_request */
	writel(0x01, &hsdcs_regs->adc_dac_reg_ctrl);
	udelay(100);

	/* Wait for iqa_bus_grant */
	if (!readl(&hsdcs_regs->adc_dac_reg_gr_status)) {
		dev_err(gul_dev->dev, "%s:bus access denied\n", __func__);
		return -EBUSY;
	}

	if (gul_ep_get_soc_rev() == GEUL_SVR_REVA_VAL) {
		/*Reset DAC1 Digital Core */
		writel(0x06, &hsdcs_regs->dac1_rst_ctrl);
		dma_wmb();
		dev_dbg(gul_dev->dev, "%s:Dac1 RST value  %x\n",
			__func__, readl(&hsdcs_regs->dac1_rst_ctrl));
		writel(0x07, &hsdcs_regs->dac1_rst_ctrl);
		dma_wmb();

		/*Reset DAC2 Digital Core */
		writel(0x06, &hsdcs_regs->dac2_rst_ctrl);
		dma_wmb();
		dev_dbg(gul_dev->dev, "%s:Dac2 RST value %x\n",
			__func__, readl(&hsdcs_regs->dac2_rst_ctrl));
		writel(0x07, &hsdcs_regs->dac2_rst_ctrl);
		dma_wmb();
	}

	/* Release iqa_bus_request */
	writel(0x0, &hsdcs_regs->adc_dac_reg_ctrl);
	dma_wmb();
	udelay(100);

	/* Wait for iqa_bus_grant release */
	if (readl(&hsdcs_regs->adc_dac_reg_gr_status)) {
		dev_err(gul_dev->dev,
			"%s:adc_dac_reg_gr_status bus not freed\n", __func__);
		return -EBUSY;
	}

	return 0;
}

/*
   hsdcs_init_dac(): this function enables the HSDACs based on input
   hsdac_mask argument to yami. If the mask is unacceptable, this function
   disables both HSDACs
*/
static int hsdcs_init_dac(struct dcs_dev *dcs_dev)
{
	struct hsdcs_regs_map *regs = dcs_dev->hsdcs_regs;
	struct gul_dev *gul_dev = dcs_dev->gul_dev;
	int ret = 0;

	dev_info(gul_dev->dev, "DCS HS:Init ADC Mask 0x%x, DAC Mask: 0x%x\n",
			hsadc_mask, hsdac_mask);

	dac_txi_txq_mask = DAC_ENABLE_NONE;

	switch (hsdac_mask) {
	case 0x0:
		dev_info(gul_dev->dev, "HSDAC0 & HSDAC1 are being disabled\n");
		break;
	case 0x1:
		dev_info(gul_dev->dev,
		"HSDAC0 will be enabled, HSDAC1 will be disabled\n");
		dac_txi_txq_mask = DAC_ENABLE_1;
		break;
	case 0x2:
		dev_info(gul_dev->dev,
		"HSDAC0 will be disabled, HSDAC1 will be enabled\n");
		dac_txi_txq_mask = DAC_ENABLE_2;
		break;
	case 0x3:
		dev_info(gul_dev->dev, "HSDAC0 & HSDAC1 are being enabled\n");
		dac_txi_txq_mask = DAC_ENABLE_BOTH;
		break;
	default:
		dev_err(gul_dev->dev, "Error!! Invalid hsdac mask 0x%x !!\n",
			hsdac_mask);
		ret = -EINVAL;
	}
	writel(dac_txi_txq_mask, &regs->dac_enable_ctrl);
	return ret;
}

static int hsdcs_init_adc_dac(struct dcs_dev *dcs_dev)
{
	struct hsdcs_regs_map *regs = dcs_dev->hsdcs_regs;
	struct gul_dev *gul_dev = dcs_dev->gul_dev;
	u32 val, retries = HSDCS_RETRIES;
	int ret = 0;

	/*Enable IP & clk_d1, clk_d2*/
	writel(0xeb, &regs->clk_ctrl);
	dev_dbg(gul_dev->dev, "%s: clk_ctrl(0x%px) 0x%x\n", __func__,
		&regs->clk_ctrl, readl(&regs->clk_ctrl));

	/*Enable DAC*/
	ret = hsdcs_init_dac(dcs_dev);
	if (ret)
		goto out;
	mdelay(100);
	/*Enable ADC*/
	ret = hsdcs_init_adc(dcs_dev);
	if (ret)
		goto out;

	dma_wmb();

	val = readl(&regs->dac_ready_stat);
	while (retries--) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(
					HSDCS_TIMEOUT));
		dma_rmb();
		val = readl(&regs->dac_ready_stat);
		if ((val & dac_txi_txq_mask) == dac_txi_txq_mask)
			break;
	}
	if ((val & dac_txi_txq_mask) != dac_txi_txq_mask) {
		dev_info(gul_dev->dev, "%s:DAC Enable failed, 0x%x\n",
			__func__, val);
		ret = -EBUSY;
		goto out;
	}

	mdelay(100);
	ret = hsdcs_dac1_dac2_rst(dcs_dev);
out:
	return ret;
}

static int hsdcs_out_of_reset(struct gul_dev *gul_dev)
{
	uint32_t *scfg_config_ctrl6 = NULL;
	uint32_t val = 0;

	/* Set SCFG Registers Base Address */
	scfg_config_ctrl6 = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL6_OFFSET);
	if (!scfg_config_ctrl6) {
		dev_err(gul_dev->dev, "DCS HS: SCFG register access failed\n");
		return -ENODEV;
	}

/* Take HS_DCS subsystem out of Reset */
	val = readl(scfg_config_ctrl6);
	val |= HS_DCS_OUT_OF_RESET;
	dev_dbg(gul_dev->dev, "DCS HS: SCFG_CONFIG_CTRL6 value = 0x%x\n",
			val);

	writel(val, scfg_config_ctrl6);
	dev_dbg(gul_dev->dev, "DCS HS: After taking out of reset scfg_ctrl6 = 0x%x\n",
			readl(scfg_config_ctrl6));
	return 0;
}

static int hsdcs_sps_config(struct gul_dev *gul_dev)
{
	uint32_t *dcs_pll_addr = NULL;
	uint32_t pll_val = 0, sps_val = 0;

	dcs_pll_addr = (uint32_t *) (gul_dev->mem_regions
		[GUL_MEM_REGION_CCSR].vaddr +
		DCS_CLK_GEN_OFFSET + DCS_PLLCR0_OFFSET);

	pll_val = readl(dcs_pll_addr);
	sps_val = hsdcs_sps;

	switch (sps_val) {
	case HSDCS_SPS_983:
		pll_val |= (1 << 14);
		pll_val |= (1 << 15);
		break;
	case HSDCS_SPS_1966:
		pll_val &= ~(1 << 14);
		pll_val |= (1 << 15);
		break;
	default:
		dev_err(gul_dev->dev,
		"DCS HS:ERROR: Unsupported DCS sps %d !!\n", sps_val);
		return -1;
	}
	gul_dev->hif->hsdcs_sps = sps_val;
	writel(pll_val, (void *)((char *)dcs_pll_addr));
	return 0;
}

static void hsdcs_scfg_config(struct gul_dev *gul_dev)
{
	uint32_t val, *scfg_config_ctrl = NULL;
	struct gul_hif *hif = gul_dev->hif;

	if (hsdcs_enable && gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL) {
		/* ctrl 2 */
		scfg_config_ctrl = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_REG_OFFSET + SCFG_CONFIG_CTRL2_OFFSET);
		val = readl(scfg_config_ctrl);
		val |= HS_DCS_SRAM_CLK_EN;
		writel(val, scfg_config_ctrl);

		hsdcs_out_of_reset(gul_dev);
		hsdcs_enable_pclk_axiq(gul_dev);
	}

#if DEBUG_SHOW_SCFG_REG_CONFIG
	show_scfg_cfg_ctrl_regs(gul_dev);
#endif
}

static int hsdcs_macro_init(struct gul_dev *gul_dev)
{
	struct dcs_dev *dcs_dev = (struct dcs_dev *) gul_dev->dcs_priv;
	struct hsdcs_regs_map *hsdcs_regs = NULL;
	struct gul_hif *hif = gul_dev->hif;
	u32 retries = HSDCS_RETRIES;
	u32 id = 0, rw = 0, val = 0, fwmaj = 0, fwmin = 0, fwsub = 0;
	int rc;

	/* Get CCSR Address */
	hsdcs_regs = (struct hsdcs_regs_map *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			DCS_HS_CCSR_OFFSET);
	if (!hsdcs_regs) {
		dev_err(gul_dev->dev, "DCS: CCSR Not Initialized\n");
		kfree(dcs_dev);
		return -ENODEV;
	}

	dev_dbg(gul_dev->dev, "%s: ccsr 0x%px, dcs 0x%px, dcs offset 0x%x\n",
		__func__, gul_dev->mem_regions[GUL_MEM_REGION_CCSR].vaddr,
		hsdcs_regs, DCS_HS_CCSR_OFFSET);
	dcs_dev->hsdcs_regs = hsdcs_regs;

	/* Enable CLKs */
	hsdcs_init_clk(dcs_dev);
	if (!hsdcs_enable)
		return 1;

	if (warmup_flag[gul_dev->id])
		return 1;

	if (gul_ep_get_hsdcs_type() == GEUL_SVR_HSDCS_NO) {
		dev_err(gul_dev->dev,
			"DCS HS: Not supported on Modem(SVR 0x%x)\n",
			gul_ep_get_svr());
		return 1;
	}

	if (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL)
		sprintf(dcs_dev->name, "%s", DCS_HS_FIRMWARE_NAME_B0);
	else
		sprintf(dcs_dev->name, "%s", DCS_HS_FIRMWARE_NAME);

	id = readl(&hsdcs_regs->ip_ver_info);
	rw = readl(&hsdcs_regs->wr_rw_test_reg);
	dev_dbg(gul_dev->dev, "%s: id - 0x%x, rw_test - 0x%x\n", __func__,
		id, rw);
	rw = DCS_HS_MAGIC;
	writel(rw, &hsdcs_regs->wr_rw_test_reg);
	rw = readl(&hsdcs_regs->wr_rw_test_reg);
	if (rw == DCS_HS_MAGIC) {
		dev_dbg(gul_dev->dev, "DCS HS: rw test fine (0x%x)\n", rw);
	} else {
		dev_err(gul_dev->dev, "%s: DCS HS: rw test failed (0x%x)\n",
			__func__, rw);
		return -ENODEV;
	}

	dev_dbg(gul_dev->dev, "%s: id 0x%px\n", __func__, &hsdcs_regs->ip_ver_info);

#if DEM_ENABLE_CONFIG
	if (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL) {
		/* set current */
		rw = readl(&hsdcs_regs->dcs_mailbox_reg0);
		rw |= HS_DCS_DAC_CURR_12MA; // Enabled by default from FW 2.9
		rw |= HS_DCS_DAC_DEM_EN; // Enabled by default from FW 2.14
		writel(rw, &hsdcs_regs->dcs_mailbox_reg0);
		rw = readl(&hsdcs_regs->dcs_mailbox_reg0);
		dev_dbg(gul_dev->dev, "%s:DCS: Cntrl Reg 0x38: 0x%x\n",
				__func__, rw);
	}
#endif

	/* intialize the FW version */
	writel(0, &hsdcs_regs->fw_ver_info);

	if (hsdcs_write_firmware(gul_dev)) {
		dev_err(gul_dev->dev, "DCS HS: Firmware Loading Failed\n");
		hsdcs_regs = NULL;
		kfree(dcs_dev);
		return -ENOMEM;
	}
	dma_wmb();
	udelay(500);

	val = readl(&hsdcs_regs->fw_ver_info);
	while ((!val) && retries) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(
					HSDCS_TIMEOUT));
		retries--;
		dma_rmb();
		val = readl(&hsdcs_regs->fw_ver_info);
	}

	if (val) {
		fwmaj = ((val >> HSDCS_SHIFT_2BYTE) & HSDCS_BYTE_MASK);
		fwmin = ((val >> HSDCS_SHIFT_1BYTE) & HSDCS_BYTE_MASK);
		fwsub = (val & HSDCS_BYTE_MASK);
		dev_info(gul_dev->dev, "DCS HS: Firmware Version = %d.%d.%d\n",
		   fwmaj, fwmin, fwsub);
	} else {
		dev_err(gul_dev->dev, "DCS HS: Firmware Ver Read Failed\n");
		if (gul_ep_get_soc_rev() != GEUL_SVR_REVA_VAL)
			return -ENOEXEC;
	}

	/* Check for latest the FW version */
	if (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL) {
		if ((fwmaj < HSDCS_MIN_REQUIRED_MAJOR) ||
			(fwmaj == HSDCS_MIN_REQUIRED_MAJOR && fwmin < HSDCS_MIN_REQUIRED_MINOR) ||
			(fwmaj == HSDCS_MIN_REQUIRED_MAJOR && fwmin == HSDCS_MIN_REQUIRED_MINOR
			&& fwsub < HSDCS_MIN_REQURED_SUBVER))
		{
			dev_err(gul_dev->dev, "DCS HS: Firmware Version Required [ %d.%d.%d ] or above\n",
			HSDCS_MIN_REQUIRED_MAJOR, HSDCS_MIN_REQUIRED_MINOR, HSDCS_MIN_REQURED_SUBVER);
			return -ENOEXEC;
		}
	}

	rc = hsdcs_init_adc_dac(dcs_dev);
	if (!rc)
		SET_HIF_HOST_RDY(gul_dev->hif, HIF_HOST_READY_DCS_HS);
	else
		return rc;

#if DEBUG_HSDCS_DUMP_REG
	hsdcs_dump_reg(gul_dev);
#endif
	return 0;
}


#if DCS_LS_EN
static int lsdcs_sps_config(struct gul_dev *gul_dev)
{
	switch (ls_dac_sps) {
	case LS_DAC_SPS_491:
		break;
	case LS_DAC_SPS_245:
		break;
	case LS_DAC_SPS_122:
		break;
	case LS_DAC_SPS_61:
		break;
	default:
		dev_err(gul_dev->dev, "DCS LS:ERROR: Unsupported DAC sps !!\n");
		return -1;
	}
	gul_dev->hif->ls_dac_sps = ls_dac_sps;
	dev_info(gul_dev->dev, "DCS LS: dac sps: %d\n", ls_dac_sps);

	switch (ls_adc_sps) {
	case LS_ADC_SPS_245:
		break;
	case LS_ADC_SPS_122:
		break;
	case LS_ADC_SPS_61:
		break;
	default:
		dev_err(gul_dev->dev, "DCS LS:ERROR: Unsupported ADC sps !!\n");
		return -1;
	}
	gul_dev->hif->ls_adc_sps = ls_adc_sps;
	dev_info(gul_dev->dev, "DCS LS: adc sps: %d\n", ls_adc_sps);

	return 0;
}

static int lsdcs_enable(struct gul_dev *gul_dev)
{
	struct gul_hif *hif = gul_dev->hif;
	uint32_t *scfg_base, val = 0;

	/* Set SCFG Registers Base Address */
	scfg_base = (uint32_t *) (gul_dev->mem_regions
				  [GUL_MEM_REGION_CCSR].vaddr + SCFG_REG_OFFSET);
	if (!scfg_base) {
		dev_err(gul_dev->dev, "DCS LS: SCFG register access failed\n");
		return -ENODEV;
	}

	if (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL) {
		/* Check for DAC/ADC sps configuration */
		if (lsdcs_sps_config(gul_dev))
			return -1;

		/* llCP clk plat/2 */
		val = readl((char *) scfg_base + SCFG_CONFIG_CTRL1_OFFSET);
		val |= LS_LLCP_PLAT_CLKDIV2;
		writel(val,
		       (void *)((char *)scfg_base + SCFG_CONFIG_CTRL1_OFFSET));

		val = readl((char *) scfg_base + SCFG_CONFIG_CTRL5_OFFSET);
		val |= ENABLE_ALL_CONFIG_CTRL5;
		writel(val,
		       (void *)((char *)scfg_base + SCFG_CONFIG_CTRL5_OFFSET));

		val = ENABLE_DEFAULT_CONFIG_CTRL0;
		writel(val,
		       (void *)((char *)scfg_base + SCFG_CONFIG_CTRL0_OFFSET));

	} else {
		val = readl((char *) scfg_base + SCFG_CONFIG_CTRL6_OFFSET);
		val &= (uint32_t) ~(LS0_DCS_PCLK_DIS | LS1_DCS_PCLK_DIS |
				    LS0_AXIQ_TX_CLK_DIS | LS1_AXIQ_TX_CLK_DIS |
				    LS0_AXIQ_RX_CLK_DIS | LS1_AXIQ_RX_CLK_DIS);
		writel(val,
		       (void *)((char *)scfg_base + SCFG_CONFIG_CTRL6_OFFSET));

		dev_dbg(gul_dev->dev, "%s: Config_ctrl_1=0x%x\n", __func__,
			readl((char *) scfg_base + SCFG_CONFIG_CTRL1_OFFSET));
		dev_dbg(gul_dev->dev, "%s: Config_ctrl_6=0x%x\n", __func__,
			readl((char *) scfg_base + SCFG_CONFIG_CTRL6_OFFSET));
		writel(ENABLE_ALL_CONFIG_CTRL5,
		       (void *)(char *) scfg_base + SCFG_CONFIG_CTRL5_OFFSET);
	}

	if (!lsdcs_disable) {
		if ((lsdac_mask | 0xf) == 0xf) {
			hif->lsdac_mask = lsdac_mask;
		} else {
			dev_err(gul_dev->dev,
			"Invalid LS DAC mask value 0x%x!!!\n", lsdac_mask);
			return -1; // Return an error code
		}
		if ((lsadc_mask | 0xf) == 0xf) {
			hif->lsadc_mask = lsadc_mask;
		} else {
			dev_err(gul_dev->dev,
			"Invalid LS ADC mask value 0x%x!!!\n", lsadc_mask);
			return -1; // Return an error code
		}
		dev_info(gul_dev->dev,
			"DCS LS: %s DAC Mask: 0x%x, ADC Mask 0x%x\n",
			__func__, lsdac_mask, lsadc_mask);
		SET_HIF_HOST_RDY(gul_dev->hif, HIF_HOST_READY_DCS_LS);
	}
	return 0;
}
#endif /* DCS_LS_EN */

#if DCS_PLL_CHK_ENABLE
static int dcs_pll_check(struct gul_dev *gul_dev)
{
	int retry_count = DCS_PLL_WAIT_TIMEOUT;
	uint32_t *dcs_pll_status = NULL;
	uint32_t pll_status = 0;

	/* Get DCS Clock Generation Registers Base Address */
	dcs_pll_status = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			DCS_CLK_GEN_OFFSET + DCS_PLLRSTCTL_OFFSET);
	if (!dcs_pll_status) {
		dev_err(gul_dev->dev, "DCS: CLK GEN register access failed\n");
		return -ENODEV;
	}
	/* Checking for DCS_PLL locking Status */
	while (retry_count--) {
		pll_status = readl(dcs_pll_status);
		if (pll_status & DCS_PLL_STATUS_CHK) {
			dev_info(gul_dev->dev, "DCS: PLL is locked\n");
			return 0;
		}
		udelay(10); /* 10 micro second delay */
	}
	dev_info(gul_dev->dev,
		"DCS: PLL not locked, PLLRSTCTRL = 0x%x\n", pll_status);
	return -EPERM;
}
#endif

static int dcs_pll_reconf(struct gul_dev *gul_dev)
{
	int retry_count = DCS_PLL_WAIT_TIMEOUT;
	uint32_t *dcs_pll_addr = NULL;
	uint32_t pll_val = 0;

	dcs_pll_addr = (uint32_t *) (gul_dev->mem_regions
		[GUL_MEM_REGION_CCSR].vaddr +
		DCS_CLK_GEN_OFFSET);

	/* only bit-26 PLLRSTCTL[STP_REQ] need to be set */
	/* stop PLL, STP_REQ=1 */
	writel(DCS_PLL_RECONF_SEQ_VAL_400,
		(void *)((char *)dcs_pll_addr + DCS_PLLRSTCTL_OFFSET));

	/* Wait for the STP_REQ to be cleared by the hardware */
	pll_val = readl((char *)(dcs_pll_addr + DCS_PLLRSTCTL_OFFSET));
	while (retry_count--) {
		if (!(pll_val & DCS_PLL_STP_REQ))
			break;
	    udelay(1000); /* 1 ms delay */
	    pll_val = readl((char *)(dcs_pll_addr + DCS_PLLRSTCTL_OFFSET));
	}
	if ((pll_val & DCS_PLL_STP_REQ) && retry_count) {
		dev_info(gul_dev->dev,
			"DCS: PLL STP REQ not cleared: %x\n", pll_val);
		return -EBUSY;
	}

	/* Change the desired PLL setting VCO, post-divider etc. */
	writel(DCS_PLL_RECONF_SEQ_VAL_408,
		(void *)((char *)dcs_pll_addr + DCS_PLLCR1_OFFSET));
	writel(DCS_PLL_RECONF_SEQ_VAL_41C,
		(void *)((char *)dcs_pll_addr + DCS_PLLCR6_OFFSET));
	writel(DCS_PLL_RECONF_SEQ_VAL_4D0,
		(void *)((char *)dcs_pll_addr + DCS_PLLCR10_OFFSET));
	writel(DCS_PLL_RECONF_SEQ_VAL_4D4,
		(void *)((char *)dcs_pll_addr + DCS_PLLCR11_OFFSET));

	/* Enable the PLL PLLRSTCTL[RST_REQ]=1 */
	writel(DCS_PLL_RECONF_SEQ_VAL2_400,
		(void *)((char *)dcs_pll_addr + DCS_PLLRSTCTL_OFFSET));

	return 0;
}

/*
 * Even though DCS PLL is initialized by sampling POR CFG inputs,
 * PLL status check and reconfiguration is done from this function.
 *
 */
static int dcs_pll_init(struct gul_dev *gul_dev)
{
	struct gul_hif *hif = gul_dev->hif;

#if DCS_PLL_CHK_ENABLE
	/* DCS PLL lock status check */
	if (dcs_pll_check(gul_dev)) {
		dev_err(gul_dev->dev, "DCS: PLL is not locked\n");
		return -ECANCELED;
	}
#endif
	mdelay(10);

	if (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL) {
		if (hsdcs_sps_config(gul_dev)) {
			dev_err(gul_dev->dev, "HSDCS: SPS configuration failed\n");
			return -ECANCELED;
		}

		/* DCS PLL RST and Reconfigure */
		if (dcs_pll_reconf(gul_dev)) {
			dev_err(gul_dev->dev, "DCS: PLL reconfigure failed\n");
			return -ECANCELED;
		}
		dev_info(gul_dev->dev, "DCS HS: sps: %d\n", hif->hsdcs_sps);
	}
	return 0;
}

void hsdcs_raise_modem_cal_req(struct gul_dev *gul_dev, int caltype)
{
	u32 i, ret, eventid_rcv;
	struct gul_hif *hif = gul_dev->hif;

	switch (caltype) {
		case HSDCS_DO_INC_RECAL:
			hif->dcshif.eventid = HSADC_EVENT_RECAL_REQUESTED_ANT0;
			break;
		case HSDCS_DO_FULL_RECAL:
			hif->dcshif.eventid = HSADC_EVENT_RECAL_REQUESTED_ANT0_1;
			break;
		default:
			dev_info(gul_dev->dev, "DCS HS: Invalid Cal type (%d)\n", caltype);
			break;
	}
	/* Raise MSI interrupt to Modem */
	dma_wmb();
	raise_modem_msi(gul_dev, MSI_TYPE_A, HOST_DCS_TMON_MSIA_7);

	for (i = 0; i <= HSDCS_TMON_RETRY_COUNT; i++) {
		eventid_rcv = hif->dcshif.eventid;

		switch (eventid_rcv) {
			case HSADC_EVENT_RECAL_REQUESTED_ANT0:
			case HSADC_EVENT_RECAL_REQUESTED_ANT1:
			case HSADC_EVENT_RECAL_REQUESTED_ANT0_1:
			case HSADC_EVENT_RECAL_FAILED_ANT0:
			case HSADC_EVENT_RECAL_FAILED_ANT1:
			case HSADC_EVENT_RECAL_FAILED_ANT0_1:
			break;

			case HSADC_EVENT_RECAL_REQ_ACK_ANT0:
				{
					ret = hsdcs_adc_fast_recal(gul_dev, HSDCS_ADC0_IQ_CHAN_MASK);
					if (ret) {
						dev_err(gul_dev->dev, "DCS HS: Inc Recal ANT0 failed. %d\n", ret);
						hif->dcshif.eventid = HSADC_EVENT_RECAL_FAILED_ANT0;
					} else {
						hif->dcshif.eventid = HSADC_EVENT_RECAL_REQUESTED_ANT1;
					}
					dma_wmb();
					raise_modem_msi(gul_dev, MSI_TYPE_A, HOST_DCS_TMON_MSIA_7);
					break;
			}
			case HSADC_EVENT_RECAL_REQ_ACK_ANT1:
				{
					ret = hsdcs_adc_fast_recal(gul_dev, HSDCS_ADC1_IQ_CHAN_MASK);
					if (ret) {
						dev_err(gul_dev->dev, "DCS HS: Inc Recal ANT1 failed. %d\n", ret);
						hif->dcshif.eventid = HSADC_EVENT_RECAL_FAILED_ANT1;
					} else {
						hif->dcshif.eventid = HSADC_EVENT_RECAL_COMPLETED;
					}
					dma_wmb();
					raise_modem_msi(gul_dev, MSI_TYPE_A, HOST_DCS_TMON_MSIA_7);
					break;
			}
			case HSADC_EVENT_RECAL_REQ_DENIED:
				dev_info(gul_dev->dev, "DCS HS: Tmon L1 denied the Req\n");
			break;

			case HSADC_EVENT_RECAL_COMPLETED:
				dev_dbg(gul_dev->dev, "%s():DCS HS: Inc Recal Completed\n\n", __func__);
				return;
			break;

			case HSADC_EVENT_RECAL_COMPLETED2:
				dev_dbg(gul_dev->dev, "%s():DCS HS: Full Recal Completed\n\n", __func__);
				return;
			break;

			case HSADC_EVENT_RECAL_REQ_ACK_ANT0_1:
				{
					ret = hsdcs_adc_full_recal(gul_dev);
					if (ret) {
						dev_err(gul_dev->dev, "DCS HS: full Recal failed. %d\n", ret);
						hif->dcshif.eventid = HSADC_EVENT_RECAL_FAILED_ANT0_1;
					} else {
						hif->dcshif.eventid = HSADC_EVENT_RECAL_COMPLETED2;
					}
					dma_wmb();
					raise_modem_msi(gul_dev, MSI_TYPE_A, HOST_DCS_TMON_MSIA_7);
					break;
			}
			default:
				dev_info(gul_dev->dev, "DCS HS: Tmon Event Invalid : 0x%x\n", eventid_rcv);
			break;
		}
		mdelay(HSDCS_TMON_RETRY_WAIT);
		i++;
	}
}

#if HSDCS_TMON_THREAD_ENABLED
static int hsdcs_tempmonitor(void *data)
{
	struct gul_dev *gul_dev = NULL;
	struct dcs_dev *dcsdev = NULL;
	struct tvd_dev *gtvd_dev = NULL;
	int i = 0;
	int32_t mtd_temp = 0;
#if HSDCS_MONITOR_CHIP_TEMPERATURE
	int iCurrTemp;
#endif

	allow_signal(SIGTERM);
	while (1) {

		if (kthread_should_stop())
		break;

		ssleep(HSDCS_TMON_TEMP_POLL_TIMER_T1);

		for (i = 0; i < MAX_MODEM; i++) {
			rcu_read_lock();
			gul_dev = rcu_dereference(dev_list[i]);

			if (gul_dev == NULL) {
				rcu_read_unlock();
				continue;
			}
			dcsdev = (struct dcs_dev *) gul_dev->dcs_priv;
			if (dcsdev == NULL) {
				rcu_read_unlock();
				continue;
			}
			gtvd_dev = (struct tvd_dev *) gul_dev->tvd_priv;
			if (gtvd_dev == NULL) {
				rcu_read_unlock();
				continue;
			}

#if HSDCS_THERMAL_COMPENSATION
			hsdcs_write_temp_compensation(gul_dev);
#endif

#if HSDCS_MONITOR_CHIP_TEMPERATURE
			if (hsdcs_get_curr_temperature(gul_dev, i, &mtd_temp, "temp-monitor") < 0) {
				dev_err(gul_dev->dev, "%s(): Failed to get temperature for modem %d!!\n",  __func__, i);
				continue;
			}

			iCurrTemp = dcsdev->ihsdcs_current_temp = mtd_temp;
			/* Check for the Recalibration */
			{
				if (dcsdev->hsdcs_full_recal_enabled)
				{
					/* check and do full recal */
					if ((iCurrTemp != dcsdev->ihsdcs_lastime_full_cal_temp) || hsdcs_tmon_modem_trigger_full_recal) {
						/* check for the change in junction temperature */
						if ((abs(iCurrTemp - dcsdev->ihsdcs_lastime_full_cal_temp) >= THRESHOLD_FULL_RECAL) ||
							hsdcs_tmon_modem_trigger_full_recal) {
							dev_dbg(gul_dev->dev, "Fullcal temp:  %d, %d , abs:%d,\n", iCurrTemp,
								dcsdev->ihsdcs_lastime_full_cal_temp, abs(iCurrTemp - dcsdev->ihsdcs_lastime_full_cal_temp));
							hsdcs_raise_modem_cal_req(gul_dev, HSDCS_DO_FULL_RECAL);
							/* update the current temp */
							dcsdev->ihsdcs_lastime_full_cal_temp = iCurrTemp;
							/* update the inc cal temp */
							dcsdev->ihsdcs_lastime_inc_cal_temp = iCurrTemp;
							/* reset the modem trigger flag */
							hsdcs_tmon_modem_trigger_full_recal = 0;
						}
					}
				}

				if (dcsdev->hsdcs_inc_recal_enabled)
				{
					/* check and do inc recal */
					if ((iCurrTemp != dcsdev->ihsdcs_lastime_inc_cal_temp) || hsdcs_tmon_modem_trigger_inc_recal) {
						/* check for the change in junction temperature */
						if ((abs(iCurrTemp - dcsdev->ihsdcs_lastime_inc_cal_temp) >= THRESHOLD_INCREMENTAL_RECAL) ||
							hsdcs_tmon_modem_trigger_inc_recal) {
							dev_dbg(gul_dev->dev, "Inc cal: %d, %d , abs:%d,\n", iCurrTemp,
								dcsdev->ihsdcs_lastime_inc_cal_temp, abs(iCurrTemp - dcsdev->ihsdcs_lastime_inc_cal_temp));
							hsdcs_raise_modem_cal_req(gul_dev, HSDCS_DO_INC_RECAL);
							/* update the current temp */
							dcsdev->ihsdcs_lastime_inc_cal_temp = iCurrTemp;
							/* reset the modem trigger flag */
							hsdcs_tmon_modem_trigger_inc_recal = 0;
						}
					}
				}
			}
#endif /* HSDCS_MONITOR_CHIP_TEMPERATURE */
			rcu_read_unlock();
		} /* for (i = 0; i < MAX_MODEM; i++) */
	} /* while (1) */
	return 0;
}
#endif /* HSDCS_TMON_THREAD_ENABLED */

int dcs_probe(struct gul_dev *gul_dev, int vspa_irq_count,
		struct virq_evt_map *vspa_virq_map)
{
	struct gul_hif *hif = gul_dev->hif;
	struct dcs_dev *dcs_dev = NULL;
	int rc;
	int32_t modem_temp = 0;

	dcs_dev = kzalloc(sizeof(struct dcs_dev), GFP_KERNEL);
	if (unlikely(!dcs_dev)) {
		dev_err(gul_dev->dev, "DCS Dev : Memory allocation failed\n");
		rc = -ENOMEM;
		goto out;
	}

	gul_dev->dcs_priv = (void *) dcs_dev;
	dcs_dev->gul_dev = gul_dev;

	if (((gul_dev->id < 0) || (gul_dev->id >= MAX_MODEM)))	{
		dev_err(gul_dev->dev, " Invalid Modem ID: %d !\n", gul_dev->id);
		kfree(dcs_dev);
		return -ECANCELED;
	}

	if (dcs_pll_init(gul_dev)) {
		dev_err(gul_dev->dev, "DCS: PLL init failed\n");
		kfree(dcs_dev);
		rc = -ECANCELED;
		goto out;
	}

	/* RevA SoC can not fallback to Platform clock for TBGEN2
	 * Or RevA only HSDCS enabled SoC should use the HSDCS TBGEN2)
	 */
	if ((gul_ep_get_soc_rev() == GEUL_SVR_REVA_VAL)
		|| (gul_ep_get_hsdcs_type() != GEUL_SVR_HSDCS_NO)) {
#if ENABLE_PCLK_AXIQ_CLK
		/* Enable PCLK and AXIQ_H */
		if (hsdcs_enable_pclk_axiq(gul_dev)) {
			dev_err(gul_dev->dev, "DCS HS: PClk and AXIQ_H enable Failed\n");
			kfree(dcs_dev);
			rc = -ECANCELED;
			goto out;
		}
#endif /* ENABLE_PCLK_AXIQ_CLK */
	}

#if BNRG_DCS
	if ((!hsdcs_enable) || (gul_ep_get_soc_rev() == GEUL_SVR_REVA_VAL)
		|| (gul_get_host_board_rev() == 'C') || (gul_get_host_board_rev() == 'F'))
#endif
	{
#if DCS_LS_EN
	/*Enable DCS LS1 & LS2 PLL and AXIQ */
	if (lsdcs_enable(gul_dev)) {
		dev_err(gul_dev->dev, "DCS LS: PLL & AXIQ enable Failed\n");
		kfree(dcs_dev);
		rc = -ECANCELED;
		goto out;
	}
	g_gul_global[gul_dev->id].lsdcs_load_status = 1;
#endif
	}

	rc = hsdcs_out_of_reset(gul_dev);
	if (rc)
		return rc;

	rc = hsdcs_enable_pclk_axiq(gul_dev);
	if (rc)
		return rc;

	mdelay(100);

	hsdcs_scfg_config(gul_dev);
	rc = hsdcs_macro_init(gul_dev);
	if (rc == 1)
		return 0;
out:
	if (rc < 0)	{
		dev_err(gul_dev->dev,
				"Error!! HS DCS init failed (%d).\n", rc);
		return rc;
	} else {
		g_gul_global[gul_dev->id].hsdcs_load_status = 1;
	}

	dcs_dev->dcs_info.fullrecal_cnt = 0;
	dcs_dev->dcs_info.fastrecal_cnt = 0;
	dcs_dev->dcs_info.temp_comp_cnt = 0;
#if HSDCS_TMON_THREAD_ENABLED
	if (hsdcs_enable && gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL)
	{
#if 0
		/* todo - irq handing is to be done */
		/* Register Async Notifications as per free IRQ */
		int index = gul_dev_get_msi(gul_dev);
		if (index >= 0 && index < GUL_MSI_MAX_CNT) {
			hif->msi_dcs = index;
		} else {
			/* Return error : No free IRQ line */
			dev_err(gul_dev->dev, "HSDCS: No free IRQ ..\n");
			return -EBUSY;
		}
		dev_dbg(gul_dev->dev, "DCS HS: MSI index =%d\n", index);
#endif
		/* todo - setting MAX_CNT - to avoid corruption in freertos*/
		hif->msi_dcs = GUL_MSI_MAX_CNT;
		if (hsdcs_get_curr_temperature(gul_dev, gul_dev->id, &modem_temp, "probe") < 0)
			return -EINVAL;
		dcs_dev->ihsdcs_lastime_inc_cal_temp = modem_temp;
		rcu_assign_pointer(dev_list[gul_dev->id], gul_dev);

	}
#endif
	return 0;
}

int dcs_remove(struct gul_dev *gul_dev)
{
#if HSDCS_TMON_THREAD_ENABLED
	struct gul_hif *hif = gul_dev->hif;

	if (hsdcs_enable && gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL)
	{
#if 0
		struct gul_hif *hif = gul_dev->hif;

		gul_dev_put_msi(gul_dev, hif->msi_dcs);
#endif
		RCU_INIT_POINTER(dev_list[gul_dev->id], NULL);
		synchronize_rcu();
	}
#endif
	kfree(gul_dev->dcs_priv);
	gul_dev->dcs_priv = NULL;

	g_gul_global[gul_dev->id].hsdcs_load_status = 0;
	g_gul_global[gul_dev->id].lsdcs_load_status = 0;
	return 0;
}

int dcs_init(void)
{
#if HSDCS_TMON_THREAD_ENABLED
	if (hsdcs_enable) {
		hsts_tmon = kthread_run(hsdcs_tempmonitor, NULL, "hsdcstmon");
		if (IS_ERR(hsts_tmon)) {
			pr_err("DCS HS: hsdcstmon Thread creation failed.\n");
			return -ENOEXEC;
		}
		pr_debug("DCS HS: hsdcstmon Kthread Created ..\n");
	}
#endif
	return 0;
}

int dcs_exit(void)
{
#if HSDCS_TMON_THREAD_ENABLED
	if (hsdcs_enable) {
		if (hsts_tmon) {
			kthread_stop(hsts_tmon);
			pr_debug("DCS HS: hsdcstmon Thread exited.\n");
		} else {
			pr_err("DCS HS: hsts_tmon is NULL");
		}
	}
#endif
	return 0;
}
