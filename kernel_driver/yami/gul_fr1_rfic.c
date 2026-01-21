/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2021-2026 NXP
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/string.h>

#include "gul_base.h"

#include "gul_modem_gpio.h"
#include "yuc_rfic_common.h"
#include "gul_pci_def.h"
#include "gul_fr1_rfic.h"
#include <linux/of_gpio.h>
#include <linux/gpio.h>

static void yuc_llcp_reg_read(struct gul_dev *gul_dev, u16 *val, u16 addr)
{
	rfic_priv_t *priv = (rfic_priv_t *)gul_dev->rfic_priv;
	/* as rfic is byte addressable over llcp so addr need to shift by 1 */
	addr = addr << 1;
	*val = llcp_read((priv->llcp_addr) + addr);
	//dev_info(gul_dev->dev, "%s:read 0x%x: 0x%x\n",__func__,addr, *val);
	return;
}

static void yuc_llcp_reg_write(struct gul_dev *gul_dev, u16 *val, u16 addr)
{
	rfic_priv_t *priv = (rfic_priv_t *)gul_dev->rfic_priv;

	addr = addr << 1;
	//dev_info(gul_dev->dev, "%s:write 0x%x: 0x%x\n",__func__,addr, *val);
	llcp_write(*val, ((priv->llcp_addr) + addr));
	return;
}

#if 0
void yuc_rfic_dump_rfic_regs(struct gul_dev *gul_dev,
			uint32_t addr, uint32_t len)
{
	uint16_t val;
	int i;

	printk("rfic_reg dump: Addr 0x%x, Len %d\n", addr, len);
	for (i = 0; i < len; i++) {
		yuc_llcp_reg_read(gul_dev, &val, addr + i);
		if (!(i % 8))
			printk("\n0x%x:", i);
		printk("%04x ", val);
	}
	printk("\n");
}

void yuc_rfic_write_rfic_reg(struct gul_dev *gul_dev,
			uint32_t addr, uint32_t val)
{
	uint16_t val_16 = (uint16_t) val;

	pr_info("rfic_reg write: Addr 0x%x, val 0x%xd\n", addr, val);
	yuc_llcp_reg_write(gul_dev, &val_16, addr);
}
#endif

#define LOG_CASE(x) \
	case x:           \
		dev_err(gul_dev->dev, "[ "#x" ]"); \
		break;

static void vPrintErrorString(struct gul_dev *gul_dev, YucRtc_t uErrWd)
{

	switch (uErrWd) {
	LOG_CASE(YUC_RTC_OK);
	LOG_CASE(YUC_RTC_CMD_UNKNOWN);
	LOG_CASE(YUC_RTC_CMD_WRONG_FLAGS);
	LOG_CASE(YUC_RTC_TOO_FEW_DATAWORDS);
	LOG_CASE(YUC_RTC_TOO_MANY_DATAWORDS);
	LOG_CASE(YUC_RTC_INVALID_PARAMETER);
	LOG_CASE(YUC_RTC_WRONG_SYS_STATE);
	LOG_CASE(YUC_RTC_PLL_UNLOCKED);
	LOG_CASE(YUC_RTC_PLL_WARNING);
	LOG_CASE(YUC_RTC_PAENV_ERROR);
	LOG_CASE(YUC_RTC_TIMEOUT);
	LOG_CASE(YUC_RTC_BOOT_FAILED);
	LOG_CASE(YUC_RTC_SET_FREQ);
	LOG_CASE(YUC_RTC_PLL_NOT_LOCKED);
	LOG_CASE(YUC_RTC_PLL_NOT_CALIBRATED);
	LOG_CASE(YUC_RTC_TRANSITION_NOT_ALLOWED);
	LOG_CASE(YUC_RTC_UNKNOWN_STATE);
	LOG_CASE(YUC_RTC_OUT_OF_RANGE);
	LOG_CASE(YUC_RTC_NOT_IMPLEMENTED);
	LOG_CASE(YUC_RTC_NOT_MOUNTED);
	LOG_CASE(YUC_RTC_REG_ACCESS_VIOLATION);
	LOG_CASE(YUC_RTC_LOOKUP_FAILED);
	LOG_CASE(YUC_RTC_WAIT_FAIL);
	LOG_CASE(YUC_RTC_CAL_FAILED);
	LOG_CASE(YUC_RTC_INTERNAL_ERROR);
	LOG_CASE(YUC_RTC_CAL_RESISTOR_FAILED);
	default:
		dev_err(gul_dev->dev, "[ YUC_RTC_UNKNOWN ]");
	break;

	}
}

static int yuc_rfic_cmd_proc(struct gul_dev *gul_dev, u32 cmd_data, u32 rw_addr,
		      u16 *words, u8 num_words)
{
	yuc_cmd_t cmd = {0};
	yuc_cmd_resp_t resp = {0};
	u32 num_read = 0;
	u16 *cmd16 = (u16 *)&cmd;
	YucRtc_t uErrWd = 0;

	dev_info(gul_dev->dev, "%s() cmd 0x%x\n", __func__, cmd_data);

	if (rw_addr == YUC_RFIC_CMD_ADDR) {
		cmd.len = num_words;
		cmd.id = cmd_data & 0x3ff;
		cmd.sd = 0;  // for future use, always set to 0
		cmd.rrq = 1; // Response request - also clears response header
		dma_wmb();

		yuc_llcp_reg_write(gul_dev, ((u16 *)&cmd), rw_addr);
		dev_info(gul_dev->dev, "cmd 0x%x, addr 0x%x, wrds %d\n", *cmd16,
			rw_addr, num_words);
		/* write header after writing command words */
		for (num_read = 1; num_read <= num_words; num_read++) {
			yuc_llcp_reg_write(gul_dev, words,
					(rw_addr + num_read));
			//dev_info(gul_dev->dev, "write wrd[%d] - 0x%x\n", num_read, *words);
			dma_wmb();
			words += 1;
		}

		dma_wmb();
		num_read = YUC_RFIC_TS_RETRY;
		while (num_read) {
			yuc_llcp_reg_read(gul_dev, ((u16 *)&resp),
					YUC_RFIC_CMD_RESP_ADDR);
			/* Wait for TF (Task Finished),indicate that command has completed */
			if (resp.tf && resp.id) {
				/* Response has arrived, now check the error bit*/
				if (resp.ew) {
					yuc_llcp_reg_read(gul_dev, ((u16 *)&uErrWd),
						YUC_RFIC_CMD_RESP_ADDR  + resp.len);
					vPrintErrorString(gul_dev, uErrWd);
					if (uErrWd != YUC_RTC_OK) {
						dev_err(gul_dev->dev,
							"%s Cmd resp not received. Timeout!!\n", __func__);
						return -EIO;
					}
				}
				return 0;
			}
			dev_info(gul_dev->dev,
				"%s Cmd resp not received num_read[%d]. Retrying..\n",
				__func__, num_read);
			num_read--;
		}

		if (!num_read && (resp.ew || !resp.id)) {
			dev_err(gul_dev->dev,
				"%s resp.id[%x] resp.ew[%x] num_read[%d]\n",
				__func__, resp.id, resp.ew, num_read);
			return -EIO;
		}
	} else {
		/* read response command */
		for (num_read = 1; num_read <= num_words; num_read++) {
			yuc_llcp_reg_read(gul_dev, words, (rw_addr + num_read));
			//dev_info(gul_dev->dev, "read wrd[%d] - 0x%x\n", num_read, *words);
			words += 1;
		}

	}
	return 0;
}

static int yuc_prog_rfic_fw(struct gul_dev *gul_dev, char *vaddr, int fw_size)
{
	char *vaddr_in = vaddr;
	char twobyte[4];
	char *line = strsep(&vaddr, "\n");
	char *ignore;
	u16 val;

	if (line == NULL) {
		dev_err(gul_dev->dev, "Invalid vaddr\n");
		return -EINVAL;
	}

	do {
		ignore = strstr(line, "//");
		if (ignore) {
			*ignore = '\0';
			line  = strsep(&vaddr, "\n");
			continue;
		}
		udelay(4);
		memcpy(twobyte, line, sizeof(twobyte));
		val = simple_strtoul(twobyte, NULL, 16);
		yuc_llcp_reg_write(gul_dev, &val, YUC_RFIC_FW_WRITE_REG);

		if (vaddr >= (vaddr_in + fw_size))
			break;

		line  = strsep(&vaddr, "\n");
	} while (line);

	dev_info(gul_dev->dev, "[%s] done\n", __func__);
	return 0;
}

static int yuc_load_rfic_img(struct gul_dev *gul_dev)
{
	int rc = 0;
	int size = YUC_RFIC_FW_SIZE, fw_size;
	u8 __iomem *vaddr;

	dev_info(gul_dev->dev, "[%s]\n", __func__);
	vaddr = vmalloc(size);
	if (vaddr == NULL) {
		dev_err(gul_dev->dev, "rfic fw malloc failed\n");
		return -ENOMEM;
	}

	rc  = gul_udev_load_firmware(gul_dev, vaddr, size,
				YUC_RFIC_PROG_FW_FILE, &fw_size);
	if (rc) {
		dev_err(gul_dev->dev, "udev Firmware [%s] request failed\n",
			 YUC_RFIC_PROG_FW_FILE);
		kvfree(vaddr);
		goto out;
	}

	dev_info(gul_dev->dev, "udev Firmware [%s] - Addr %p, size %d\n",
		YUC_RFIC_PROG_FW_FILE, vaddr, fw_size);
	rc = yuc_prog_rfic_fw(gul_dev, vaddr, fw_size);
	kvfree(vaddr);
out:
	return rc;
}

static void yuc_prog_rfic_def(struct gul_dev *gul_dev, char *vaddr, int fw_size)
{
	char *vaddr_in = vaddr;
	char *line = strsep(&vaddr, "\n");
	char *reg_offset_string;
	unsigned short reg_offset;
	char twobyte[4];
	char *ignore;
	u16 val;

	do {
	ignore = strstr(line, "//");
	if (ignore) {
		*ignore = '\0';
		line  = strsep(&vaddr, "\n");
	continue;
	}
		udelay(4);
		reg_offset_string = strsep(&line, " ");
		memcpy(twobyte, reg_offset_string, sizeof(twobyte));
		reg_offset = simple_strtoul(twobyte, NULL, 16);
		memcpy(twobyte, line, sizeof(twobyte));
		val = simple_strtoul(twobyte, NULL, 16);
		//dev_info(gul_dev->dev,"offset:0x%x val:0x%x\n",reg_offset, val);
		yuc_llcp_reg_write(gul_dev, &val, reg_offset);
		if (vaddr >= (vaddr_in + fw_size))
			break;
		line  = strsep(&vaddr, "\n");
	} while (line);

	dev_info(gul_dev->dev, "[%s] Done..\n", __func__);
	return;
}

static int yuc_load_rfic_def_reg(struct gul_dev *gul_dev, char *fw_name)
{
	int rc = 0;
	int size = YUC_RFIC_FW_SIZE, fw_size;
	u8 __iomem *vaddr;

	vaddr = vmalloc(size);
	if (vaddr == NULL) {
		dev_err(gul_dev->dev, "rfic fw malloc failed\n");
		return -ENOMEM;
	}

	rc  = gul_udev_load_firmware(gul_dev, vaddr,
				     size, fw_name, &fw_size);
	if (rc) {
		dev_err(gul_dev->dev, "udev Firmware [%s] request failed\n",
			 fw_name);
		kvfree(vaddr);
		goto out;
	}

	dev_info(gul_dev->dev, "udev Firmware [%s] - Addr %p, size %d\n",
		fw_name, vaddr, fw_size);

	yuc_prog_rfic_def(gul_dev, vaddr, fw_size);
	kvfree(vaddr);

out:
	return rc;
}

static int yuc_rfic_start_fw(struct gul_dev *gul_dev)
{
	u32 cmd_data;
	u16 rfic_words[YUC_RFIC_WORD_4];
	int ret = 0;
	u32 minor_ver, major_ver, target_ver;

	dma_wmb();

	/* wait for 200 micro sec for go pulse */
	udelay(YUC_RFIC_START_TIMEWAIT);

	/* do get fimware version to state firmware is actually start */
	cmd_data = YUC_FWCMD_GETVERSION;
	ret = yuc_rfic_cmd_proc(gul_dev, cmd_data, YUC_RFIC_CMD_ADDR,
			NULL, 0);
	if (!ret) {
		/* read command response for get version command */
		ret = yuc_rfic_cmd_proc(gul_dev, cmd_data,
			YUC_RFIC_CMD_RESP_ADDR,
			&rfic_words[0], YUC_RFIC_WORD_4);

		minor_ver  = GETnBITS(rfic_words[2], 0, 8);
		major_ver  = GETnBITS(rfic_words[2], 8, 4);
		target_ver = GETnBITS(rfic_words[2], 12, 4);
		dev_info(gul_dev->dev, "Yucca version: HW 0x%x Minor:%d,"\
			"Major:%d, Target: %d\n", rfic_words[1],
			minor_ver, major_ver, target_ver);
		if (!ret) {
			/*HW version is always non-zero*/
			if (rfic_words[1] == 0) {
				dev_err(gul_dev->dev, "Yucca HW version:0\n");
				ret = -ENODATA;
			}
		}
	}
	return ret;
}

static int yuc_rfic_APP_CalRegulator(struct gul_dev *gul_dev)
{
	u32 cmd_data;
	u16 rfic_words[YUC_RFIC_WORD_2];
	int ret = 0;
	u16 rcal_value;

	cmd_data = YUC_FWCMD_CALREGULATOR;
	ret = yuc_rfic_cmd_proc(gul_dev, cmd_data, YUC_RFIC_CMD_ADDR,
			NULL, 0);
	if (!ret) {
		ret = yuc_rfic_cmd_proc(gul_dev, cmd_data,
			YUC_RFIC_CMD_RESP_ADDR,
			&rfic_words[0], YUC_RFIC_WORD_2);

		rcal_value  = GETnBITS(rfic_words[1], 0, 4);
		dev_info(gul_dev->dev, "%s():rcal_value:%d\n", __func__,
				rfic_words[1]);
	}
	return ret;
}

static int yuc_rfic_APP_CalResistor(struct gul_dev *gul_dev)
{
	u32 cmd_data;
	u16 rfic_words[YUC_RFIC_WORD_2];
	int ret = 0;
	u16 rcal_value;

	rfic_words[0] = YUC_RFIC_CAL_RESISTOR_REFERENCE;
	cmd_data = YUC_FWCMD_CALRESISTOR;
	ret = yuc_rfic_cmd_proc(gul_dev, cmd_data, YUC_RFIC_CMD_ADDR,
			rfic_words, 1);
	if (!ret) {
		ret = yuc_rfic_cmd_proc(gul_dev, cmd_data,
			YUC_RFIC_CMD_RESP_ADDR, &rfic_words[0],
			YUC_RFIC_WORD_2);

		rcal_value  = GETnBITS(rfic_words[1], 0, 4);
		dev_info(gul_dev->dev, "%s():rcal_value:%d\n", __func__,
				rfic_words[1]);
	}
	return ret;
}

static int yuc_do_rfic_resetn(struct gul_dev *gul_dev)
{
	int ret;
	uint32_t gpdata;
	rfic_priv_t *priv;

	priv = gul_dev->rfic_priv;

	if ((ret = modem_gpio_getdata(gul_dev, GPIO2, &gpdata))) {
		dev_err(gul_dev->dev, " modem_gpio_getdata failed. ret:%d\n", ret);
		goto fail;
	}
	dev_dbg(gul_dev->dev, "Gpdata: 0x%x\n", gpdata);

	if ((ret = modem_gpio_setdata(gul_dev, GPIO2, priv->yucca1_reset_gpio, 0))) {
		dev_err(gul_dev->dev, " modem_gpio_setdata failed for pin:%d ret:%d\n",
			priv->yucca1_reset_gpio, ret);
		goto fail;
	}

	if ((ret = modem_gpio_setdata(gul_dev, GPIO2, priv->yucca2_reset_gpio, 0))) {
		dev_err(gul_dev->dev, " modem_gpio_setdata failed for pin:%d ret:%d\n",
			priv->yucca2_reset_gpio, ret);
		goto fail;
	}

	if ((ret = modem_gpio_setdata(gul_dev, GPIO2, priv->yucca1_trx_gpio, 1))) {
		dev_err(gul_dev->dev, " modem_gpio_setdata failed for pin:%d ret:%d\n",
			priv->yucca1_trx_gpio, ret);
		goto fail;
	}

	if ((ret = modem_gpio_setdata(gul_dev, GPIO2, priv->yucca2_trx_gpio, 1))) {
		dev_err(gul_dev->dev, " modem_gpio_setdata failed for pin:%d: ret:%d\n",
			priv->yucca2_trx_gpio, ret);
		goto fail;
	}
#if defined(LA1238RDB) || defined(LA1238CPE)
	if ((ret = modem_gpio_setdata(gul_dev, GPIO3, priv->yucca_trx_gpio, 1))) {
		dev_err(gul_dev->dev, " modem_gpio_setdata failed for pin:%d: ret:%d\n",
			priv->yucca_trx_gpio, ret);
		goto fail;
	}
#endif
	set_current_state(TASK_INTERRUPTIBLE);
	/* wait for 3ms */
	schedule_timeout(msecs_to_jiffies(YUC_RFIC_RESETN_TIMEWAIT));
	/* + 0.1ms for setup LVDS from OTP setting automatically */
	schedule_timeout(usecs_to_jiffies(YUC_RFIC_LVDS_OTP_TIMEWAIT));
	set_current_state(TASK_RUNNING);

	if ((ret = modem_gpio_setdata(gul_dev, GPIO2, priv->yucca1_reset_gpio, 1))) {
		dev_err(gul_dev->dev, " modem_gpio_setdata2 failed for pin:%d ret:%d\n",
			priv->yucca1_reset_gpio, ret);
		goto fail;
	}

	if ((ret = modem_gpio_setdata(gul_dev, GPIO2, priv->yucca2_reset_gpio, 1))) {
		dev_err(gul_dev->dev, " modem_gpio_setdata2 failed for pin:%d ret:%d\n",
			priv->yucca2_reset_gpio, ret);
		goto fail;
	}

	if ((ret = modem_gpio_getdata(gul_dev, GPIO2, &gpdata))) {
		dev_err(gul_dev->dev, " modem_gpio_getdata failed. ret:%d\n", ret);
		goto fail;
	}
	dev_dbg(gul_dev->dev, "Gpdata: 0x%x\n", gpdata);

fail:
	return 0;
}

int yuc_rfic_start_up(struct gul_dev *gul_dev, enum yuc_reset_type rst_type)
{
	int rc = 0;


    if ((rst_type == YUC_PO_RESET) || (rst_type == YUC_COLD_RESET)) {
		rc = yuc_load_rfic_img(gul_dev);
		if (rc) {
			dev_err(gul_dev->dev, "rfic firmware load failed: %d\n",
				rc);
			goto err;
		}
	}
	yuc_load_rfic_def_reg(gul_dev, YUC_RFIC_DATA_FW_FILE);
	if (rc) {
		dev_err(gul_dev->dev, "rfic default memory set failed: %d\n",
			rc);
		goto err;

	}

	rc = yuc_rfic_start_fw(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "rfic start firmware failed: %d\n", rc);
		goto err;
	}

	rc = yuc_rfic_APP_CalResistor(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "yuc_rfic_APP_CalResistor failed: %d\n", rc);
		goto err;
	}

	rc = yuc_rfic_APP_CalRegulator(gul_dev);
	if (rc) {
		dev_err(gul_dev->dev, "yuc_rfic_APP_CalRegulator failed: %d\n", rc);
		goto err;
	}

err:
	return rc;
}

int gul_yucca_rfic_probe(struct gul_dev *gul_dev, int virq_count,
		   struct virq_evt_map *virq_map)
{
	int ret = 0;
	rfic_priv_t *priv;
	u32 *llcp_csr, *llcp_gain_addr;
	u16 chipid;
	struct device_node *dn_modem_rf;
	int id;
	struct gul_mem_region_info *ccsr_mem =
		&(gul_dev->mem_regions[GUL_MEM_REGION_CCSR]);

	dev_dbg(gul_dev->dev, "Inside %s function\n", __func__);

	gul_dev->rfic_priv = kmalloc(sizeof(struct rfic_priv), GFP_KERNEL);
	if (gul_dev->rfic_priv == NULL) {
		dev_err(gul_dev->dev, "rfic priv malloc failed\n");
		return -ENOMEM;
	}

	priv = gul_dev->rfic_priv;
	priv->llcp1_addr = (u8 __iomem *) (ccsr_mem->vaddr + LLCP1_RFIC_OFFSET);
	priv->llcp2_addr = (u8 __iomem *) (ccsr_mem->vaddr + LLCP2_RFIC_OFFSET);

	/* Read GPIO pin no's from the DTS file */
#ifdef LA1224
	if (gul_dev->dn_modem)
		of_node_get(gul_dev->dn_modem);

	dn_modem_rf = of_find_node_by_name(gul_dev->dn_modem, "modem_rf");
	id = 0;
#else
	dn_modem_rf = of_find_node_by_name(NULL, "modem_rf");
	id = gul_dev->id;
#endif
	if (!dn_modem_rf) {
		dev_err(gul_dev->dev, "modem_rf:Node missing in DTB\n");
		kfree(gul_dev->rfic_priv);
		gul_dev->rfic_priv = NULL;
		return 0;
	}
	priv->yucca1_reset_gpio =
	of_get_named_gpio(dn_modem_rf, "yucca1-reset-gpio", id);
	priv->yucca2_reset_gpio =
	of_get_named_gpio(dn_modem_rf, "yucca2-reset-gpio", id);
	priv->yucca1_trx_gpio =
	of_get_named_gpio(dn_modem_rf, "yucca1-trx-gpio", id);
	priv->yucca2_trx_gpio =
	of_get_named_gpio(dn_modem_rf, "yucca2-trx-gpio", id);
#if defined(LA1238RDB) || defined(LA1238CPE)
	priv->yucca_trx_gpio =
	of_get_named_gpio(dn_modem_rf, "yucca-trx-gpio", id);
#endif
	if (!gpio_is_valid(priv->yucca1_reset_gpio) ||
		!gpio_is_valid(priv->yucca2_reset_gpio) ||
		!gpio_is_valid(priv->yucca1_trx_gpio) ||
#if defined(LA1238RDB) || defined(LA1238CPE)
		!gpio_is_valid(priv->yucca_trx_gpio) ||
#endif
		!gpio_is_valid(priv->yucca2_trx_gpio)) {
		dev_err(gul_dev->dev, " Invalid gpio pin\n");
		goto fail;
	}

	/* Init GPIO pins  */
	if (modem_gpio_init(gul_dev, GPIO2, priv->yucca1_reset_gpio, GPIO_OUTPUT)) {
		dev_err(gul_dev->dev, " modem_gpio_init failed for y1 reset\n");
		goto fail;
	}

	if (modem_gpio_init(gul_dev, GPIO2, priv->yucca2_reset_gpio, GPIO_OUTPUT)) {
		dev_err(gul_dev->dev, " modem_gpio_init failed for y2 reset\n");
		goto fail;
	}

	if (modem_gpio_init(gul_dev, GPIO2, priv->yucca1_trx_gpio, GPIO_OUTPUT)) {
		dev_err(gul_dev->dev, " modem_gpio_init failed for y1 trx\n");
		goto fail;
	}

	if (modem_gpio_init(gul_dev, GPIO2, priv->yucca2_trx_gpio, GPIO_OUTPUT)) {
		dev_err(gul_dev->dev, " modem_gpio_init failed for y2 trx\n");
		goto fail;
	}
	if (modem_gpio_init(gul_dev, GPIO3, priv->yucca_trx_gpio, GPIO_OUTPUT)) {
		dev_err(gul_dev->dev, " modem_gpio_init failed for yucca trx\n");
		goto fail;
	}
	/* GPIO Init End */

	ret = yuc_do_rfic_resetn(gul_dev);
	if (ret) {
		dev_err(gul_dev->dev, " yuc_do_rfic_resetn failed: %d\n", ret);
		goto fail;
	}

	udelay(YUC_RFIC_START_TIMEWAIT);
	/* Start Yucca 1 */
	llcp_csr = (u32 *)(ccsr_mem->vaddr + LLCPCSR1_OFFSET);
	llcp_gain_addr = (u32 *)(ccsr_mem->vaddr + LLCPGA1_OFFSET);
	writel(LLCPGA_VALUE, llcp_gain_addr);
	dev_dbg(gul_dev->dev, "llcp1 csr 0x%x\n", readl(llcp_csr));
	priv->llcp_addr = priv->llcp1_addr;
	chipid = readw((u32 *)(ccsr_mem->vaddr + LLCP1_RFIC_RO_REG_CHIPID_OFFSET));
	dev_info(gul_dev->dev, "%s: RF1 chipidreg[%x]\r\n", __func__, chipid);
	if (chipid != YUC_RFIC_CHIP_ID_RO_REG_VALUE) {
		dev_err(gul_dev->dev, "%s: RF1 Card is not there !!\r\n", __func__);
		goto yucca2;
	}

	dev_info(gul_dev->dev, "%s: Yucca1: start .. \r\n", __func__);
	ret = yuc_rfic_start_up(gul_dev, YUC_PO_RESET);
	if (ret) {
		dev_err(gul_dev->dev, "Failed to start Yucaa 1. err[%d]\n", ret);
		goto yucca2;
	}
	dev_dbg(gul_dev->dev, "llcp1 csr after fw load 0x%x\n", readl(llcp_csr));
	gul_set_host_ready(gul_dev, HIF_HOST_READY_RFIC);
	dev_info(gul_dev->dev, "%s: Yucca1:HIF_HOST_READY_RFIC is set \r\n", __func__);
	/* Start Yucca 1 */

yucca2:
	udelay(YUC_RFIC_START_TIMEWAIT);
	/* Start Yucca 2 */
	llcp_csr = (u32 *)(ccsr_mem->vaddr + LLCPCSR2_OFFSET);
	llcp_gain_addr = (u32 *)(ccsr_mem->vaddr + LLCPGA2_OFFSET);
	writel(LLCPGA_VALUE, llcp_gain_addr);
	dev_dbg(gul_dev->dev, "llcp2 csr 0x%x\n", readl(llcp_csr));
	priv->llcp_addr = priv->llcp2_addr;
	chipid = readw((u32 *)(ccsr_mem->vaddr + LLCP2_RFIC_RO_REG_CHIPID_OFFSET));
	dev_info(gul_dev->dev, "%s: RF2 chipidreg[%x]\r\n", __func__, chipid);
	if (chipid != YUC_RFIC_CHIP_ID_RO_REG_VALUE) {
		dev_err(gul_dev->dev, "%s: RF2 Card is not there !! \r\n", __func__);
		goto fail;
	}

	dev_info(gul_dev->dev, "%s: Yucca2: start .. \r\n", __func__);
	ret = yuc_rfic_start_up(gul_dev, YUC_PO_RESET);
	if (ret) {
		dev_err(gul_dev->dev, "Failed to Start Yucaa2. err[%d]\n", ret);
		goto fail;
	}
	dev_dbg(gul_dev->dev, "llcp2 csr after fw load 0x%x\n", readl(llcp_csr));
	gul_set_host_ready(gul_dev, HIF_HOST_READY_RFIC2);
	dev_info(gul_dev->dev, "%s: Yucca2:HIF_HOST_READY_RFIC2 is set \r\n", __func__);
	/* Start Yucca 2 */

fail:
	kfree(gul_dev->rfic_priv);
	gul_dev->rfic_priv = NULL;
	return ret;
}

int gul_yucca_rfic_remove(struct gul_dev *gul_dev)
{
	kfree(gul_dev->rfic_priv);
	gul_dev->rfic_priv = NULL;
	return 0;
}

