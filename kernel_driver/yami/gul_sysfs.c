/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2026 NXP
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/stat.h>
#include <gul_host_if.h>

#include "gul_base.h"
#ifdef	RF_DRVR_ENABLED
#include "gul_rfic.h"
#include <rfdev_ioctl.h>
#endif
#ifdef	HAWK_DRVR_ENABLED
#include "gul_hawk.h"
#endif
#include "gul_vspa.h"
#include "gul_dcs.h"

uint8_t wdog_handler_notifier[MAX_MODEM];
extern struct gul_global g_gul_global[MAX_MODEM];
static enum gul_warmup_status g_warmup_status[MAX_MODEM] = {1, 1, 1, 1};
static ssize_t gul_show_e200_usage(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u64 addr =
		(u64)(&(gul_dev->hif->stats.cpuidle_stats[0].strt1_l)) -
		(u64)(gul_dev->mem_regions[GUL_MEM_REGION_PEBM].vaddr) +
		gul_dev->mem_regions[GUL_MEM_REGION_PEBM].phys_addr;
	return sprintf(buf, "0x%llx\n", addr);
}

static ssize_t gul_show_id(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", gul_dev->id);
}

static ssize_t show_pci_dev_name(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", g_gul_global[gul_dev->id].dev_name);
}

static ssize_t gul_show_la12xx_version(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n",
		((g_gul_global[gul_dev->id].soc_version & GEUL_SVR_REVB_VAL) == GEUL_SVR_REVB_VAL) ? "B0" : "A0");
}

static ssize_t gul_show_stats_control_mask(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct gul_dev *gul_dev;

	gul_dev = dev_get_drvdata(dev);
	return sprintf(buf, "%x\n", gul_dev->stats_desc.stats_control);
}

static ssize_t gul_set_stats_control_mask(struct device *dev,
						struct device_attribute *attr,
						const char *buf,  size_t count)
{
	struct gul_dev *gul_dev;
	int rc = 0;
	unsigned long val;

	gul_dev = dev_get_drvdata(dev);

	rc = kstrtoul(buf, 0, &val);
	if (rc) {
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
		goto out;
	} else {
		gul_dev->stats_desc.stats_control = val;
	}
out:
	return strnlen(buf, count);
}

ssize_t gul_collect_ep_log(struct gul_ep_log *ep_log, char *buf)
{
	int log_len, max_len, str_len;
	char *ep_log_str;

	log_len = 0;
	str_len = 0;
	max_len = 0;

	ep_log_str = ep_log->buf + ep_log->offset;
	max_len = ep_log->len - ep_log->offset;
	str_len = strnlen(ep_log_str, max_len);
	if (str_len) {
		memcpy_fromio(buf, ep_log_str, str_len);
		memset_io(ep_log_str, 0, str_len);
		ep_log->offset += str_len;
		if (ep_log->offset >= ep_log->len)
			ep_log->offset = 0;
		log_len += str_len;
		buf += str_len;

		if (max_len == str_len) {
			ep_log_str = ep_log->buf;
			str_len = strnlen(ep_log_str, ep_log->len);
			if (str_len) {
				memcpy_fromio(buf, ep_log_str, str_len);
				memset_io(ep_log_str, 0, str_len);
				log_len += str_len;
				buf += str_len;
			}
			ep_log->offset = str_len;
			if (ep_log->offset >= ep_log->len)
				ep_log->offset = 0;
		}
	}
	return log_len;
}

static ssize_t show_ep_log(struct device *dev,
		struct device_attribute *attr, char *buf, uint8_t core_id)
{
	struct gul_dev *gul_dev;
	struct gul_ep_log *ep_log;
	int log_len, i;

	gul_dev = dev_get_drvdata(dev);
	ep_log = &gul_dev->ep_log[core_id];
	log_len = 0;

	dev_dbg(gul_dev->dev, "GUL log buf dump, vaddr %p, offset %d\n",
			ep_log->buf, ep_log->offset);

	log_len = gul_collect_ep_log(ep_log, buf);
	if (log_len == 0) {
		for (i = 0; i < ep_log->len; i++) {
			if (ep_log->buf[i] != 0) {
				ep_log->offset = i;
				log_len = gul_collect_ep_log(ep_log, buf);
			}
		}
	}

	dev_dbg(gul_dev->dev, "log len: %d, offset : %d\n", log_len,
			ep_log->offset);

	/* Returning PAGE_SIZE length will gives bad count*/
	if (log_len >= PAGE_SIZE)
		log_len -= 1;

	return log_len;
}

static uint32_t cal_core_mask(uint32_t cores)
{
	int core_mask = 0, loop = 0;
	for (loop = 0; loop < cores; loop++)
		core_mask |= 1 << (loop * 2);
	return  core_mask;
}
static ssize_t gul_modem_status(struct gul_dev *gul_dev, char *buf)
{
	struct gul_hif *hif;
	uint32_t loop, core_mask = 0, core_status_val = 0;

	hif = gul_dev->hif;

	core_mask = cal_core_mask(gul_ep_get_numcores());
	core_status_val = readl(&hif->core_status_flag);
	sprintf(&buf[strlen(buf)], "%s", "\n");

	sprintf(&buf[strlen(buf)], "MODEM_ID: %d NUM CORE:%d STATUS:%s\n",
			gul_dev->id, gul_ep_get_numcores(),
			((core_status_val == core_mask) ?
			 "ALIVE" : "NOT ALIVE"));

	sprintf(&buf[strlen(buf)], "CORE_ID\t: STATUS\n");
	for (loop = 0; loop < gul_ep_get_numcores(); loop++) {
		sprintf(&buf[strlen(buf)], "%d\t: %s\n", loop,
			core_status_val & (1 << (loop * 2)) ?
			(core_status_val & (1 << ((loop * 2) + 1)) ?
			 "NOT RUNNING" : "RUNNING") : "HOLD-OFF");
	}

	sprintf(&buf[strlen(buf)], "MODEM_ID: %d WDOG_HANDLER %s\n",
			gul_dev->id,
			wdog_handler_notifier[gul_dev->id] ? "REGISTERED" :
			"NOT REGISTERED");

	sprintf(&buf[strlen(buf)], "%s", "\n");

	return 0;
}

void gul_set_warmup_status(int dev_id, enum gul_warmup_status status)
{
	g_warmup_status[dev_id] = status;
}
static ssize_t gul_modem_warmup_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev;

	gul_dev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", g_warmup_status[gul_dev->id]);
}
static ssize_t gul_show_ep_log_core_0(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return show_ep_log(dev, attr, buf, 0);
}

static ssize_t gul_show_ep_log_core_1(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return show_ep_log(dev, attr, buf, 1);
}

static ssize_t gul_show_ep_log_core_2(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return show_ep_log(dev, attr, buf, 2);
}

static ssize_t gul_show_ep_log_core_3(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return show_ep_log(dev, attr, buf, 3);
}

static ssize_t gul_show_ep_log_core_4(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return show_ep_log(dev, attr, buf, 4);
}

static ssize_t gul_show_ep_log_core_5(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return show_ep_log(dev, attr, buf, 5);
}

static ssize_t reset_ep_log(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count, uint8_t core_id)
{
	struct gul_dev *gul_dev;
	int rc = 0;
	unsigned long val;
	struct gul_ep_log *ep_log;

	gul_dev = dev_get_drvdata(dev);
	ep_log = &gul_dev->ep_log[0];

	rc = kstrtoul(buf, 0, &val);
	if (rc) {
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
		goto out;
	}

	if (val) {
		dev_err(gul_dev->dev, "%d not valid. Write 0 to reset buffer\n",
			(int) val);
		goto out;
	}

	/* reset GUL End-Point debug log buffer */
	memset(ep_log->buf, 0, ep_log->len);
	ep_log->offset = 0;

	dev_info(gul_dev->dev, "GUL log buf reset, vaddr %px, offset %d\n",
		ep_log->buf, ep_log->offset);
out:
	return strnlen(buf, count);
}

static ssize_t gul_reset_ep_log_core_0(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	return reset_ep_log(dev, attr, buf, count, 0);
}

static ssize_t gul_reset_ep_log_core_1(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	return reset_ep_log(dev, attr, buf, count, 1);
}

static ssize_t gul_reset_ep_log_core_2(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	return reset_ep_log(dev, attr, buf, count, 2);
}

static ssize_t gul_reset_ep_log_core_3(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	return reset_ep_log(dev, attr, buf, count, 3);
}

static ssize_t gul_reset_ep_log_core_4(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	return reset_ep_log(dev, attr, buf, count, 4);
}

static ssize_t gul_reset_ep_log_core_5(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	return reset_ep_log(dev, attr, buf, count, 5);
}

static ssize_t gul_show_ep_log_level(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct gul_dev *gul_dev;
	struct gul_hif *hif;
	struct debug_log_regs *dbg_log_regs;
	int core_id = 0, len = 0;


	gul_dev = dev_get_drvdata(dev);
	hif = gul_dev->hif;
	for (core_id = 0; core_id < gul_ep_get_numcores(); core_id++) {
		dbg_log_regs = &gul_dev->hif->dbg_log_regs[core_id];
		len += snprintf((buf + len), GUL_DBG_LOG_MAX_STRLEN,
				"LA12xx[Core %d] log level - %d\n",
				core_id, readl(&dbg_log_regs->log_level));
	}
	return len;
}

static ssize_t gul_set_ep_log_level(struct device *dev,
					struct device_attribute *attr,
					const char *buf,  size_t count)
{
	struct gul_dev *gul_dev;
	struct gul_hif *hif;
	struct debug_log_regs *dbg_log_regs;
	int rc = 0, core_id = 0;
	unsigned long val;

	gul_dev = dev_get_drvdata(dev);
	hif = gul_dev->hif;

	rc = kstrtoul(buf, 0, &val);
	if (rc) {
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
		goto out;
	}
	if ((val < GUL_LOG_LEVEL_ERR) || (val > GUL_LOG_LEVEL_ALL)) {
		dev_err(gul_dev->dev, "Invalid level %d, valid [%d - %d]\n",
			(int) val, GUL_LOG_LEVEL_ERR, GUL_LOG_LEVEL_ALL);
		goto out;
	}
	for (core_id = 0; core_id < gul_ep_get_numcores(); core_id++) {
		dbg_log_regs = &gul_dev->hif->dbg_log_regs[core_id];
		writel(val, &dbg_log_regs->log_level);
	}
out:
	return strnlen(buf, count);
}

#ifdef RF_DRVR_ENABLED
static ssize_t rf_dump_hif_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 1 to dump the RF HIF\n");
}

static ssize_t rf_dump_hif(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 1)
		__rf_dump_hif(gul_dev->rfdev);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}
#endif

#ifdef RF_FR2_DRVR_ENABLED
static ssize_t rf_rxtx_duty_test_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<tx_cnt>:<rx_cnt>:<slot time us>:<total time in s>\n");
}

static ssize_t rf_rxtx_duty_test(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 rx_count, tx_count, duration, slot;
	unsigned long total_time;

	if (sscanf(buf, "%u:%u:%u:%u", &tx_count, &rx_count, &slot, &duration) == 4) {

		total_time = jiffies + msecs_to_jiffies(duration * 1000);

		/* Run the Rx/Tx switching for total input duration */
		while (time_before(jiffies, total_time)) {
			if (tx_count) {
				/* Switch to tx */
				__rf_switch_tx(gul_dev->rfdev);
				/* Wait for required slots before switching to Rx */
				/* Avoid sending multiple command for each slot */
				udelay(slot * tx_count);
			}

			if (rx_count) {
				__rf_switch_rx(gul_dev->rfdev);
				udelay(slot * rx_count);
			}
		}
	} else {
		dev_err(gul_dev->dev, "%s is invalid\n", buf);
	}

	return strnlen(buf, count);
}

static ssize_t rf_fpga_read_regs(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,  size_t count)
{
	struct gul_dev *gul_dev;
	u32 addr, len, *regbuf = NULL;
	int ret = 0, i;

	gul_dev = dev_get_drvdata(dev);
	sscanf(buf, "%x:%x", &addr, &len);
	regbuf = kzalloc((sizeof(*regbuf) * len), GFP_KERNEL);
	if (!regbuf) {
		dev_err(gul_dev->dev, "Buf allocation failure\n");
		goto out;
	}
	/*RF FPGA registers are 16 bits only*/
	ret = __rf_read_regs(gul_dev->rfdev, (u16)addr, len, regbuf);
	if (ret) {
		dev_err(gul_dev->dev, "Read cmd failed %d\n\n", ret);
		goto out;
	}

	printk("RF FPGA reg dump: Addr 0x%04x, Len %d\n", addr, len);
	for (i = 0; i < len; i++) {
		if (!(i % 8))
			printk("\n0x%x:", i);
		printk("0x%02x ", regbuf[i]);
	}
	printk("\n");
out:
	kfree(regbuf);
	return strnlen(buf, count);
}

static ssize_t rf_fpga_read_regs_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev;

	gul_dev = dev_get_drvdata(dev);
	return sprintf(buf, "<0xaddr>:<0xcount>/len\n");
}

static ssize_t rf_fpga_write_regs(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,  size_t count)
{
	struct gul_dev *gul_dev;
	u32 addr, val;
	struct rif_write_reg_buf reg_buf;
	int ret = 0;

	gul_dev = dev_get_drvdata(dev);
	sscanf(buf, "%x:%x", &addr, &val);
	/*RF FPGA registers have 16 bit addr, 8 bit data*/
	reg_buf.addr = addr & 0xffff;
	reg_buf.data = val & 0xff;
	ret = __rf_write_regs(gul_dev->rfdev, 1, &reg_buf);
	if (ret) {
		dev_err(gul_dev->dev, "Write cmd failed %d\n\n", ret);
		goto out;
	}
out:
	return strnlen(buf, count);
}

static ssize_t rf_chip_write_regs(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,  size_t count)
{
	struct gul_dev *gul_dev;
	u32 addr, val, chip, ret = 0;
	struct rif_write_reg_buf reg_buf;

	gul_dev = dev_get_drvdata(dev);
	sscanf(buf, "%x:%x:%x", &chip, &addr, &val);
	printk("chip 0x%02x, addr 0x%04x, val 0x%x\n",
	       chip, addr, val);
	reg_buf.addr = addr;
	reg_buf.data = val;
	ret = rf_write_chip_regs(gul_dev->rfdev, 1,
				 &reg_buf, chip);
	if (ret)
		dev_err(gul_dev->dev, "mv chip reg write failed.");

	return strnlen(buf, count);
}

static ssize_t rf_chip_write_regs_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev;
	int len = 0;

	gul_dev = dev_get_drvdata(dev);
	len += sprintf((buf + len), "<0xchip_id>:<0xaddr>:<0xval>/len\n");
	len += sprintf((buf + len), "chip_id:\n");
	len += sprintf((buf + len), "0: MV2801 chain 0\n");
	len += sprintf((buf + len), "1: MV2801 chain 1\n");
	len += sprintf((buf + len), "2: MV2801 chain 2\n");
	len += sprintf((buf + len), "3: MV2801 chain 3\n");
	len += sprintf((buf + len), "4: MV2801 broadcast\n");
	len += sprintf((buf + len), "5: MV2802 chain 0\n");
	len += sprintf((buf + len), "6: MV2802 chain 1\n");
	len += sprintf((buf + len), "7: MV2802 broadcast\n");
	len += sprintf((buf + len), "8: MV2803 TX\n");
	len += sprintf((buf + len), "9: MV2803 RX\n");
	len += sprintf((buf + len), "a: MV2803 broadcast\n");
	return len;
}

static ssize_t rf_chip_read_regs(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,  size_t count)
{
	struct gul_dev *gul_dev;
	int len, chip, i, ret = 0;
	u32 addr, *regbuf = NULL;

	gul_dev = dev_get_drvdata(dev);
	sscanf(buf, "%x:%x:%x", &chip, &addr, &len);
	printk("chip 0x%02x, addr 0x%04x, len 0x%x\n",
	       chip, addr, len);

	regbuf = kzalloc((sizeof(*regbuf) * len), GFP_KERNEL);
	if (!regbuf) {
		dev_err(gul_dev->dev, "Buf allocation failure\n");
		goto out;
	}
	/*RF FPGA registers are 16 bits only*/
	ret = rf_read_chip_regs(gul_dev->rfdev, (u16)addr, len, regbuf, chip);
	if (ret) {
		dev_err(gul_dev->dev, "Read cmd failed %d\n\n", ret);
		goto out;
	}

	printk("RF mv chip reg dump: Addr 0x%04x, Len %d, chip %d\n",
	       addr, len, chip);

	for (i = 0; i < len; i++) {
		if (!(i % 8))
			printk("\n0x%x:", i);
		printk("%02x ", regbuf[i]);
	}
	printk("\n");
out:
	kfree(regbuf);
	return strnlen(buf, count);
}

static ssize_t rf_vcxo_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<Index between 0 to 255>\n");
}

static ssize_t rf_set_vcxo_dac(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	int rc;
	u32 val;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else
		__rf_set_vcxo_dac(gul_dev->rfdev, (u16)val);

	return strnlen(buf, count);
}

static ssize_t rf_tx_gain_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<lut_index>\n");
}

static ssize_t rf_set_tx_gain(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	int rc;
	u32 val;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else
		__rf_set_tx_gain(gul_dev->rfdev, (u8)val);

	return strnlen(buf, count);
}

static ssize_t rf_rx_gain_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<beam_mask>:<lut_index>\n");
}

static ssize_t rf_set_rx_gain(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 beam_mask, lut_index;

	if (sscanf(buf, "%u:%u", &beam_mask, &lut_index) == 2) {
		__rf_set_rx_gain(gul_dev->rfdev, (u8)beam_mask,
				 (u8)lut_index);
	} else {
		dev_err(gul_dev->dev, "%s is invalid\n", buf);
	}

	return strnlen(buf, count);
}

static ssize_t rf_tx_switch_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 1\n");
}

static ssize_t rf_switch_tx(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 1)
		__rf_switch_tx(gul_dev->rfdev);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_rx_switch_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 1\n");
}

static ssize_t rf_switch_rx(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 1)
		__rf_switch_rx(gul_dev->rfdev);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_stress_test_control_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 1 to start test, Write 0 to end test\n");
}

static ssize_t rf_stress_test_control(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 1 || val == 0)
		__rf_control_stress_test(gul_dev->rfdev, val);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_enable_disable_bg_task_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 1 to enable task, Write 0 to disable task\n");
}

static ssize_t rf_enable_disable_bg_task(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 1 || val == 0)
		__rf_enable_disable_bg_task(gul_dev->rfdev, val);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_set_freq_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<freq(khz)>:<ppm>:<ref_freq(khz)>:<init>\n");
}

static ssize_t rf_set_freq_tx(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 rf_freq_khz, ppm, ref_freq_khz, init;

	if (sscanf(buf, "%u:%u:%u:%u", &rf_freq_khz, &ppm, &ref_freq_khz, &init)
	    == 4) {
		__rf_set_freq_tx(gul_dev->rfdev,
				 rf_freq_khz, ppm,
				 ref_freq_khz, init);
	} else {
		dev_err(gul_dev->dev, "%s is invalid\n", buf);
	}

	return strnlen(buf, count);
}

static ssize_t rf_rx_agc_gain_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<beam_mask>:<lut_index>\n");
}

static ssize_t rf_set_rx_agc_gain(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 beam_mask, lut_index;

	if (sscanf(buf, "%u:%u", &beam_mask, &lut_index) == 2) {
		__rf_set_rx_agc_gain_index(gul_dev->rfdev, (u8)beam_mask,
					   (u8)lut_index);
	} else {
		dev_err(gul_dev->dev, "%s is invalid\n", buf);
	}

	return strnlen(buf, count);
}

static ssize_t rf_pll_auto_tune_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<iter_count>\n");
}

static ssize_t rf_set_pll_auto_tune(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	int rc;
	u32 val;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else
		__rf_set_auto_tune_pll(gul_dev->rfdev, (u8)val);

	return strnlen(buf, count);
}

static ssize_t rf_bbk_index_tx_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<bbk_index_tx>\n");
}

static ssize_t rf_set_bbk_index_tx(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	int rc;
	u32 bbk_index;

	rc = kstrtou32(buf, 0, &bbk_index);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else
		__rfic_set_beambook_index_tx(gul_dev->rfdev, (u8)bbk_index);

	return strnlen(buf, count);
}

static ssize_t rf_bbk_index_rx_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<bbk_index_rx>\n");
}

static ssize_t rf_set_bbk_index_rx(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	int rc;
	u32 bbk_index;

	rc = kstrtou32(buf, 0, &bbk_index);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else
		__rfic_set_beambook_index_rx(gul_dev->rfdev, (u8)bbk_index);

	return strnlen(buf, count);
}

static ssize_t rf_trgt_brdcast_tx_fe_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 1\n");
}

static ssize_t rf_set_trgt_brdcast_tx_fe(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 1)
		__rfic_set_target_broadcast_tx_fe(gul_dev->rfdev);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_trgt_brdcast_rx_fe_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 1\n");
}

static ssize_t rf_set_trgt_brdcast_rx_fe(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 1)
		__rfic_set_target_broadcast_rx_fe(gul_dev->rfdev);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_pbk_mode_tx_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 1\n");
}

static ssize_t rf_set_pbk_mode_tx(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 1)
		__rfic_set_pbk_mode_tx(gul_dev->rfdev, (rfic_pbk_modes_t)val);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_pbk_mode_rx_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 1\n");
}

static ssize_t rf_set_pbk_mode_rx(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 1)
		__rfic_set_pbk_mode_rx(gul_dev->rfdev, (rfic_pbk_modes_t)val);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_sync_reset_pbk_mode_tx_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 3\n");
}

static ssize_t rf_set_sync_reset_pbk_mode_tx(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 3)
		__rfic_set_pbk_mode_tx(gul_dev->rfdev, (rfic_pbk_modes_t)val);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_sync_reset_pbk_mode_rx_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 3\n");
}

static ssize_t rf_set_sync_reset_pbk_mode_rx(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else if (val == 3)
		__rfic_set_pbk_mode_rx(gul_dev->rfdev, (rfic_pbk_modes_t)val);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_inc_pbk_index_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write 1\n");
}

static ssize_t rf_set_inc_pbk_index(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev,
			"%s is not hex or decimal\n", buf);
	else if (val == 1)
		__rfic_increment_pbk_index(gul_dev->rfdev);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t rf_cfg_dynamic_ctrl_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev;
	int len = 0;

	gul_dev = dev_get_drvdata(dev);
	len += sprintf((buf + len),
			"<dynamic_operation>:<enable/disbale(1/0)>\n");
	len += sprintf((buf + len), "dynamic_operations:\n");
	len += sprintf((buf + len), "0: FPGA_PLL_AUTO_TUNE\n");
	len += sprintf((buf + len), "1: FPGA_RX_SPI_SLOT\n");
	len += sprintf((buf + len), "2: FPGA_TX_SPI_SLOT\n");
	len += sprintf((buf + len), "3: FPGA_RX_GAIN_UPDATE\n");
	len += sprintf((buf + len), "4: FPGA_TX_GAIN_UPDATE\n");
	len += sprintf((buf + len), "5: FPGA_LOGEN_CAL\n");
	len += sprintf((buf + len), "6: FPGA_ALL_OPS\n");
	return len;
}

static ssize_t rf_set_cfg_dynamic_ctrl(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 dynamic_op, enable;

	if (sscanf(buf, "%u:%u", &dynamic_op, &enable) == 2) {
		__rfic_config_dynamic_controller(gul_dev->rfdev,
				(rfic_cfg_dynamic_ctrl_t)dynamic_op,
				(u8)enable);
	} else {
		dev_err(gul_dev->dev, "%s is invalid\n", buf);
	}

	return strnlen(buf, count);
}

static ssize_t rf_write_list_to_pbk_memory_tx_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<pbk_length>\n");
}

static ssize_t rf_set_write_list_to_pbk_memory_tx(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	int rc;
	u32 pbk_length;

	rc = kstrtou32(buf, 0, &pbk_length);
	if (rc) {
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	} else {
		__rfic_write_list_to_pbk_memory_tx(gul_dev->rfdev,
						   (u8)pbk_length);
	}

	return strnlen(buf, count);
}

static ssize_t rf_write_list_to_pbk_memory_rx_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<pbk_length>\n");
}

static ssize_t rf_set_write_list_to_pbk_memory_rx(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	int rc;
	u32 pbk_length;

	rc = kstrtou32(buf, 0, &pbk_length);
	if (rc) {
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	} else {
		__rfic_write_list_to_pbk_memory_rx(gul_dev->rfdev,
						   (u8)pbk_length);
	}

	return strnlen(buf, count);
}

static ssize_t rf_cfg_beam_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Beam1:1, Beam2:2, Both Beams:3\n");
}

static ssize_t rf_set_cfg_beam(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 beam;
	int rc;

	rc = kstrtou32(buf, 0, &beam);
	if (rc)
		dev_err(gul_dev->dev, "%s is not hex or decimal\n", buf);
	else
		__rfic_set_beam_config(gul_dev->rfdev, (u8)beam);

	return strnlen(buf, count);
}


#endif

static ssize_t yami_version_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", GUL_HOST_SW_VERSION);
}

static ssize_t yami_status_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return gul_show_global_status(buf);
}

static ssize_t modem_status_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct list_head *ptr;
	struct gul_dev *dev = NULL;

	list_for_each(ptr, &pcidev_list) {
		dev = list_entry(ptr, struct gul_dev, list);
		if (g_gul_global[dev->id].active)
			gul_modem_status(dev, buf);
	}
	return strlen(buf);
}

static ssize_t ipc_status_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct list_head *ptr;
	struct gul_dev *dev = NULL;

	list_for_each(ptr, &pcidev_list) {
		dev = list_entry(ptr, struct gul_dev, list);
		if (g_gul_global[dev->id].active)
			gul_ipc_show(dev, buf);
	}
	return strlen(buf);
}

static DEVICE_ATTR(warmup_status, 0644,
			gul_modem_warmup_status,
			NULL);

static DEVICE_ATTR(target_log_core_0, 0644,
			gul_show_ep_log_core_0,
			gul_reset_ep_log_core_0);

static DEVICE_ATTR(target_log_core_1, 0644,
			gul_show_ep_log_core_1,
			gul_reset_ep_log_core_1);

static DEVICE_ATTR(target_log_core_2, 0644,
			gul_show_ep_log_core_2,
			gul_reset_ep_log_core_2);

static DEVICE_ATTR(target_log_core_3, 0644,
			gul_show_ep_log_core_3,
			gul_reset_ep_log_core_3);

static DEVICE_ATTR(target_log_core_4, 0644,
			gul_show_ep_log_core_4,
			gul_reset_ep_log_core_4);

static DEVICE_ATTR(target_log_core_5, 0644,
			gul_show_ep_log_core_5,
			gul_reset_ep_log_core_5);

static DEVICE_ATTR(target_log_level, 0644,
			gul_show_ep_log_level,
			gul_set_ep_log_level);

static DEVICE_ATTR(target_stats_control, 0644,
			gul_show_stats_control_mask,
			gul_set_stats_control_mask);

static DEVICE_ATTR(e200_cpu_usage, 0644,
			gul_show_e200_usage,
			NULL);

static DEVICE_ATTR(gul_id, 0644,
			gul_show_id,
			NULL);

static DEVICE_ATTR(pci_dev_name, 0644,
			show_pci_dev_name,
			NULL);

static DEVICE_ATTR(la12xx_version, 0644,
			gul_show_la12xx_version,
			NULL);

#ifdef RF_DRVR_ENABLED
static DEVICE_ATTR(rf_dump_hif, 0644,
			rf_dump_hif_show,
			rf_dump_hif);
#endif

#ifdef RF_FR2_DRVR_ENABLED
static DEVICE_ATTR(rf_fpga_read_regs, 0644,
			rf_fpga_read_regs_show,
			rf_fpga_read_regs);
static DEVICE_ATTR(rf_fpga_write_regs, 0644,
			rf_fpga_read_regs_show,
			rf_fpga_write_regs);
static DEVICE_ATTR(rf_rxtx_duty_test, 0644,
			rf_rxtx_duty_test_show,
			rf_rxtx_duty_test);
static DEVICE_ATTR(rf_chip_read_regs, 0644,
			rf_chip_write_regs_show,
			rf_chip_read_regs);
static DEVICE_ATTR(rf_chip_write_regs, 0644,
			rf_chip_write_regs_show,
			rf_chip_write_regs);

static DEVICE_ATTR(rf_vcxo, 0644,
			rf_vcxo_show,
			rf_set_vcxo_dac);

static DEVICE_ATTR(rf_tx_gain, 0644,
			rf_tx_gain_show,
			rf_set_tx_gain);

static DEVICE_ATTR(rf_rx_gain, 0644,
			rf_rx_gain_show,
			rf_set_rx_gain);

static DEVICE_ATTR(rf_tx_switch, 0644,
			rf_tx_switch_show,
			rf_switch_tx);

static DEVICE_ATTR(rf_rx_switch, 0644,
			rf_rx_switch_show,
			rf_switch_rx);

static DEVICE_ATTR(rf_stress_test_control, 0644,
			rf_stress_test_control_show,
			rf_stress_test_control);

static DEVICE_ATTR(rf_set_freq, 0644,
			rf_set_freq_show,
			rf_set_freq_tx);

static DEVICE_ATTR(rf_rx_agc_gain, 0644,
			rf_rx_agc_gain_show,
			rf_set_rx_agc_gain);

static DEVICE_ATTR(rf_pll_auto_tune, 0644,
			rf_pll_auto_tune_show,
			rf_set_pll_auto_tune);

static DEVICE_ATTR(rf_bbk_index_tx, 0644,
			rf_bbk_index_tx_show,
			rf_set_bbk_index_tx);

static DEVICE_ATTR(rf_bbk_index_rx, 0644,
			rf_bbk_index_rx_show,
			rf_set_bbk_index_rx);

static DEVICE_ATTR(rf_trgt_brdcast_tx_fe, 0644,
			rf_trgt_brdcast_tx_fe_show,
			rf_set_trgt_brdcast_tx_fe);

static DEVICE_ATTR(rf_trgt_brdcast_rx_fe, 0644,
			rf_trgt_brdcast_rx_fe_show,
			rf_set_trgt_brdcast_rx_fe);

static DEVICE_ATTR(rf_pbk_mode_tx, 0644,
			rf_pbk_mode_tx_show,
			rf_set_pbk_mode_tx);

static DEVICE_ATTR(rf_pbk_mode_rx, 0644,
			rf_pbk_mode_rx_show,
			rf_set_pbk_mode_rx);

static DEVICE_ATTR(rf_sync_reset_pbk_mode_tx, 0644,
			rf_sync_reset_pbk_mode_tx_show,
			rf_set_sync_reset_pbk_mode_tx);

static DEVICE_ATTR(rf_sync_reset_pbk_mode_rx, 0644,
			rf_sync_reset_pbk_mode_rx_show,
			rf_set_sync_reset_pbk_mode_rx);

static DEVICE_ATTR(rf_inc_pbk_index, 0644,
			rf_inc_pbk_index_show,
			rf_set_inc_pbk_index);

static DEVICE_ATTR(rf_cfg_dynamic_ctrl, 0644,
			rf_cfg_dynamic_ctrl_show,
			rf_set_cfg_dynamic_ctrl);

static DEVICE_ATTR(rf_write_list_to_pbk_memory_tx, 0644,
			rf_write_list_to_pbk_memory_tx_show,
			rf_set_write_list_to_pbk_memory_tx);

static DEVICE_ATTR(rf_write_list_to_pbk_memory_rx, 0644,
			rf_write_list_to_pbk_memory_rx_show,
			rf_set_write_list_to_pbk_memory_rx);

static DEVICE_ATTR(rf_cfg_beam, 0644,
			rf_cfg_beam_show,
			rf_set_cfg_beam);

static DEVICE_ATTR(rf_enable_disable_bg_task, 0644,
			rf_enable_disable_bg_task_show,
			rf_enable_disable_bg_task);
#endif

#ifdef HAWK_DRVR_ENABLED
static ssize_t hawk_list_events_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);

	return __hawk_list_events_show(gul_dev->hawkdev, buf);
}

static ssize_t hawk_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);

	return __hawk_events_show(gul_dev->hawkdev, buf);
}

static ssize_t hawk_event_set(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 event_output, event;

	if (sscanf(buf, "%u:%u", &event_output, &event) == 2) {
		__hawk_event_set(gul_dev->hawkdev, event_output, event);
	} else {
		dev_err(gul_dev->dev, "%s is invalid\n", buf);
	}

	return strnlen(buf, count);
}

static ssize_t hawk_event_reset(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev,
			"%s is not hex or decimal\n", buf);
	else if (val == 1)
		__hawk_event_reset(gul_dev->hawkdev);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t hawk_core_mask_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);

	return __hawk_core_mask_show(gul_dev->hawkdev, buf);
}

static ssize_t hawk_core_mask_set(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 core_mask;

	if (sscanf(buf, "%x", &core_mask) == 1) {
		__hawk_core_mask_set(gul_dev->hawkdev, core_mask);
	} else {
		dev_err(gul_dev->dev, "%s is invalid\n", buf);
	}

	return strnlen(buf, count);
}

static ssize_t hawk_report_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);

	return __hawk_report_show(gul_dev->hawkdev, buf);
}

static ssize_t hawk_mark_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);

	return __hawk_mark_show(gul_dev->hawkdev, buf);
}

static ssize_t hawk_mark_set(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev,
			"%s is not hex or decimal\n", buf);
	else if (val == 0 || val == 1)
		__hawk_mark_set(gul_dev->hawkdev, val);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t hawk_record_set(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev,
			"%s is not hex or decimal\n", buf);
	else if (val == 0 || val == 1)
		__hawk_record_set(gul_dev->hawkdev, val);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}
#endif

static ssize_t vspa_reset_mask_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);

	return __vspa_reset_mask_show(gul_dev, buf);
}

static ssize_t vspa_reset_mask_set(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev,
			"%s is not hex or decimal\n", buf);
	else if (val <= ((1 << GUL_VSPA_CORE_MAX) - 1))
		__vspa_reset_mask_set(gul_dev, val);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t vspa_reset_set(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev,
			"%s is not hex or decimal\n", buf);
	else if (val == 1)
		__vspa_reset_set(gul_dev, val);
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}
#ifdef DCS_DRVR_ENABLED
static ssize_t hsdcs_sinfo(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	struct gul_hif *hif = gul_dev->hif;
	if((gul_dev->dcs_priv) && (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL))
		return hsdcs_show_stats(gul_dev,buf);
	else
	{
		dev_err(gul_dev->dev, "%s This command is not supported\n", buf);
		return 0;
	}
}

static ssize_t hsadc_fullcal(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	struct gul_hif *hif = gul_dev->hif;
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev,"%s is not hex or decimal\n", buf);
	else if ((val == 1))
	{
		if((gul_dev->dcs_priv) && (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL))
			hsdcs_adc_full_recal(gul_dev);
		else
			dev_err(gul_dev->dev, "%s This comand is not supported\n", buf);
	}
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t hsadc_recal(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	struct gul_hif *hif = gul_dev->hif;
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev,
			"%s is not hex or decimal\n", buf);
	else if ((val >= 1) && (val <= 15))
	{
		if((gul_dev->dcs_priv) && (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL))
			hsdcs_adc_fast_recal(gul_dev, val);
		else
			dev_err(gul_dev->dev, "%s This command is not supported\n", buf);
	}
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t hsadc_tmon_tri(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	struct gul_hif *hif = gul_dev->hif;
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev,
			"%s is not hex or decimal\n", buf);
	else if ((val == 1))
	{
		if((gul_dev->dcs_priv) && (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL))
			hsdcs_adc_set_tmon_tri();
		else
			dev_err(gul_dev->dev, "%s This command is not supported\n", buf);
	}
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t hsadc_tmon_tri_full(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	struct gul_hif *hif = gul_dev->hif;
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		dev_err(gul_dev->dev,
			"%s is not hex or decimal\n", buf);
	else if ((val == 1))
	{
		if((gul_dev->dcs_priv) && (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL))
			hsdcs_adc_set_tmon_tri_full();
		else
			dev_err(gul_dev->dev, "%s This command is not supported\n", buf);
	}
	else
		dev_err(gul_dev->dev, "%s is invalid input\n", buf);

	return strnlen(buf, count);
}

static ssize_t hsadc_set_inc_cal(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	struct gul_hif *hif = gul_dev->hif;
	u32 val;
	int rc;

	if((gul_dev->dcs_priv) && (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL))
	{
		rc = kstrtou32(buf, 0, &val);
		if ((val == 1) || (val == 0))
		{
			hsdcs_adc_en_dis_inc_cal(gul_dev->dcs_priv,val);
		}
		else
			dev_err(gul_dev->dev, "%s is invalid input\n", buf);
	}
	else
	{
		dev_err(gul_dev->dev, "%s This command is not supported\n", buf);
	}

	return strnlen(buf, count);
}

static ssize_t hsadc_set_full_cal(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,  size_t count)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);
	struct gul_hif *hif = gul_dev->hif;
	u32 val;
	int rc;

	if((gul_dev->dcs_priv) && (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL))
	{
		rc = kstrtou32(buf, 0, &val);
		if ((val == 1) || (val == 0))
		{
			hsdcs_adc_en_dis_fullcal(gul_dev->dcs_priv,val);
		}
		else
			dev_err(gul_dev->dev, "%s is invalid input\n", buf);
	}
	else
	{
		dev_err(gul_dev->dev, "%s This command is not supported\n", buf);
	}

	return strnlen(buf, count);
}
#endif

static ssize_t yami_dev_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gul_dev *gul_dev = dev_get_drvdata(dev);

	return gul_device_dump(gul_dev, buf);
}

#ifdef HAWK_DRVR_ENABLED
static DEVICE_ATTR(hawk_list_events, 0644,
			hawk_list_events_show,
			NULL);

static DEVICE_ATTR(hawk_event, 0644,
			hawk_event_show,
			hawk_event_set);

static DEVICE_ATTR(hawk_reset_event, 0644,
			NULL,
			hawk_event_reset);

static DEVICE_ATTR(hawk_core_mask, 0644,
			hawk_core_mask_show,
			hawk_core_mask_set);

static DEVICE_ATTR(hawk_mark, 0644,
			hawk_mark_show,
			hawk_mark_set);

static DEVICE_ATTR(hawk_record, 0644,
			NULL,
			hawk_record_set);

static DEVICE_ATTR(hawk_report, 0644,
			hawk_report_show,
			NULL);
#endif

static DEVICE_ATTR(vspa_reset_mask, 0644,
			vspa_reset_mask_show,
			vspa_reset_mask_set);

static DEVICE_ATTR(vspa_reset, 0644,
			NULL,
			vspa_reset_set);
#ifdef DCS_DRVR_ENABLED
static DEVICE_ATTR(hsdcs_recal, 0644,
			NULL,
			hsadc_recal);

static DEVICE_ATTR(hsdcs_tmon_tri, 0644,
			NULL,
			hsadc_tmon_tri);

static DEVICE_ATTR(hsdcs_enable_inc_cal, 0644,
			NULL,
			hsadc_set_inc_cal);

static DEVICE_ATTR(hsdcs_enable_full_cal, 0644,
			NULL,
			hsadc_set_full_cal);

static DEVICE_ATTR(hsdcs_tmon_tri_full, 0644,
			NULL,
			hsadc_tmon_tri_full);

static DEVICE_ATTR(hsdcs_fullcal, 0644,
			NULL,
			hsadc_fullcal);

static DEVICE_ATTR(hsdcs_stats, 0444,
			hsdcs_sinfo,
			NULL);
#endif
static DEVICE_ATTR(yami_status, 0444,
			yami_dev_status_show,
			NULL);
static struct attribute *gul_sysfs_entries[] = {
	&dev_attr_warmup_status.attr,
	&dev_attr_target_log_core_0.attr,
	&dev_attr_target_log_core_1.attr,
	&dev_attr_target_log_core_2.attr,
	&dev_attr_target_log_core_3.attr,
	&dev_attr_target_log_level.attr,
	&dev_attr_target_stats_control.attr,
	&dev_attr_e200_cpu_usage.attr,
	&dev_attr_gul_id.attr,
	&dev_attr_pci_dev_name.attr,
	&dev_attr_la12xx_version.attr,
	&dev_attr_vspa_reset_mask.attr,
	&dev_attr_vspa_reset.attr,
#ifdef DCS_DRVR_ENABLED
	&dev_attr_hsdcs_recal.attr,
	&dev_attr_hsdcs_tmon_tri.attr,
	&dev_attr_hsdcs_tmon_tri_full.attr,
	&dev_attr_hsdcs_fullcal.attr,
	&dev_attr_hsdcs_stats.attr,
	&dev_attr_hsdcs_enable_inc_cal.attr,
	&dev_attr_hsdcs_enable_full_cal.attr,
#endif
	&dev_attr_yami_status.attr,
	NULL
};

static struct attribute *gul_sysfs_entries_dynamic[] = {
	&dev_attr_target_log_core_4.attr,
	&dev_attr_target_log_core_5.attr,
	NULL
};

static struct attribute *gul_sysfs_entries_rf[] = {
#ifdef RF_DRVR_ENABLED
	&dev_attr_rf_dump_hif.attr,
#endif
#ifdef RF_FR2_DRVR_ENABLED
	&dev_attr_rf_rxtx_duty_test.attr,
	&dev_attr_rf_fpga_read_regs.attr,
	&dev_attr_rf_fpga_write_regs.attr,
	&dev_attr_rf_chip_read_regs.attr,
	&dev_attr_rf_chip_write_regs.attr,
	&dev_attr_rf_vcxo.attr,
	&dev_attr_rf_tx_gain.attr,
	&dev_attr_rf_rx_gain.attr,
	&dev_attr_rf_tx_switch.attr,
	&dev_attr_rf_rx_switch.attr,
	&dev_attr_rf_stress_test_control.attr,
	&dev_attr_rf_set_freq.attr,
	&dev_attr_rf_rx_agc_gain.attr,
	&dev_attr_rf_pll_auto_tune.attr,
	&dev_attr_rf_bbk_index_tx.attr,
	&dev_attr_rf_bbk_index_rx.attr,
	&dev_attr_rf_trgt_brdcast_tx_fe.attr,
	&dev_attr_rf_trgt_brdcast_rx_fe.attr,
	&dev_attr_rf_pbk_mode_tx.attr,
	&dev_attr_rf_pbk_mode_rx.attr,
	&dev_attr_rf_sync_reset_pbk_mode_tx.attr,
	&dev_attr_rf_sync_reset_pbk_mode_rx.attr,
	&dev_attr_rf_inc_pbk_index.attr,
	&dev_attr_rf_cfg_dynamic_ctrl.attr,
	&dev_attr_rf_write_list_to_pbk_memory_tx.attr,
	&dev_attr_rf_write_list_to_pbk_memory_rx.attr,
	&dev_attr_rf_cfg_beam.attr,
	&dev_attr_rf_enable_disable_bg_task.attr,
#endif
	NULL
};


#ifdef HAWK_DRVR_ENABLED
static struct attribute *gul_sysfs_entries_hawk[] = {
	&dev_attr_hawk_list_events.attr,
	&dev_attr_hawk_event.attr,
	&dev_attr_hawk_reset_event.attr,
	&dev_attr_hawk_core_mask.attr,
	&dev_attr_hawk_mark.attr,
	&dev_attr_hawk_record.attr,
	&dev_attr_hawk_report.attr,
	NULL
};
#endif

static struct kobj_attribute yami_version_attr = __ATTR(yami_version, 0444,
						yami_version_show, NULL);

static struct kobj_attribute yami_status_attr = __ATTR(yami_status, 0444,
						yami_status_show, NULL);

static struct kobj_attribute modem_status_attr = __ATTR(modem_status, 0444,
						modem_status_show, NULL);

static struct kobj_attribute ipc_status_attr = __ATTR(ipc_status, 0444,
						ipc_status_show, NULL);

static struct attribute *yami_sysfs_entries[] = {
	&yami_version_attr.attr,
	&yami_status_attr.attr,
	&modem_status_attr.attr,
	&ipc_status_attr.attr,
	NULL
};

struct attribute_group gul_attribute_group_dynamic = {
	.name = "gulsysfs",
	.attrs = gul_sysfs_entries_dynamic,
};

struct attribute_group gul_attribute_group_rf = {
	.name = "gulsysfs",
	.attrs = gul_sysfs_entries_rf,
};

#ifdef HAWK_DRVR_ENABLED
struct attribute_group gul_attribute_group_hawk = {
	.name = "hawksysfs",
	.attrs = gul_sysfs_entries_hawk,
};
#endif

struct attribute_group gul_attribute_group = {
	.name = "gulsysfs",
	.attrs = gul_sysfs_entries,
};

struct attribute_group yami_attribute_group = {
	.attrs = yami_sysfs_entries,
};

int gul_init_sysfs(struct gul_dev *gul_dev)
{
	int rc = 0;
	struct gul_hif *hif = gul_dev->hif;

	dev_set_drvdata(gul_dev->dev, gul_dev);
	rc = sysfs_create_group(&gul_dev->pdev->dev.kobj, &gul_attribute_group);
	if (rc) {
		dev_err(gul_dev->dev, "Failed to create sysfs group\n");
		goto out;
	}

	if (!rfic_disable) {
		rc = sysfs_merge_group(&gul_dev->pdev->dev.kobj, &gul_attribute_group_rf);

		if (rc) {
			dev_err(gul_dev->dev, "Failed to merge sysfs group rf\n");
			goto out;
		}
	}

	if (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL) {
		rc = sysfs_merge_group(&gul_dev->pdev->dev.kobj, &gul_attribute_group_dynamic);

		if (rc) {
			dev_err(gul_dev->dev, "Failed to merge sysfs group dynamic\n");
			goto out;
		}
	}

	dev_dbg(gul_dev->dev, "Created sysfs group %s\n",
			gul_attribute_group_rf.name);
#ifdef HAWK_DRVR_ENABLED
	rc = sysfs_create_group(&gul_dev->pdev->dev.kobj,
			&gul_attribute_group_hawk);
	if (rc) {
		dev_err(gul_dev->dev, "Failed to create sysfs group hawk\n");
		goto out;
	}
	dev_dbg(gul_dev->dev, "Created sysfs group %s\n",
			gul_attribute_group_hawk.name);
#endif
out:
	return rc;
}

void gul_remove_sysfs(struct gul_dev *gul_dev)
{
	struct gul_hif *hif = gul_dev->hif;

	if (!rfic_disable)
		sysfs_unmerge_group(&gul_dev->pdev->dev.kobj, &gul_attribute_group_rf);

	if (gul_ep_get_soc_rev() == GEUL_SVR_REVB_VAL) {
		sysfs_unmerge_group(&gul_dev->pdev->dev.kobj, &gul_attribute_group_dynamic);
	}

	sysfs_remove_group(&gul_dev->pdev->dev.kobj, &gul_attribute_group);

#ifdef HAWK_DRVR_ENABLED
	sysfs_remove_group(&gul_dev->pdev->dev.kobj, &gul_attribute_group_hawk);
#endif
}

static struct kobject *yami_kobj;

int gul_init_global_sysfs(void)
{
	int rc = 0;

	yami_kobj = kobject_create_and_add("yami", NULL);
	if (!yami_kobj) {
		pr_err("Failed to crate yami_kobj\n");
		rc = -1;
		goto out;
	}
	rc = sysfs_create_group(yami_kobj, &yami_attribute_group);
	if (rc) {
		kobject_put(yami_kobj);
		goto out;
	}
	pr_info("Created sysfs group %s\n", yami_attribute_group.name);
out:
	return rc;
}

void gul_remove_global_sysfs(void)
{
	sysfs_remove_group(yami_kobj, &yami_attribute_group);
	kobject_put(yami_kobj);
}
