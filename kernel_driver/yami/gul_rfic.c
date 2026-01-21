/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2026 NXP
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
#include "gul_rfic.h"
#include <rfdev_ioctl.h>

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
#include <linux/delay.h>

#include "gul_rf_dspi.h"

#define DEVICE_NAME_LEN		32
#define GUL_MINOR_START		0
#define RF_SWCMD_TIMEOUT_MSECS    100
#define RF_SWCMD_TIMEOUT_RETRIES 5

#define GUL_RF_INIT_WAIT_RETRIES 10
#define GUL_RF_INIT_WAIT_TIMEOUT 100

#define SLOT_DURATION			125
#define PLL_AUTO_TUNE_ITER_COUNT	2
#define TX_RX_SWITCH_ITER_COUNT		20

#define RX_LINE_LOW_DELAY_US	25

static uint32_t gul_rfic_major;
static uint32_t gul_rfic_minor;
static dev_t rfdevnr;
static uint32_t gul_nr_dev;
static uint8_t rfic_minor_index;
static uint8_t in_use_minor[MAX_MODEM];


#ifdef RF_FR2_DRVR_ENABLED
static void rf_raise_modem_irq(struct rfdev *rfdev)
{
	raise_modem_msi(rfdev->gul_dev, MSI_TYPE_A, HOST_MSI_RF);
}

/*
 * This function will use shared memory to send RFIC SW command
 */
static int rf_send_swcmd(struct rfdev *rfdev, struct rf_sw_cmd_desc *sw_cmd,
		  int data_size)
{
	int ret = 0, retries = RF_SWCMD_TIMEOUT_RETRIES;
	struct rf_sw_cmd_desc *remote_cmd;
	int cmd_size, i;
	struct rf_host_stats *stats = &rfdev->host_stats;
	struct gul_dev *gul_dev = rfdev->gul_dev;
	u32 *cmd_wrd_l, *cmd_wrd_r;

	remote_cmd = &rfdev->r_hif->rf_mdata.host_swcmd;

	if (ioread32be(&remote_cmd->status) != RF_SW_CMD_STATUS_FREE) {
		if (ioread32be(&remote_cmd->status) == RF_SW_CMD_STATUS_DONE) {
			/* This can happen when command processing takes more
			 * time than timeout configured in this driver. We can
			 * process successfully in this case as DONE from MODEM
			 * means FREE for this driver.
			 */
			dev_warn(gul_dev->dev, "RFIC: timeout is insufficient\n");
		} else {
			dev_err(gul_dev->dev, "RFIC: mdata host swcmd busy [%d]\n",
				ioread32be(&remote_cmd->status));
			stats->sw_cmds_desc_busy++;
			ret = -EBUSY;
			goto busy_out;
		}
	}

	sw_cmd->timeout = RFIC_REMOTE_CMD_TIMEOUT;
	sw_cmd->status  = RF_SW_CMD_STATUS_POSTED;
	sw_cmd->flags = RF_CMD_FLAGS_REMOTE;

	cmd_size = rfdev->swcmd_common_size + data_size;
	/*cmd_size is needed in words*/
	cmd_size = cmd_size / 4;
	cmd_wrd_l = (u32 *) sw_cmd;
	cmd_wrd_r = (u32 *) remote_cmd;
	dev_dbg(gul_dev->dev, "cmd %d, cmdsize %d\n",
		sw_cmd->cmd, cmd_size);
	dev_dbg(gul_dev->dev, "cmd dump:\n");
	/*Modem E200 cores are big-endian, thus changing endianness.
	 *Copying local command to remote command is sub-optimal, but because
	 *of different endianness it is better to handle change of endianess via
	 *a copy so that code is less error prone. Moreover there is no real
	 *time or low latency requirement of this interface, so a copy here is
	 *fine
	 */
	for (i = 0; i < cmd_size; i++) {
		iowrite32be(*cmd_wrd_l, cmd_wrd_r);
		dev_dbg(gul_dev->dev, "cmd_wrd_l 0x%x, cmd_wrd_r 0x%x\n",
			*cmd_wrd_l, *cmd_wrd_r);
		cmd_wrd_l++;
		cmd_wrd_r++;
	}
	dma_wmb();
	rf_raise_modem_irq(rfdev);
	dma_wmb();
	/* Wait Modem RF driver to complete processing */
	while (retries &&
	       (ioread32be(&remote_cmd->status) != RF_SW_CMD_STATUS_DONE)) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(RF_SWCMD_TIMEOUT_MSECS)
				);
		retries--;
	}
	if (!retries &&
	    ((ioread32be(&remote_cmd->status)  != RF_SW_CMD_STATUS_DONE))) {
		dev_err(gul_dev->dev, "swcmd 0x%x timed out\n",
			sw_cmd->cmd);
		stats->sw_cmds_timed_out++;
		ret = -EBUSY;
		goto out;
	}
	dev_dbg(gul_dev->dev, "swcmd 0x%x done\n", sw_cmd->cmd);
	stats->sw_cmds_tx++;

	ret = ioread32be(&remote_cmd->result);
	if (ret != RF_SW_CMD_RESULT_OK) {
		dev_err(gul_dev->dev, "%s: CMD response error. result[%d]\n",
			__func__, ret);
	}

out:
	iowrite32be(RF_SW_CMD_STATUS_FREE, &remote_cmd->status);
busy_out:
	return ret;
}

static void rf_swcmd_get_data(struct rfdev *rfdev, struct rf_sw_cmd_desc *sw_cmd,
		       int data_size)
{
	struct rf_sw_cmd_desc *remote_cmd;
	u32 *data_r, *data_l, size_wrds, i;

	remote_cmd = &rfdev->r_hif->rf_mdata.host_swcmd;
	data_r = (u32 *) &remote_cmd->data[0];
	data_l = (u32 *) &sw_cmd->data[0];
	size_wrds = data_size >> 2;
	for (i = 0; i < size_wrds; i++) {
		data_l[i] = ioread32be(data_r);

		dev_dbg(rfdev->gul_dev->dev, "%d: 0x%x, 0x%x\n", i,
			data_l[i], *data_r);
		data_r++;
	}
}

/* Get the sw_cmd from rf metadata on modem. This function helps in sending
 * swcmd to modem metadata without copy also takes lock for sw_cmd.
 * Locking - Anybody using swcmd should use rf_get_cmd() which takes lock,
 * post cmd, and use rf_free_cmd() to free the lock.
 */
static struct rf_sw_cmd_desc *rf_get_swcmd(struct rfdev *rfdev)
{
	struct rf_sw_cmd_desc *sw_cmd = 0;

	if (down_interruptible(&rfdev->mdata_lock)) {
		dev_err(rfdev->gul_dev->dev, "Didn't get mdata lock\n");
		goto out;
	}
	sw_cmd = &rfdev->cmd_local;
	memset(sw_cmd, 0, sizeof(*sw_cmd));
	sw_cmd->core_id = 4;
out:
	return sw_cmd;
}

static void rf_free_cmd(struct rfdev *rfdev, rf_sw_cmd_desc_t *sw_cmd)
{
	rf_sw_cmd_desc_t *remote_cmd;

	remote_cmd = &rfdev->r_hif->rf_mdata.host_swcmd;
	iowrite32(RF_SW_CMD_STATUS_FREE, &remote_cmd);
	sw_cmd->status = RF_SW_CMD_STATUS_FREE;
	up(&rfdev->mdata_lock);
}
#endif

static int gul_rfic_open(struct inode *inode, struct file *filp)
{
	struct rfdev *dev = container_of(inode->i_cdev,
					 struct rfdev, cdev);

	filp->private_data = dev;

	return 0;
}

static int gul_rfic_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;

	return 0;
}

void __rf_dump_hif(struct rfdev *rfdev)
{
	int i;

	dev_info(rfdev->gul_dev->dev, "Host descriptor\n");
	print_hex_dump(KERN_INFO, NULL, DUMP_PREFIX_NONE, 16, 1,
			&(rfdev->r_hif->rf_mdata.host_swcmd),
			sizeof(struct rf_sw_cmd_desc), false);

	for (i = 0; i < E200_CORE_COUNT; i++) {
		dev_info(rfdev->gul_dev->dev, "e200 core %d descriptor\n", i);
		print_hex_dump(KERN_INFO, NULL, DUMP_PREFIX_NONE, 16, 1,
				&(rfdev->r_hif->rf_prv_mdata.swcmd_descs[i]),
				sizeof(struct rf_sw_cmd_desc), false);
	}

	dev_info(rfdev->gul_dev->dev, "Modem RF device\n");
	dev_info(rfdev->gul_dev->dev, "RF Card Type: %d\n",
		ioread32be(&rfdev->r_hif->rf_mdata.rfic_type));
	print_hex_dump(KERN_INFO, NULL, DUMP_PREFIX_NONE, 16, 1,
			&(rfdev->r_hif->rf_prv_mdata.rfdev[0]),
			128, false);
}

#ifdef RF_FR2_DRVR_ENABLED
int __rf_read_regs(struct rfdev *rfdev, u16 addr, int count,
		   u32 *buf)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret = 0, i, size, data_size;
	struct rf_swcmd_reg_rw *reg_rw;

	dev_dbg(rfdev->gul_dev->dev, "%s: reg %x, len %x\n",
		__func__, addr, count);
	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_READ_REG;
	reg_rw = (struct rf_swcmd_reg_rw *)&sw_cmd->data[0];
	reg_rw->count = count;
	for (i = 0; i < count; i++)
		reg_rw->reg_data[i].addr = (addr + i);

	data_size = (count * sizeof(struct rf_reg_rw_data));
	size = 4 + data_size;
	ret = rf_send_swcmd(rfdev, sw_cmd, size);
	if (ret) {
		dev_err(rfdev->gul_dev->dev, "cmd %d failed, ret %d\n",
			sw_cmd->cmd, ret);
		goto out;
	}

	rf_swcmd_get_data(rfdev, sw_cmd, size);

	for (i = 0; i < count; i++)
		iowrite32be(reg_rw->reg_data[i].val,
			&(reg_rw->reg_data[i].val));

	/*Read operations are grossly inefficient with 2 copies.
	 *but it is done to keep endian-ness conversion hidden.
	 *This should not be problem because read operation is
	 *just for debug
	 */
	for (i = 0; i < count; i++) {
		buf[i] = reg_rw->reg_data[i].val & MV_REG_VAL_MASK;
		dev_dbg(rfdev->gul_dev->dev, "%d: 0x%x\n", i,
			reg_rw->reg_data[i].val);
	}

out:
	rf_free_cmd(rfdev, sw_cmd);
	return ret;
}

int __rf_read_dspi_regs(struct rfdev *rfdev, u16 addr, int count, u32 *buf)
{
	int32_t i, ret;
	struct gul_mem_region_info *ccsr_mem =
		&(rfdev->gul_dev->mem_regions[GUL_MEM_REGION_CCSR]);

	for (i = 0; i < count; i++) {
		ret = rf_movandi_read_reg(DSPI4, ccsr_mem->vaddr, addr,
					  (uint8_t *)(&buf[i]));
		if (ret)
			break;
	}

	return 0;
}

int rf_read_regs(struct rfdev *rfdev,
		 struct rif_reg_buf *reg_buf, u32 *buf)
{

	u16 addr = reg_buf->addr & MV_REG_ADDR_MASK;

	//return __rf_read_regs(rfdev, addr, reg_buf->count, buf);
	return __rf_read_dspi_regs(rfdev, addr, reg_buf->count, buf);
}

int __rf_write_regs(struct rfdev *rfdev, int count,
		    struct rif_write_reg_buf *reg_buf)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret = 0, i, size;
	struct rf_swcmd_reg_rw *reg_rw;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_WRITE_REG;
	reg_rw = (struct rf_swcmd_reg_rw *)&sw_cmd->data[0];
	reg_rw->count = count;
	for (i = 0; i < count; i++) {
		reg_rw->reg_data[0].addr = reg_buf->addr & MV_REG_ADDR_MASK;
		reg_rw->reg_data[0].val = reg_buf->data & MV_REG_VAL_MASK;
	}
	size = 4 + (count * sizeof(struct rf_reg_rw_data));
	ret = rf_send_swcmd(rfdev, sw_cmd, size);

	rf_free_cmd(rfdev, sw_cmd);
	return ret;
}

int __rf_write_dspi_regs(struct rfdev *rfdev, int count,
		    struct rif_write_reg_buf *reg_buf)
{
	int32_t i, ret;
	struct gul_mem_region_info *ccsr_mem =
		&(rfdev->gul_dev->mem_regions[GUL_MEM_REGION_CCSR]);

	for (i = 0; i < count; i++) {
		ret = rf_movandi_write_reg(DSPI4,
					   ccsr_mem->vaddr,
					   (uint16_t)(reg_buf[i].addr),
					   (uint8_t)(reg_buf[i].data));
		if (ret)
			break;
	}

	return 0;
}

int rf_write_regs(struct rfdev *rfdev,
		  struct rif_write_reg_buf *reg_buf)
{


	/*XXX:TBD - optimization needs to be done - insead of sending one
	 *register write accumulate 64 writes in a buffer and send them
	 *This will speed up init script execution. It can be done in user
	 *space but in that case rfutil will not remain portable on Aberdeen
	 *and Inverness
	 **/
	//return __rf_write_regs(rfdev, 1, reg_buf);
	return __rf_write_dspi_regs(rfdev, 1, reg_buf);
}

int rf_read_chip_regs(struct rfdev *rfdev, u16 addr, int count,
		      u32 *buf, int chip)
{
	struct gul_dev *gul_dev = rfdev->gul_dev;
	int ret = 0;

	/*XXX:TBD - Implement a SWCMD for chip read & use it here*/
	dev_info(gul_dev->dev, "Not yet implemented\n");

	return ret;
}

int rf_write_chip_regs(struct rfdev *rfdev, int count,
		       struct rif_write_reg_buf *reg_buf, int chip)
{
	struct gul_dev *gul_dev = rfdev->gul_dev;
	int ret = 0;

	/*XXX:TBD - Implement a SWCMD for chip write & use it here*/
	dev_info(gul_dev->dev, "Not yet implemented\n");

	return ret;

}

int __rf_set_vcxo_dac(struct rfdev *rfdev, u16 index)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_set_vcxo *vcxo_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_VCXO;
	vcxo_desc = (struct rf_swcmd_set_vcxo *)&sw_cmd->data[0];
	vcxo_desc->index = index;

	ret = rf_send_swcmd(rfdev, sw_cmd, 4);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_set_tx_gain(struct rfdev *rfdev, u8 lut_index)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_set_tx_gain *tx_gain_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_TX_GAIN;
	tx_gain_desc = (struct rf_swcmd_set_tx_gain *)&sw_cmd->data[0];
	tx_gain_desc->lut_index = lut_index;

	ret = rf_send_swcmd(rfdev, sw_cmd, 4);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_set_rx_gain(struct rfdev *rfdev, u8 beam_mask, u8 lut_index)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_set_rx_gain *rx_gain_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_RX_GAIN;
	rx_gain_desc = (struct rf_swcmd_set_rx_gain *)&sw_cmd->data[0];
	rx_gain_desc->lut_index = lut_index;
	rx_gain_desc->beam_mask = beam_mask;

	ret = rf_send_swcmd(rfdev, sw_cmd, 8);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_switch_tx(struct rfdev *rfdev)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SWITCH_TX;

	ret = rf_send_swcmd(rfdev, sw_cmd, 0);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_switch_rx(struct rfdev *rfdev)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SWITCH_RX;

	ret = rf_send_swcmd(rfdev, sw_cmd, 0);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_control_stress_test(struct rfdev *rfdev, u32 start)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	if (start)
		sw_cmd->cmd = RF_SW_CMD_START_TEST;
	else
		sw_cmd->cmd = RF_SW_CMD_END_TEST;

	ret = rf_send_swcmd(rfdev, sw_cmd, 0);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_enable_disable_bg_task(struct rfdev *rfdev, u32 start)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_enable_disable_bg_task *cmd_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_ENABLE_DISABLE_BG_TASK;
	cmd_desc = (struct rf_swcmd_enable_disable_bg_task *)&sw_cmd->data[0];
	cmd_desc->en_dis = start;

	ret = rf_send_swcmd(rfdev, sw_cmd, sizeof(*cmd_desc));

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_set_target_broadcast_rx_fe(struct rfdev *rfdev)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_TARGET_BRDCAST_RX_FE;

	ret = rf_send_swcmd(rfdev, sw_cmd, 0);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_tx_rx_seq_cmd_init(struct rfdev *rfdev)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_set_tx_rx_init_seq_cmd *cmd_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_TX_RX_INIT_SEQ_CMD;
	cmd_desc = (struct rf_swcmd_set_tx_rx_init_seq_cmd *)&sw_cmd->data[0];
	cmd_desc->tx_on_seq_no = 30;
	cmd_desc->tx_off_seq_no = 34;
	cmd_desc->rx_on_seq_no = 12;
	cmd_desc->rx_off_seq_no = 16;
	cmd_desc->logen_select_beam = 3;

	ret = rf_send_swcmd(rfdev, sw_cmd, sizeof(*cmd_desc));

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_enable_agc(struct rfdev *rfdev)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_ENABLE_AGC;

	ret = rf_send_swcmd(rfdev, sw_cmd, 0);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_set_freq_tx(struct rfdev *rfdev,
		     u32 rf_freq_khz, u32 ppm,
		     u32 ref_freq_khz, u32 init)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_set_freq_tx *freq_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_FREQ_TX;
	freq_desc = (struct rf_swcmd_set_freq_tx *)&sw_cmd->data[0];
	freq_desc->rf_freq_khz = rf_freq_khz;
	freq_desc->ref_freq_khz = ref_freq_khz;
	freq_desc->ppm = ppm;
	freq_desc->init = init;

	ret = rf_send_swcmd(rfdev, sw_cmd, sizeof(struct rf_swcmd_set_freq_tx));

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_set_rx_agc_gain_index(struct rfdev *rfdev,
			       u8 beam_mask, u8 lut_index)
{
	int ret;

	ret = __rf_switch_tx(rfdev);
	if (ret != RF_SW_CMD_RESULT_OK)
		goto out;
	ret = __rf_switch_rx(rfdev);
	if (ret != RF_SW_CMD_RESULT_OK)
		goto out;
	ret = __rf_set_rx_gain(rfdev, beam_mask, lut_index);
	if (ret != RF_SW_CMD_RESULT_OK)
		goto out;
	udelay(5);
	ret = __rf_switch_tx(rfdev);
	if (ret != RF_SW_CMD_RESULT_OK)
		goto out;
	udelay(RX_LINE_LOW_DELAY_US);
	ret = __rf_switch_rx(rfdev);
	if (ret != RF_SW_CMD_RESULT_OK)
		goto out;

out:
	if (ret)
		dev_err(rfdev->gul_dev->dev, "%s: RX AGC Error. result[%d]\n",
			__func__, ret);
	return ret;
}

int __rf_get_pll_status(struct rfdev *rfdev)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_GET_PLL_LOCK_STATUS;

	ret = rf_send_swcmd(rfdev, sw_cmd, 0);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rf_set_auto_tune_pll(struct rfdev *rfdev, u8 iter)
{
	int ret = 0, i, j;

	for (i = 0; i < iter * PLL_AUTO_TUNE_ITER_COUNT; i++) {
		for (j = 0; j < TX_RX_SWITCH_ITER_COUNT; j++) {
			ret = __rf_switch_rx(rfdev);
			if (ret != RF_SW_CMD_RESULT_OK)
				goto out;
			udelay(SLOT_DURATION);
			ret = __rf_switch_tx(rfdev);
			if (ret != RF_SW_CMD_RESULT_OK)
				goto out;
			udelay(SLOT_DURATION);
		}
	}

out:
	if (ret)
		dev_err(rfdev->gul_dev->dev, "%s: PLL Tune Error. result[%d]\n",
			__func__, ret);

	return ret;

}

int __rfic_set_beambook_index_tx(struct rfdev *rfdev, u8 bbk_index)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_set_bbk_index_tx *bbk_index_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_BBK_INDEX_TX;
	bbk_index_desc = (struct rf_swcmd_set_bbk_index_tx *)&sw_cmd->data[0];
	bbk_index_desc->index = bbk_index;

	ret = rf_send_swcmd(rfdev, sw_cmd, 4);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rfic_set_beambook_index_rx(struct rfdev *rfdev, u8 bbk_index)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_set_bbk_index_rx *bbk_index_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_BBK_INDEX_RX;
	bbk_index_desc = (struct rf_swcmd_set_bbk_index_rx *)&sw_cmd->data[0];
	bbk_index_desc->index = bbk_index;

	ret = rf_send_swcmd(rfdev, sw_cmd, 4);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rfic_set_target_broadcast_tx_fe(struct rfdev *rfdev)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_TARGET_BRDCAST_TX_FE;

	ret = rf_send_swcmd(rfdev, sw_cmd, 0);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rfic_set_target_broadcast_rx_fe(struct rfdev *rfdev)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_TARGET_BRDCAST_RX_FE;

	ret = rf_send_swcmd(rfdev, sw_cmd, 0);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rfic_set_pbk_mode_tx(struct rfdev *rfdev, rfic_pbk_modes_t mode)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_set_pbk_mode *pbk_mode_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_PBK_MODE_TX;
	pbk_mode_desc = (struct rf_swcmd_set_pbk_mode *)&sw_cmd->data[0];
	pbk_mode_desc->mode = mode;

	ret = rf_send_swcmd(rfdev, sw_cmd, 4);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rfic_set_pbk_mode_rx(struct rfdev *rfdev, rfic_pbk_modes_t mode)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_set_pbk_mode *pbk_mode_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_PBK_MODE_RX;
	pbk_mode_desc = (struct rf_swcmd_set_pbk_mode *)&sw_cmd->data[0];
	pbk_mode_desc->mode = mode;

	ret = rf_send_swcmd(rfdev, sw_cmd, 4);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rfic_increment_pbk_index(struct rfdev *rfdev)
{
	struct rf_sw_cmd_desc *sw_cmd;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_INC_PBK_INDEX;

	ret = rf_send_swcmd(rfdev, sw_cmd, 0);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rfic_config_dynamic_controller(struct rfdev *rfdev,
				     rfic_cfg_dynamic_ctrl_t dynamic_op,
				     u8 enable)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_cfg_dynamic_ctrl *dynamic_ctrl_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_CFG_DYNAMIC_CTRL;
	dynamic_ctrl_desc =
		(struct rf_swcmd_cfg_dynamic_ctrl *)&sw_cmd->data[0];
	dynamic_ctrl_desc->dynamic_ops = dynamic_op;
	dynamic_ctrl_desc->enable_disable = enable;

	ret = rf_send_swcmd(rfdev, sw_cmd, 8);

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rfic_write_list_to_pbk_memory_tx(struct rfdev *rfdev, u8 pbk_length)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_write_list_to_pbk_mem *write_list_to_pbk_mem;
	uint8_t i;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_WRITE_LIST_TO_PBK_MEM_TX;
	write_list_to_pbk_mem =
		(struct rf_swcmd_write_list_to_pbk_mem *)&sw_cmd->data[0];
	write_list_to_pbk_mem->bbk_list_params.length = pbk_length;

	for (i = 0; i < pbk_length; i++)
		write_list_to_pbk_mem->bbk_list_params.bbk_indices[i] = i;

	ret = rf_send_swcmd(rfdev, sw_cmd, sizeof(*write_list_to_pbk_mem));

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rfic_write_list_to_pbk_memory_rx(struct rfdev *rfdev, u8 pbk_length)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_write_list_to_pbk_mem *write_list_to_pbk_mem;
	uint8_t i;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_WRITE_LIST_TO_PBK_MEM_RX;
	write_list_to_pbk_mem = (struct rf_swcmd_write_list_to_pbk_mem *)
					&sw_cmd->data[0];
	write_list_to_pbk_mem->bbk_list_params.length = pbk_length;

	for (i = 0; i < pbk_length; i++)
		write_list_to_pbk_mem->bbk_list_params.bbk_indices[i] = i;

	ret = rf_send_swcmd(rfdev, sw_cmd, sizeof(*write_list_to_pbk_mem));

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

int __rfic_set_beam_config(struct rfdev *rfdev, u8 beam)
{
	struct rf_sw_cmd_desc *sw_cmd;
	struct rf_swcmd_set_config_beam *cfg_beam_desc;
	int ret;

	sw_cmd = rf_get_swcmd(rfdev);
	sw_cmd->cmd = RF_SW_CMD_SET_CONFIG_BEAM;
	cfg_beam_desc = (struct rf_swcmd_set_config_beam *)&sw_cmd->data[0];
	cfg_beam_desc->config_beam = beam;

	ret = rf_send_swcmd(rfdev, sw_cmd, sizeof(*cfg_beam_desc));

	rf_free_cmd(rfdev, sw_cmd);

	return ret;
}

void rf_set_host_ready(struct rfdev *rfdev)
{
	struct gul_dev *gul_dev = rfdev->gul_dev;

	dma_wmb();
	SET_HIF_HOST_RDY(gul_dev->hif, HIF_HOST_READY_RFIC);
	dma_wmb();
}

static long gul_rfic_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	struct rif_reg_buf reg_buf;
	struct rif_write_reg_buf write_reg_buf;
	int ret = 0;
	size_t size;
	u32 *buf = NULL;
	struct rfdev *rfdev = (struct rfdev *)filp->private_data;
	struct gul_dev *gul_dev = rfdev->gul_dev;

	switch (cmd) {
	case RIF_READ_PHY_REGS:
		if (!copy_from_user(&reg_buf, (struct rif_reg_buf *)arg,
				    sizeof(struct rif_reg_buf))) {
			if (reg_buf.count > MAX_RW_REGS) {
				ret = -ENOMEM;
				goto out;
			}
			size = 4 * reg_buf.count;
			buf = kzalloc(size, GFP_KERNEL);
			if (!buf) {
				ret = -ENOMEM;
				goto out;
			}
			ret = rf_read_regs(rfdev, &reg_buf, buf);
			if (ret)
				goto out;
			if (copy_to_user((u32 *)reg_buf.buf,
					 buf, size))
				ret = -EFAULT;
		} else {
			ret = -EFAULT;
		}
		break;

	case RIF_WRITE_PHY_REGS:
		if (!copy_from_user(&write_reg_buf,
				    (struct rif_write_reg_buf *)arg,
				    sizeof(struct rif_write_reg_buf))) {
			ret = rf_write_regs(rfdev, &write_reg_buf);
		} else {
			ret = -EFAULT;
		}
		break;
	case RIF_START:
		__rf_set_target_broadcast_rx_fe(rfdev);
		__rf_tx_rx_seq_cmd_init(rfdev);
		__rf_enable_agc(rfdev);
		__rf_switch_rx(rfdev);
		rf_set_host_ready(rfdev);
		break;
	case RIF_STOP:
		dev_info(gul_dev->dev, "RF stop is not implemented\n");
		break;
	default:
		ret = -ENOTTY;
	}

out:
	kfree(buf);

	return ret;
}
#endif /* RF_FR2_DRVR_ENABLED */

static const struct file_operations gul_rfic_fops = {
	.owner = THIS_MODULE,
	.open =	gul_rfic_open,
	.release = gul_rfic_release,
#ifdef RF_FR2_DRVR_ENABLED
	.unlocked_ioctl = gul_rfic_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gul_rfic_ioctl,
#endif
#endif /* RF_FR2_DRVR_ENABLED */
};

static ssize_t rfic_show_stats(void *stats_args, char *buf, void *devp)
{

	struct rfdev *rfdev = (struct rfdev *) stats_args;
	struct rf_ep_stats *ep_stats;
	struct rf_host_stats *host_stats;
	struct gul_dev *gul_dev;
	int len = 0;
	u32 i, j;

	ep_stats = &rfdev->r_hif->stats.ep_stats;
	host_stats = &rfdev->host_stats;
	gul_dev = rfdev->gul_dev;

	if ((gul_dev->stats_desc.stats_control &
	     (1 << HOST_CONTROL_RFIC_STATS)) == 0) {
		dev_err(gul_dev->dev,
			"HOST_CONTROL_RF_STATS not set - stats_conrol 0x%X\n",
			gul_dev->stats_desc.stats_control);
		return 0;
	}

	len += sprintf((buf + len), "\nRFIC: ##### HOST stats  ######\n");

	len += sprintf((buf + len), "sw_cmds_tx = %u\n",
		       host_stats->sw_cmds_tx);
	len += sprintf((buf + len), "sw_cmds_failed = %u\n",
		       host_stats->sw_cmds_failed);
	len += sprintf((buf + len), "sw_cmds_timed_out = %u\n",
		       host_stats->sw_cmds_timed_out);
	len += sprintf((buf + len), "sw_cmds_desc_busy = %u\n",
		       host_stats->sw_cmds_desc_busy);

	len += sprintf((buf + len), "\nRFIC: ##### Modem stats  ######\n");
	for (i = 0; i < E200_CORE_COUNT; i++)
		len += sprintf((buf + len), "init state = %u, %u\n",
			       i, ioread32be(&(ep_stats->init_state[i])));
	len += sprintf((buf + len), "core task state = %u\n",
		       ioread32be(&(ep_stats->core_task_state)));
	for (i = 0; i < RF_TOTAL_USERS; i++) {
		if (ep_stats->remote_isr_hit_count[i]) {
			len += sprintf((buf + len),
				"remote isr hit count = %u, %u\n",
				i,
				ioread32be(
					&(ep_stats->remote_isr_hit_count[i])));
		}
	}
	for (i = 0; i < RF_TOTAL_USERS; i++) {
		if (ep_stats->remote_isr_event_send_failed[i]) {
			len += sprintf((buf + len),
			"remote isr event send failed = %u, %u\n",
			i,
			ioread32be(
				&(ep_stats->remote_isr_event_send_failed[i])));
		}
	}
	for (i = 0; i < RF_TOTAL_USERS; i++) {
		if (ep_stats->remote_isr_enqueue_failed[i]) {
			len += sprintf((buf + len),
			"remote isr enqueue failed = %u, %u\n",
			i,
			ioread32be(
				&(ep_stats->remote_isr_enqueue_failed[i])));
		}
	}
	for (i = 0; i < RF_TOTAL_USERS; i++) {
		for (j = 0; j < RF_SW_CMD_MAX_COUNT; j++) {
			if (ep_stats->remote_cmd_timeout[i][j]) {
				len += sprintf((buf + len),
				"remote cmd timeout = %u, %u, %u\n",
				i,
				j,
				ioread32be(
					&(ep_stats->remote_cmd_timeout[i][j])));
			}
		}
	}
	len += sprintf((buf + len), "core task local cmd count = %u\n",
		       ioread32be(&(ep_stats->core_task_local_cmd_count)));
	len += sprintf((buf + len), "core task remote cmd count = %u\n",
		       ioread32be(&(ep_stats->core_task_remote_cmd_count)));
	len += sprintf((buf + len), "rfic test status = %u\n",
		       ioread32be(&(ep_stats->rfic_test)));
	if (ep_stats->local_invalid_cmd_count)
		len += sprintf((buf + len), "local invalid commands = %u\n",
			ioread32be(&(ep_stats->local_invalid_cmd_count)));
	if (ep_stats->local_desc_errors)
		len += sprintf((buf + len), "local desc errors = %u\n",
			       ioread32be(&(ep_stats->local_desc_errors)));
	if (ep_stats->remote_desc_errors)
		len += sprintf((buf + len), "remote desc errors = %u\n",
			       ioread32be(&(ep_stats->remote_desc_errors)));
	for (i = 0; i < RF_TOTAL_USERS; i++) {
		if (ep_stats->remote_invalid_cmd_count[i]) {
			len += sprintf((buf + len),
			"remote invalid commands = %u, %u\n",
			i,
			ioread32be(&(ep_stats->remote_invalid_cmd_count[i])));
		}
	}
	if (ep_stats->core_task_local_cmd_errors)
		len += sprintf((buf + len),
			"core task local cmd errors = %u\n",
			ioread32be(&(ep_stats->core_task_local_cmd_errors)));
	if (ep_stats->core_task_remote_cmd_error)
		len += sprintf((buf + len),
			"core task remote cmd errors = %u\n",
			ioread32be(&(ep_stats->core_task_remote_cmd_error)));
	if (ep_stats->local_queue_recv_failed)
		len += sprintf((buf + len),
			"local queue recv failed = %u\n",
			ioread32be(&(ep_stats->local_queue_recv_failed)));
	for (i = 0; i < RF_TOTAL_USERS; i++) {
		if (ep_stats->remote_queue_recv_failed[i]) {
			len += sprintf((buf + len),
			"remote queue recv failed = %u, %u\n",
			i,
			ioread32be(&(ep_stats->remote_queue_recv_failed[i])));
		}
	}
	if (ep_stats->local_resp_send_failed)
		len += sprintf((buf + len), "local resp send failed = %u\n",
			       ioread32be(
				&(ep_stats->local_resp_send_failed)));
	if (ep_stats->local_sw_cmd_timeout_count)
		len += sprintf((buf + len), "local sw cmd timeout = %u\n",
			       ioread32be(
				&(ep_stats->local_sw_cmd_timeout_count)));
	if (ep_stats->local_queue_send_failed)
		len += sprintf((buf + len), "local queue send failed = %u\n",
			       ioread32be(
				&(ep_stats->local_queue_send_failed)));
	if (ep_stats->local_event_send_failed)
		len += sprintf((buf + len), "local event send failed = %u\n",
			       ioread32be(
					&(ep_stats->local_event_send_failed)));
	if (ep_stats->remote_event_failure)
		len += sprintf((buf + len), "remote event failed = %u\n",
			       ioread32be(&(ep_stats->remote_event_failure)));
	for (i = 0; i < RF_TOTAL_USERS; i++) {
		len += sprintf((buf + len),
			       "desc result = %u, %u\n",
				i,
				ioread32be(&(ep_stats->desc_latest_result[i])));
	}
	if (ep_stats->sqt_pending_cmd) {
		len += sprintf((buf + len),
			       "sqt_pending_cmd = %u\n",
				ioread32be(&(ep_stats->sqt_pending_cmd)));
		len += sprintf((buf + len),
			       "sqt_pending_cmd_trx_type = %u\n",
				ioread32be(&(ep_stats->sqt_pending_cmd_trx_type)));
		len += sprintf((buf + len),
			       "sqt_pending_cmd_pending_trx_count = %u\n",
				ioread32be(&(ep_stats->sqt_pending_cmd_pending_trx_count)));
		len += sprintf((buf + len),
			       "sqt_pending_cmd_inserted_fpga = %u\n",
				ioread32be(&(ep_stats->sqt_pending_cmd_inserted_fpga)));
	}

	for (j = 0; j < RF_SW_CMD_MAX_COUNT; j++) {
		if (ep_stats->sqt_rejected_cmd[j]) {
			len += sprintf((buf + len),
			"sqt_rejected_cmd = %u, %u\n",
			j,
			ioread32be(&(ep_stats->sqt_rejected_cmd[j])));
		}
		if (ep_stats->sqt_completed_cmd[j]) {
			len += sprintf((buf + len),
			"sqt_completed_cmd = %u, %u\n",
			j,
			ioread32be(&(ep_stats->sqt_completed_cmd[j])));
		}
		if (ep_stats->sqt_error_cmd[j]) {
			len += sprintf((buf + len),
			"sqt_error_cmd = %u, %u\n",
			j,
			ioread32be(&(ep_stats->sqt_error_cmd[j])));
		}
	}

	for (i = 0; i < RF_TOTAL_USERS; i++) {
		for (j = 0; j < RF_SW_CMD_MAX_COUNT; j++) {
			if (ep_stats->cmd_counts[i][j]) {
				len += sprintf((buf + len),
				"cmd counts = %u, %u, %u\n",
				i,
				j,
				ioread32be(&(ep_stats->cmd_counts[i][j])));
			}
			if (ep_stats->cmd_complete_counts[i][j]) {
				len += sprintf((buf + len),
				"cmd complete counts = %u, %u, %u\n",
				i,
				j,
				ioread32be(
				&(ep_stats->cmd_complete_counts[i][j])));
			}
		}
	}

	for (i = 0; i < RF_TOTAL_USERS; i++) {
		for (j = 0; j < RF_SW_CMD_MAX_COUNT; j++) {
			if (ep_stats->cmd_latencies[i][j]) {
				len += sprintf((buf + len),
				"cmd latency = %u, %u, %u\n",
				i,
				j,
				ioread32be(
					&(ep_stats->cmd_latencies[i][j])));
			}
			if (ep_stats->max_cmd_latencies[i][j]) {
				len += sprintf((buf + len),
				"max cmd latency = %u, %u, %u\n",
				i,
				j,
				ioread32be(
					&(ep_stats->max_cmd_latencies[i][j])));
			}
		}
	}

	len += sprintf((buf + len),
					"vcxo_index: %u\n",
					ioread32be(&(ep_stats->vcxo_index)));
	len += sprintf((buf + len),
					"rx_gain_lut_index: %u\n",
					ioread32be(&(ep_stats->rx_gain_lut_index)));
	len += sprintf((buf + len),
					"rx_gain_beam_mask: %u\n",
					ioread32be(&(ep_stats->rx_gain_beam_mask)));
	len += sprintf((buf + len),
					"bbk_index_rx: %u\n",
					ioread32be(&(ep_stats->bbk_index_rx)));
	len += sprintf((buf + len),
					"bbk_index_tx: %u\n",
					ioread32be(&(ep_stats->bbk_index_tx)));
	len += sprintf((buf + len),
					"tx_gain_lut_index: %u\n",
					ioread32be(&(ep_stats->tx_gain_lut_index)));
#if 0
	len += sprintf((buf + len), "<stat name, copy from struct> = %u\n",
		       ep_stats->stat_name);
#endif

	return len;
}

static void rfic_reset_stats(void *stats_args)
{
	struct rfdev *rfdev = (struct rfdev *) stats_args;
	struct rf_ep_stats *ep_stats;
	struct rf_host_stats *host_stats;

	ep_stats = &rfdev->r_hif->stats.ep_stats;
	host_stats = &rfdev->host_stats;
	dev_info(rfdev->gul_dev->dev, "Resetting RF stats\n");
	memset_io(ep_stats, 0, sizeof(*ep_stats));
	memset_io(host_stats, 0, sizeof(*host_stats));
}
static int gul_rfic_stats_init(struct gul_dev *gul_dev)
{
	struct gul_stats_ops rfic_stats_ops;

	rfic_stats_ops.gul_show_stats = rfic_show_stats;
	rfic_stats_ops.gul_reset_stats = rfic_reset_stats;
	rfic_stats_ops.stats_args = (void *)gul_dev->rfdev;

	return gul_host_add_stats(gul_dev, &rfic_stats_ops);
}

static int gul_rfic_create_cdev(struct rfdev *rfdev)
{
	int ret = 0;

	cdev_init(&rfdev->cdev, &gul_rfic_fops);
	rfdev->cdev.owner = THIS_MODULE;
	rfdev->cdev.ops = &gul_rfic_fops;
	ret = cdev_add(&rfdev->cdev, rfdev->rfdevnr, 1);
	if (ret)
		printk(KERN_CRIT "Error %d adding %s\n", ret, rfdev->name);

	return ret;
}

static int gul_rfic_init_mdata(struct rfdev *rfdev)
{
	struct gul_dev *gul_dev = rfdev->gul_dev;
	struct gul_hif *hif;
	struct rf_host_if *r_hif;
	struct gul_mem_region_info *pebm;
	int ret = 0, size;

	hif = gul_dev->hif;

	pebm = &gul_dev->mem_regions[GUL_MEM_REGION_PEBM];
#if 0
	u32 r_hif_offset, r_hif_size;

	r_hif_offset = ioread32be(&hif->rfic_regs.mdata_offset_reg);
	r_hif_size = ioread32be(&hif->rfic_regs.mdata_offset_reg);
	dev_info(gul_dev->dev, "r_hif_off 0x%x, r_hif_siz 0x%x\n",
		 r_hif_offset, r_hif_size);
	r_hif = (struct rf_host_if *)((u64) pebm->vaddr + (u32) r_hif_offset);
	/*XXX:TBD - Modem RF driver currently is not using rfic_regs, instead
	 *of that rf_hif is part of hif. Pick pointer from HIF directly till
	 *Modem RF driver starts using rfic_regs
	 */
#else
	r_hif = &hif->rf_hif;
#endif
	dev_dbg(gul_dev->dev, "pebm [v]0x%px, [p]0x%llx\n", pebm->vaddr,
		 pebm->phys_addr);
	rfdev->r_hif = r_hif;
	size = sizeof(rf_sw_cmd_desc_t);
	dev_dbg(gul_dev->dev, "r_hif  0x%px\n", r_hif);
	dev_dbg(gul_dev->dev, "swcmd size %d\n", size);
	dev_dbg(gul_dev->dev, "swcmd data size %d\n",
		 (int) sizeof(r_hif->rf_mdata.host_swcmd.data));
	size = size - sizeof(r_hif->rf_mdata.host_swcmd.data);
	dev_dbg(gul_dev->dev, "swcmd common size %d\n", size);
	rfdev->swcmd_common_size = size;
	dev_info(gul_dev->dev, "RF Card Type: %d\n",
		ioread32be(&r_hif->rf_mdata.rfic_type));

	sema_init(&rfdev->mdata_lock, 1);
	return ret;
}

int gul_rfic_probe(struct gul_dev *gul_dev, int virq_count,
		   struct virq_evt_map *virq_map)
{
	int ret = 0, retries  = GUL_RF_INIT_WAIT_RETRIES;
	uint32_t i;
	struct gul_hif *hif;
	struct rfdev *rfdev;
	struct device *rfic_class_dev = NULL;

	dev_dbg(gul_dev->dev, "Inside %s function\n", __func__);

	for (i = 0; i < MAX_MODEM; i++) {
		if (in_use_minor[i] == 0) {
			rfic_minor_index = i;
			break;
		}
	}

	if (i == MAX_MODEM) {
		printk(KERN_ERR "No minor no. free to create rfic dev\n");
		return -ENODEV;
	}

	rfdev = kzalloc(sizeof(struct rfdev), GFP_KERNEL);
	if (!rfdev) {
		printk(KERN_CRIT "Memory allocation failure for rfdev\n");
		return -ENOMEM;
	}

	rfdev->rfdevnr = MKDEV(gul_rfic_major, rfic_minor_index);
	sprintf(rfdev->name, "%s%d", "rfdev", rfic_minor_index);

	rfic_class_dev = device_create(gul_dev->class, NULL,
				       rfdev->rfdevnr,
				       NULL, rfdev->name);
	if (IS_ERR(rfic_class_dev))
		goto fail;

	rfdev->gul_dev = gul_dev;

	/* Wait for Modem to ready RFIC metadata */
	hif = gul_dev->hif;
	while (!CHK_HIF_MOD_RDY(hif, HIF_MOD_READY_RFIC) && retries) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(
						  GUL_RF_INIT_WAIT_TIMEOUT));
		retries--;
		dma_rmb();
	}

	/*XXX:TBD - uncomment ready check after modem supports ready*/
#if 0
	if (!CHK_HIF_MOD_RDY(hif, HIF_MOD_READY_RFIC)) {
		dev_err(gul_dev->dev, "modem RFIC drvr ready not set,Abort\n");
		goto out;
	}
#endif
	dev_info(gul_dev->dev, "modem RFIC driver is ready!\n");
	ret = gul_rfic_init_mdata(rfdev);
	if (ret)
		goto fail;

	ret = gul_rfic_create_cdev(rfdev);
	if (ret)
		goto fail;

	gul_dev->rfdev = rfdev;

	in_use_minor[rfic_minor_index] = 1;
	rfdev->minor = rfic_minor_index;

	/* Register Stats ops to SYSFS interface */
	ret = gul_rfic_stats_init(gul_dev);
	if (ret < 0) {
		dev_err(gul_dev->dev, "RFIC Stats registration failed!\n");
		goto fail;
	}
	gul_dev->stats_desc.stats_control |= (1 << HOST_CONTROL_RFIC_STATS);

	dev_dbg(gul_dev->dev, "Exiting function %s\n", __func__);
	return ret;

fail:
	if (rfdev) {
		if (rfic_class_dev) {
			if (IS_ERR(rfic_class_dev))
				ret = PTR_ERR(rfic_class_dev);
			else
				device_destroy(gul_dev->class,
					       rfdev->rfdevnr);
		}

		kfree(rfdev);
	}


	return ret;
}

int gul_rfic_remove(struct gul_dev *gul_dev)
{
	//int i;
	struct rfdev *rfdev;

	rfdev = gul_dev->rfdev;
	if (rfdev) {
		/*XXX:TBD: RFIC IRQs not yet implemented*/
#if 0
		for (i = 0; i < NUM_RF_CHANNELS; i++) {
			free_irq(rfdev->ch_irq[i],
				 &(rfdev->rx_notification[i]));
		}
#endif

		cdev_del(&rfdev->cdev);
		device_destroy(gul_dev->class, rfdev->rfdevnr);
		in_use_minor[rfdev->minor] = 0;
		kfree(rfdev);
		gul_dev->rfdev = NULL;
		gul_dev->rfic_priv = NULL;
	}

	return 0;
}

int gul_rfic_init(void)
{
	int ret;

	rfdevnr = 0;
	rfic_minor_index = GUL_MINOR_START;
	gul_rfic_minor = GUL_MINOR_START;
	gul_nr_dev = MAX_MODEM;

	ret = alloc_chrdev_region(&rfdevnr, gul_rfic_minor, gul_nr_dev,
				  "rf");
	if (ret < 0) {
		printk(KERN_CRIT "gul_rfic: Failed in getting major number\n");
		return ret;
	}

	gul_rfic_major = MAJOR(rfdevnr);

	printk(KERN_INFO "LA12xx RFIC driver: major_nr %d, minor %d\n",
	       gul_rfic_major, gul_rfic_minor);

	return ret;
}
EXPORT_SYMBOL_GPL(gul_rfic_init);

int gul_rfic_exit(void)
{
	unregister_chrdev_region(rfdevnr, gul_nr_dev);
	return 0;
}
EXPORT_SYMBOL_GPL(gul_rfic_exit);
