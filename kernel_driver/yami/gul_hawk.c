/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020, 2022-2023 NXP
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
#include "gul_hawk.h"

#define HAWK_SWCMD_TIMEOUT_MSECS    100
#define HAWK_SWCMD_TIMEOUT_RETRIES 5

static struct hawk_event hawk_event_table[] = {

	{0, 0, "Nothing"},
	{0, 0, "Processor cycles"},
	{0, 1, "Instructions completed"},
	{1, 3, "Processor cycles with 0 instructions issued"},
	{1, 3, "Processor cycles with 1 instruction issued"},
	{1, 3, "Processor cycles with 2 instructions issued"},
	{2, 3, "Instruction words fetched"},
	{2, 3, "PM_EVENT transitions"},
	{2, 3, "PM_EVENT cycles"},
	{0, 0, "Nothing"},
	{2, 3, "Branch instructions completed"},
	{2, 3, "Branch and link type instructions completed"},
	{2, 3, "Conditional branch instructions completed"},
	{2, 3, "Taken Branch instructions completed"},
	{2, 3, "Taken Conditional Branch instructions completed"},
	{2, 3, "Load instructions completed"},
	{2, 3, "Store instructions completed"},
	{2, 3, "Integer instructions completed"},
	{2, 3, "Multiply instructions completed"},
	{2, 3, "Divide instructions completed"},
	{2, 3, "Divide instruction execution cycles"},
	{2, 3, "EFPU FP instructions completed"},
	{2, 3, "Cycles decode stalled due to no instructions available"},
	{2, 3, "Cycles issue stalled, not due to empty instruction buffer"},
	{2, 3, "Dcache linefills"},
	{2, 3, "Dcache load hits"},
	{2, 3, "Store buffer full stalls"},
	{2, 3, "Icache linefills"},
	{2, 3, "Number of Instruction fetches"},
	{2, 3, "BIU instruction-side transfers"},
	{2, 3, "BIU instruction-side cycles"},
	{2, 3, "BIU data-side transfers"},
	{2, 3, "BIU data-side cycles"},
	{2, 3, "BIU single-beat write cycles"},
	{0, 0, "PMC0 rollover"},
	{0, 0, "PMC1 rollover"},
	{0, 0, "PMC2 rollover"},
	{0, 0, "PMC3 rollover"},
	{2, 3, "Interrupts taken"},
	{2, 3, "External input interrupts taken"},
	{2, 3, "Critical input interrupts taken"},
	{2, 3, "Cycles in which MSREE=0"},
	{2, 3, "Cycles in which MSRCE=0"},
};


struct hawk_sw_cmd_desc *hawk_get_swcmd(struct hawkdev *hawkdev)
{
	struct hawk_sw_cmd_desc *sw_cmd = 0;

	sw_cmd = &hawkdev->host_data.cmd_local;
	memset(sw_cmd, 0, sizeof(*sw_cmd));
	return sw_cmd;
}

void hawk_free_cmd(struct hawkdev *hawkdev, hawk_sw_cmd_desc_t *sw_cmd)
{
	int i;
	hawk_sw_cmd_desc_t *remote_cmd;

	remote_cmd = &hawkdev->p_hif->hawk_mdata.host_swcmd;
	iowrite32(HAWK_SW_CMD_STATUS_FREE, &remote_cmd);
	for (i = 0; i < E200_CORE_COUNT; i++)
		sw_cmd->status[i] = HAWK_SW_CMD_STATUS_FREE;
}

void hawk_swcmd_get_data(struct hawkdev *hawkdev, struct hawk_sw_cmd_desc *sw_cmd,
		       int data_size)
{
	struct hawk_sw_cmd_desc *remote_cmd;
	u32 *data_src, *data_dest, words, i;

	remote_cmd = &hawkdev->p_hif->hawk_mdata.host_swcmd;
	data_src = (u32 *) &remote_cmd->data[0];
	data_dest = (u32 *) &sw_cmd->data[0];
	words = data_size >> 2;
	for (i = 0; i < words; i++) {
		data_dest[i] = ioread32be(data_src);

		dev_dbg(hawkdev->gul_dev->dev, "HAWK: %d: 0x%x, 0x%x\n", i,
			data_dest[i], *data_src);
		data_src++;
	}
}

void hawk_raise_modem_irq(struct hawkdev *hawkdev)
{
	raise_modem_msi(hawkdev->gul_dev, MSI_TYPE_A, HOST_MSI_HAWK);
}

int hawk_send_swcmd(struct hawkdev *hawkdev, struct hawk_sw_cmd_desc *sw_cmd,
		  int data_size)
{
	struct gul_dev *gul_dev = hawkdev->gul_dev;
	int ret = 0, retries = HAWK_SWCMD_TIMEOUT_RETRIES;
	struct hawk_sw_cmd_desc *remote_cmd;
	u32 *cmd_dest, *cmd_src;
	int cmd_size, words, i;
	hawk_sw_cmd_status_t cmd_status;

	remote_cmd = &hawkdev->p_hif->hawk_mdata.host_swcmd;

	for (i = 0; i < E200_CORE_COUNT; i++) {
		if (ioread32be(&remote_cmd->status[i]) != HAWK_SW_CMD_STATUS_FREE) {
			dev_err(gul_dev->dev, "HAWK: mdata host swcmd busy [%d]\n",
					ioread32be(&remote_cmd->status));

			ret = -EBUSY;
			goto busy_out;
		}
	}

	sw_cmd->core_mask = hawkdev->host_data.core_mask;
	for (i = 0; i < E200_CORE_COUNT; i++) {
		if ((1 << i) & sw_cmd->core_mask)
			sw_cmd->status[i]  = HAWK_SW_CMD_STATUS_POSTED;
	}

	cmd_size = sizeof(struct hawk_sw_cmd_desc) -
			sizeof(hawkdev->p_hif->hawk_mdata.host_swcmd.data)
			+ data_size;
	words = cmd_size >> 2;

	cmd_dest = (u32 *) sw_cmd;
	cmd_src = (u32 *) remote_cmd;
	for (i = 0; i < words; i++) {
		iowrite32be(*cmd_dest, cmd_src);
		dev_dbg(gul_dev->dev, "HAWK: cmd_dest 0x%x, cmd_src 0x%x\n",
			*cmd_dest, *cmd_dest);
		cmd_dest++;
		cmd_src++;
	}

	dma_wmb();
	hawk_raise_modem_irq(hawkdev);
	dma_wmb();

	/* Wait Modem HAWK driver to complete processing */
	for (i = 0; i < E200_CORE_COUNT; i++) {
		if ((1 << i) & sw_cmd->core_mask) {
			cmd_status = ioread32be(&remote_cmd->status[i]);
			while (retries &&
					(cmd_status != HAWK_SW_CMD_STATUS_DONE)) {
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(msecs_to_jiffies(HAWK_SWCMD_TIMEOUT_MSECS));
				cmd_status = ioread32be(&remote_cmd->status);
				retries--;
			}
			if (!retries &&
					((cmd_status != HAWK_SW_CMD_STATUS_DONE))) {
				dev_err(gul_dev->dev, "HAWK: swcmd 0x%x timed out\n",
						sw_cmd->cmd);
				ret = -EBUSY;
				goto out;
			}
			dev_dbg(gul_dev->dev, "HAWK: swcmd 0x%x done\n", sw_cmd->cmd);

			ret = ioread32be(&remote_cmd->result[i]);
			if (ret != HAWK_SW_CMD_RESULT_OK) {
				dev_err(gul_dev->dev, "HAWK: CMD response error. result[%d]\n",
						ret);
			}
		}
	}
out:
	for (i = 0; i < E200_CORE_COUNT; i++) {
		iowrite32be(HAWK_SW_CMD_STATUS_FREE, &remote_cmd->status[i]);
	}
busy_out:
	return ret;
}

int __hawk_list_events_show(struct hawkdev *hawkdev, char *buf)
{
	int i, len = 0;

	len += sprintf((buf + len), "Supported Events:\n");

	for (i = 0; i < ARRAY_SIZE(hawk_event_table); i++) {
		len += sprintf((buf + len), " C%u-%u:%u %s\n",
				hawk_event_table[i].event_output_start,
				hawk_event_table[i].event_output_end,
				i,
				hawk_event_table[i].event_string);
	}
	return len;
}

int __hawk_events_show(struct hawkdev *hawkdev, char *buf)
{
	int i, len = 0;

	for (i = 0; i < hawkdev->host_data.cmd_stat.event_count; i++) {
		len += sprintf((buf + len), "C%u:%u\t%s\n", hawkdev->host_data.cmd_stat.event_output[i],
				hawkdev->host_data.cmd_stat.event[i],
				hawk_event_table[i].event_string);
	}

	return len;
}

int __hawk_event_set(struct hawkdev *hawkdev, u32 event_output, u32 event)
{
	if (hawkdev->host_data.cmd_stat.event_count >= HAWK_EVENT_MAX_COUNT) {
		dev_err(hawkdev->gul_dev->dev, "HAWK: event array is full!\n");
		return -1;
	}

	hawkdev->host_data.cmd_stat.event_output[hawkdev->host_data.cmd_stat.event_count]
		= event_output;
	hawkdev->host_data.cmd_stat.event[hawkdev->host_data.cmd_stat.event_count] = event;
	hawkdev->host_data.cmd_stat.event_count++;
	return 0;
}

int __hawk_event_reset(struct hawkdev *hawkdev)
{
	if (!hawkdev->host_data.cmd_stat.event_count) {
		dev_err(hawkdev->gul_dev->dev, "HAWK: event array is empty!\n");
		return -1;
	}

	memset(&hawkdev->host_data.cmd_stat, 0,
				sizeof(hawkdev->host_data.cmd_stat));
	return 0;
}

int __hawk_core_mask_show(struct hawkdev *hawkdev, char *buf)
{
	int len = 0;

	len = sprintf(buf, "%x\n", hawkdev->host_data.core_mask);
	return len;
}

int __hawk_core_mask_set(struct hawkdev *hawkdev, u32 core_mask)
{
	hawkdev->host_data.core_mask = core_mask;
	return 0;
}

int __hawk_report_show(struct hawkdev *hawkdev, char *buf)
{
	int i, j, len = 0;

	for (i = 0; i < hawkdev->host_data.cmd_stat.event_count; i++) {
		len +=
		sprintf((buf + len), "C%u:%u = ",
				hawkdev->host_data.cmd_stat.event_output[i],
				hawkdev->host_data.cmd_stat.event[i]);
		for (j = 0; j < E200_CORE_COUNT; j++) {
			if ((1 << j) & hawkdev->host_data.core_mask) {
				len += sprintf((buf + len), "%10u\t", hawkdev->host_data.cmd_stat.ctrs[j][i]);
			}
		}
		len += sprintf((buf + len), "%-64s\n", hawk_event_table[hawkdev->host_data.cmd_stat.event[i]].event_string);
	}
	return len;
}

int __hawk_mark_show(struct hawkdev *hawkdev, char *buf)
{
	int len = 0;

	if (!hawkdev->host_data.cmd_stat.event_count) {
		dev_err(hawkdev->gul_dev->dev, "HAWK: event array is empty!\n");
		return -1;
	}

	len = sprintf(buf, "%u\n", hawkdev->host_data.cmd_stat.event_mark);
	return len; }

int __hawk_mark_set(struct hawkdev *hawkdev, bool enable)
{
	struct hawk_sw_cmd_desc *sw_cmd;
	struct hawk_swcmd_stat *stat_desc;
	int ret;

	if (!hawkdev->host_data.cmd_stat.event_count) {
		dev_err(hawkdev->gul_dev->dev, "HAWK: event array is empty!\n");
		return -1;
	}

	sw_cmd = hawk_get_swcmd(hawkdev);

	sw_cmd->cmd = enable ? HAWK_SW_CMD_MARK_SET : HAWK_SW_CMD_MARK_RESET;
	stat_desc = (struct hawk_swcmd_stat *) &sw_cmd->data[0];
	memcpy(stat_desc, &hawkdev->host_data.cmd_stat, sizeof(struct hawk_swcmd_stat));
	stat_desc->event_mark = enable;
	ret = hawk_send_swcmd(hawkdev, sw_cmd, sizeof(struct hawk_swcmd_stat));
	if (!ret)
		hawkdev->host_data.cmd_stat.event_mark = stat_desc->event_mark;

	hawk_free_cmd(hawkdev, sw_cmd);
	return ret;
}

int __hawk_record_set(struct hawkdev *hawkdev, bool enable)
{
	struct hawk_sw_cmd_desc *sw_cmd;
	struct hawk_swcmd_stat *stat_desc;
	int ret;

	if (!hawkdev->host_data.cmd_stat.event_count) {
		dev_err(hawkdev->gul_dev->dev, "HAWK: event array is empty!\n");
		return -1;
	}

	sw_cmd = hawk_get_swcmd(hawkdev);

	if (enable) {
		sw_cmd->cmd = HAWK_SW_CMD_RECORD_START;
		stat_desc = (struct hawk_swcmd_stat *) &sw_cmd->data[0];
		memcpy(stat_desc, &hawkdev->host_data.cmd_stat, sizeof(struct hawk_swcmd_stat));
		ret = hawk_send_swcmd(hawkdev, sw_cmd, sizeof(struct hawk_swcmd_stat));
	} else {
		sw_cmd->cmd = HAWK_SW_CMD_RECORD_STOP;
		stat_desc = (struct hawk_swcmd_stat *) &sw_cmd->data[0];
		memcpy(stat_desc, &hawkdev->host_data.cmd_stat, sizeof(struct hawk_swcmd_stat));
		ret = hawk_send_swcmd(hawkdev, sw_cmd, sizeof(struct hawk_swcmd_stat));

		if (ret == HAWK_SW_CMD_RESULT_OK) {
			hawk_swcmd_get_data(hawkdev, sw_cmd, sizeof(struct hawk_swcmd_stat));
			stat_desc = (struct hawk_swcmd_stat *) &sw_cmd->data[0];
			memcpy(&hawkdev->host_data.cmd_stat.ctrs, &stat_desc->ctrs,
				       sizeof(stat_desc->ctrs));
		}
	}

	hawk_free_cmd(hawkdev, sw_cmd);
	return ret;
}

int gul_hawk_init_mdata(struct hawkdev *hawkdev)
{
	struct gul_dev *gul_dev = hawkdev->gul_dev;
	struct gul_hif *hif;
	struct hawk_host_if *p_hif;
	int ret = 0;

	hif = gul_dev->hif;
	p_hif = &hif->hawk_hif;

	hawkdev->p_hif = p_hif;
	hawkdev->host_data.core_mask = 0xf;

	dev_dbg(gul_dev->dev, "HAWK: p_hif  0x%p\n", p_hif);

	return ret;
}

int gul_hawk_probe(struct gul_dev *gul_dev, int virq_count,
		   struct virq_evt_map *virq_map)
{
	int ret = 0;
	struct hawkdev *hawkdev;

	dev_dbg(gul_dev->dev, "HAWK: Inside %s function\n", __func__);

	hawkdev = kzalloc(sizeof(struct hawkdev), GFP_KERNEL);
	if (!hawkdev) {
		printk(KERN_CRIT "Memory allocation failure for hawkdev\n");
		return -ENOMEM;
	}
	hawkdev->gul_dev = gul_dev;
	gul_dev->hawkdev = hawkdev;

	ret = gul_hawk_init_mdata(hawkdev);
	if (ret)
		goto fail;

	dev_dbg(gul_dev->dev, "HAWK: Exiting function %s\n", __func__);

	return ret;
fail:
	return ret;
}

int gul_hawk_remove(struct gul_dev *gul_dev)
{
	struct hawkdev *hawkdev;

	dev_dbg(gul_dev->dev, "HAWK: Inside %s function\n", __func__);

	hawkdev = gul_dev->hawkdev;
	if (hawkdev) {
		kfree(hawkdev);
		gul_dev->hawkdev = NULL;
	}

	dev_dbg(gul_dev->dev, "HAWK: Exiting function %s\n", __func__);
	return 0;
}


