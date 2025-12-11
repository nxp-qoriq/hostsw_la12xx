/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2021-2022 NXP
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
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/timer.h>

#include "gul_base.h"
#include "gul_usim.h"

/* Globals */
int g_poll_timer_event;

static dev_t usim_dev_num;
static uint32_t usim_dev_major;
static uint32_t usim_dev_minor;
static struct class *usim_dev_class;
static struct usim_device_data *usim_dev_data_g[MAX_USIM_DEV];

/* USIM char Dev data holder */
struct usim_device_data {
	struct usim_dev *usim_dev;
	struct cdev cdev;
};

/* Function Declaration */
static void sim_power_off(struct usim_dev *);
static void sim_data_reset(struct usim_dev *);
static void sim_stop(struct usim_dev *);

/* Function: sim_atr_add
 *
 * Description: add a byte to the raw ATR string
 *
 * Parameters:
 * struct usim_dev *usim_dev    pointer to SIM device handler
 * uint8_t data            byte to be added
 */

static void sim_atr_add(struct usim_dev *usim_dev, uint32_t data)
{
	struct sim_t *sim = usim_dev->sim;

	pr_debug("%s entering.\n", __func__);

	if (sim->atr.size < SIM_ATR_LENGTH_MAX)
		sim->atr.t[sim->atr.size++] = (uint8_t)data;
	else
		pr_info("sim.c: ATR received is too big!\n");
};

/* Function: wait_for_tc
 *
 * Description: waiting for transmission of data complete
 *
 * Parameters:
 * struct usim_dev *usim_dev    pointer to SIM device handler
 */

void wait_for_tc(struct usim_dev *usim_dev)
{
	struct usim_regs_map *regs = usim_dev->usim_regs;

	/* polling for tc, etc and tfe to set */
	bit_set_polling(&regs->usim_xmt_status, 0x38, 0);
	/* clearing the above set flags */
	writel(0x38, &regs->usim_xmt_status);
}

/* Function: wait_for_rc
 *
 * Description: waiting for reception of data complete
 *
 * Parameters:
 * struct usim_dev *usim_dev    pointer to SIM device handler
 */

void wait_for_rc(struct usim_dev *usim_dev)
{
	struct usim_regs_map *regs = usim_dev->usim_regs;

	/* polling till rfe is set */
	bit_set_polling(&regs->usim_rcv_status, 0x10, 0);
	/* clearing rfe fla */
	writel(0x2, &regs->usim_rcv_status);
	/* polling till rfe and rdrf is set */
	bit_set_polling(&regs->usim_rcv_status, 0x30, 0);
}


/* Function: sim_init
 *
 * Description: waiting for reception of data complete
 *
 * Parameters:
 * struct usim_dev *usim_dev    pointer to SIM device handler
 */

void sim_init(struct usim_dev *usim_dev)
{
	struct usim_regs_map *regs = usim_dev->usim_regs;
	struct sim_t *sim = usim_dev->sim;
	int clk_pre = sim->clk_prescaler;
	int i = 0, j = 0;
	uint32_t value;
	uint32_t atr_data;
	uint32_t atr_length = 0;
	uint32_t atr[30];

	usim_init_clk(usim_dev);
	usim_card_detect(usim_dev);

	/* SIM CARD ACTIVATION */
	writel(0x40, &regs->usim_port0_cntl);       //3VOLT0 = 1
	writel(0x42, &regs->usim_port0_cntl);       //SVEN = 1
	writel(0x1, &regs->usim_od_config);         //OD_P0 = 1
	writel(clk_pre, &regs->usim_clk_prescaler);    //CLK_PRESCALER = 0XE
	writel(0x46, &regs->usim_port0_cntl);       //STEN0 = 1
	writel(0xfe, &regs->usim_guard_cntl);       //GUARD_CNTL = 0XFE
	writel(0x16, &regs->usim_cntl);             //ANACK=1,ICM =1,SAMPLE12=1
	writel(0x3, &regs->usim_xmt_threshold);     //XMT_THRESHOLD =3
	writel(0xf23, &regs->usim_rcv_status);      //CLEARING RCV_STATUS
	writel(0x56, &regs->usim_port0_cntl);       //SCEN0 = 1


	/* STARTING COUNTER FOR 400 CYCLES DELAY BETWEEN SCEN AND SRST */
	delay_clk(0x190, usim_dev);

	/* WAITING TILL GPCNT = 1 , MEANS COUNTER REACHED THE VALUE */
	bit_set_polling(&regs->usim_xmt_status, 0x100, 0);
	clear_gpcnt_counter(usim_dev);
	/* REMOVING RESET */
	writel(0x5e, &regs->usim_port0_cntl);
	/* starting gpcnt counter to count for 40,000 cycles */
	delay_clk(0x9c40, usim_dev);

	/* CHAR WAIT COUNTER VALUE SETTING AND ENABLING */
	/* UNMASKING CWT INTERRUPT */
	writel(((readl(&regs->usim_int_mask)) & 0x3dff), &regs->usim_int_mask);
	/* SET THE VALUE TO 0x2580 = 0d9600 */
	writel(0x2580, &regs->usim_char_wait);
	//writel(0x3C00, &regs->usim_char_wait);
	/* ENABLING CWT COUNTE */
	writel(((readl(&regs->usim_cntl)) | 0x800), &regs->usim_cntl);
	/* ENABLING RECEIVER TO RECEIVE ATR */
	writel(0x01, &regs->usim_enable);

	/* ATR Test */

	value = readl(&regs->usim_xmt_status);
	if ((value & 0x100) != 0x100)
		pr_debug("GPCNT flag not set - %s\n", __func__);
	else
		pr_debug("GPCNT flag set - %s\n", __func__);


	bit_set_polling(&regs->usim_rcv_status, 0x10, 0);

	/*reading ATR */
	sim->atr.size = 0;
	value = readl(&regs->usim_rcv_status);
	while ((value & 0x10) == 0x10) {
		i = 0;
		atr_length++;
		atr_data = readl(&regs->usim_rcv_buff);
		atr[j++] = atr_data;
		pr_debug(" %x", atr_data);
		sim_atr_add(usim_dev, atr_data);

		value = readl(&regs->usim_rcv_status);
		while ((value & 0x10) != 0x10) {
			if (i == 1000000)
				break;
			value = readl(&regs->usim_rcv_status);
			i++;
		}
		if (i == 1000000)
			break;
	}

}

/* Function: usim_xfer
 *
 * Description: Transfer function to send/rcv Command/Response
 *
 * Parameters:
 * struct usim_dev *usim_dev    pointer to SIM device handler
 * uint8_t *cmd_buff		pointer to command buffer
 * uint32_t cmd_len		length of command
 * uint8_t *rsp_buff		pointer to response buffer
 * uint32_t rsp_len		length of response
 *
 */

int usim_xfer(struct usim_dev *usim_dev, uint8_t *cmd_buff,
			uint32_t cmd_len, uint8_t *rsp_buff, uint32_t rcv_len)
{
	uint8_t *rcv = rsp_buff;
	struct usim_regs_map *regs = usim_dev->usim_regs;
	uint32_t i = 0, j = 0, k = 0;
	uint32_t value;
	uint32_t select_cmd = cmd_len;

	pr_debug("%s cmd_len=0x%x, rcv_len=0x%x\n", __func__, cmd_len, rcv_len);
	for (i = 0 ; i < cmd_len ; i++)
		pr_debug("%s cmd_buff[%d]=0x%x\n", __func__, i, cmd_buff[i]);

	writel(0x3, &regs->usim_enable);		// Rx and Tx Enable
	writel(0x3, &regs->usim_reset_cntl);		// flush xmt and rcv
	writel(0x0, &regs->usim_reset_cntl);		// flush xmt and rcv
	writel(0x1b9, &regs->usim_xmt_status);		// clear xmt_status
	writel(readl(&regs->usim_rcv_threshold) | (rcv_len & 0x1FF),
			&regs->usim_rcv_threshold);	// setting rcv_len

	/* For select command only send command
	   without File ID or AID  */
	if (cmd_buff[1] == 0xA4)
		select_cmd = 5;

	for (i = 0 ; i < select_cmd ; i++) {
		writel(cmd_buff[i], &regs->usim_xmt_buff);
		wait_for_tc(usim_dev);
	}

	pr_debug("%s Command sent.....\n", __func__);

	writel(0xf23, &regs->usim_rcv_status);		// clear rcv_status

	/* waiting till rfe and rdrf to set */
	bit_set_polling(&regs->usim_rcv_status, 0x10, 0);

	pr_debug("%s Data rcvd from SIM card.... ::\n", __func__);

	value = readl(&regs->usim_rcv_status);
	i = 0;
	j = 0;

	/* reading rcv_buf till rfe is set */
	while ((value & 0x10) == 0x10) {
		rcv[i] = readl(&regs->usim_rcv_buff);
		pr_debug(" %x", rcv[i]);

		/* Once ACK is rcvd for Select command
		   send File ID or AID */
		if (rcv[i] == 0xA4) {
			for (k = 5 ; k < cmd_len ; k++) {
				writel(cmd_buff[k], &regs->usim_xmt_buff);
				wait_for_tc(usim_dev);
			}
		}

		if (i == (rcv_len-1))
			break;

		value = readl(&regs->usim_rcv_status);
		i++;

		while ((value & 0x10) != 0x10) {
			if (j == 1000000)
				break;
			value = readl(&regs->usim_rcv_status);
			j++;
		}
		if (j == 1000000)
			break;
	}

	if (i == 0)
		return SIM_E_NOCARD;

	if (i < (rcv_len-1))
		return SIM_E_NOCARD;

	if ((rcv[i-2] == 0x90) && (rcv[i-1] == 0x00)) {
		pr_debug("SW1 SW2 Status -- complete");
		return SIM_OK;
	} else if ((rcv[i-2] == 0x9f) || (rcv[i-2] == 0x61)) {
		pr_debug("SW1 SW2 Status -- opeartion finished");
		return SIM_OK;
	}

	pr_debug("SW1 SW2 Status -- error occurred");
	return -SIM_E_ACCESS;
}

/* Function: sim_power_on
 *
 * Description: run the power on sequence
 *
 * Parameters:
 * struct usim_dev *usim_dev              pointer to SIM device handler
 */

static void sim_power_on(struct usim_dev *usim_dev)
{
	struct usim_regs_map *regs = usim_dev->usim_regs;
	struct gul_dev *gul_dev = usim_dev->gul_dev;
	uint32_t reg_data;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s\n", __func__);

	/* power on sequence */
	reg_data = readl(&regs->usim_port0_cntl);
	reg_data |= SIM_PORT_CNTL_SVEN;
	writel(reg_data, &regs->usim_port0_cntl);
	msleep(10);
	reg_data = readl(&regs->usim_port0_cntl);
	reg_data |= SIM_PORT_CNTL_SCEN;
	writel(reg_data, &regs->usim_port0_cntl);
	msleep(10);
	reg_data = SIM_RCV_THRESHOLD_RTH(0) | SIM_RCV_THRESHOLD_RDT(1);
	writel(reg_data, &regs->usim_rcv_threshold);
	writel(SIM_RCV_STATUS_RDRF, &regs->usim_rcv_status);
	reg_data = readl(&regs->usim_int_mask);
	reg_data &= ~(SIM_INT_MASK_RIM | SIM_INT_MASK_GPCM);
	writel(reg_data, &regs->usim_int_mask);
	writel(31, &regs->usim_divisor);
	reg_data = readl(&regs->usim_cntl);
	reg_data |= SIM_CNTL_SAMPLE12;
	writel(reg_data, &regs->usim_cntl);
	reg_data = readl(&regs->usim_enable);
	reg_data |= SIM_ENABLE_RCVEN;
	writel(reg_data, &regs->usim_enable);
	reg_data = readl(&regs->usim_port0_cntl);
	reg_data |= SIM_PORT_CNTL_SRST;
	writel(reg_data, &regs->usim_port0_cntl);

	dev_dbg(gul_dev->dev, "%s port0_ctl is 0x%x.\n", __func__,
		 readl(&regs->usim_port0_cntl));

	dev_dbg(gul_dev->dev, "%s cntl is 0x%x.\n", __func__,
		 readl(&regs->usim_cntl));

	usim_dev->sim->power = SIM_POWER_ON;
	usim_dev->sim->sim_pts_pps2_flag = 0;
	usim_dev->sim->sim_pts_pps3_flag = 0;
	usim_dev->sim->sim_pts_valid_flag = 0;

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);
};

/* Function: sim_power_off
 *
 * Description: run the power off sequence
 *
 * Parameters:
 * struct usim_dev *usim_dev              pointer to SIM device handler
 */

static void sim_power_off(struct usim_dev *usim_dev)
{
	struct usim_regs_map *regs = usim_dev->usim_regs;
	struct gul_dev *gul_dev = usim_dev->gul_dev;
	uint32_t reg_data;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s\n", __func__);

	/* sim_power_off sequence */
	reg_data = readl(&regs->usim_port0_cntl);
	reg_data &= ~SIM_PORT_CNTL_SCEN;
	writel(reg_data, &regs->usim_port0_cntl);
	reg_data = readl(&regs->usim_enable);
	reg_data &= ~SIM_ENABLE_RCVEN;
	writel(reg_data, &regs->usim_enable);
	reg_data = readl(&regs->usim_int_mask);
	reg_data |= SIM_INT_MASK_RIM;
	writel(reg_data, &regs->usim_int_mask);
	writel(0, &regs->usim_rcv_threshold);
	reg_data = readl(&regs->usim_port0_cntl);
	reg_data &= ~SIM_PORT_CNTL_SRST;
	writel(reg_data, &regs->usim_port0_cntl);
	reg_data = readl(&regs->usim_port0_cntl);
	reg_data &= ~SIM_PORT_CNTL_SVEN;
	writel(reg_data, &regs->usim_port0_cntl);
	usim_dev->sim->power = SIM_POWER_OFF;

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);
};

/* Function: sim_start
 *
 * Description: ramp up the SIM interface
 *
 * Parameters:
 * struct usim_dev *usim_dev              pointer to SIM device handler
 */

static void sim_start(struct usim_dev *usim_dev)
{
	struct usim_regs_map *regs = usim_dev->usim_regs;
	struct gul_dev *gul_dev = usim_dev->gul_dev;
	uint32_t reg_data;

	memset(&usim_dev->sim->atrparser, 0, sizeof(usim_dev->sim->atrparser));
	memset(&usim_dev->sim->atr, 0, sizeof(usim_dev->sim->atr));

	dev_dbg(gul_dev->dev, "USIM: Entering function %s\n", __func__);

	usim_init_clk(usim_dev);
	/* Configuring SIM for Operation */
	reg_data = SIM_XMT_THRESHOLD_XTH(0) | SIM_XMT_THRESHOLD_TDT(5);
	writel(reg_data, &regs->usim_xmt_threshold);
	writel(0, &regs->usim_setup);

	reg_data = SIM_CNTL_GPCNT_CLK_SEL(0) | SIM_CNTL_BAUD_SEL(7)
	    | SIM_CNTL_SAMPLE12 | SIM_CNTL_ANACK | SIM_CNTL_ICM;
	writel(reg_data, &regs->usim_cntl);
	writel(31, &regs->usim_divisor);
	reg_data = readl(&regs->usim_od_config);
	reg_data |= SIM_OD_CONFIG_OD_P0;
	writel(reg_data, &regs->usim_od_config);
	reg_data = readl(&regs->usim_port0_cntl);
	reg_data |= SIM_PORT_CNTL_3VOLT | SIM_PORT_CNTL_STEN;
	writel(reg_data, &regs->usim_port0_cntl);

	/* presense detect */
	dev_dbg(gul_dev->dev, "USIM: %s p0_det is 0x%x\n", __func__,
		 readl(&regs->usim_port0_detect));

	if (readl(&regs->usim_port0_detect) & SIM_PORT_DETECT_SPDP) {
		dev_dbg(gul_dev->dev, "%s card removed\n", __func__);
		reg_data = readl(&regs->usim_port0_detect);
		reg_data &= ~SIM_PORT_DETECT_SPDS;
		writel(reg_data, &regs->usim_port0_detect);
		usim_dev->sim->present = SIM_PRESENT_REMOVED;
		usim_dev->sim->state = SIM_STATE_REMOVED;
	} else {
		dev_dbg(gul_dev->dev, "USIM: %s card inserted\n", __func__);
		reg_data = readl(&regs->usim_port0_detect);
		reg_data |= SIM_PORT_DETECT_SPDS;
		writel(reg_data, &regs->usim_port0_detect);
		usim_dev->sim->present = SIM_PRESENT_DETECTED;
		usim_dev->sim->state = SIM_STATE_DETECTED_ATR_T0;
	};

	reg_data = readl(&regs->usim_port0_detect);
	reg_data |= SIM_PORT_DETECT_SDI;
	reg_data &= ~SIM_PORT_DETECT_SDIM;
	writel(reg_data, &regs->usim_port0_detect);

	dev_dbg(gul_dev->dev, "USIM: %s After p0_det is 0x%x\n", __func__,
		 readl(&regs->usim_port0_detect));

	if (usim_dev->sim->present == SIM_PRESENT_DETECTED)
		sim_power_on(usim_dev);

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);
};

/* Function: sim_stop
 *
 * Description: shut down the SIM interface
 *
 * Parameters:
 * struct usim_dev *usim_dev             pointer to SIM device handler
 */

static void sim_stop(struct usim_dev *usim_dev)
{
	struct usim_regs_map *regs = usim_dev->usim_regs;
	struct gul_dev *gul_dev = usim_dev->gul_dev;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s\n", __func__);

	writel(0, &regs->usim_setup);
	writel(0, &regs->usim_enable);
	writel(0, &regs->usim_port0_cntl);
	writel(0x06, &regs->usim_cntl);
	writel(0, &regs->usim_clk_prescaler);
	writel(0, &regs->usim_setup);
	writel(0, &regs->usim_od_config);
	writel(0, &regs->usim_xmt_threshold);
	writel(0xb8, &regs->usim_xmt_status);
	writel(4, &regs->usim_reset_cntl);
	mdelay(1);

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);
};

/* Function: sim_data_reset
 *
 * Description: reset a SIM structure to default values
 *
 * Parameters:
 * struct usim_dev *usim_dev              pointer to SIM device handler
 */

static void sim_data_reset(struct usim_dev *usim_dev)
{
	struct gul_dev *gul_dev = usim_dev->gul_dev;
	struct sim_t *sim = usim_dev->sim;
	sim_param_t param_default = SIM_PARAM_DEFAULT;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s\n", __func__);

	sim->present = SIM_PRESENT_REMOVED;
	sim->state = SIM_STATE_REMOVED;
	sim->power = SIM_POWER_OFF;
	sim->errval = SIM_OK;
	memset(&sim->atrparser, 0, sizeof(sim->atrparser));
	memset(&sim->atr, 0, sizeof(sim->atr));
	sim->param_atr = param_default;
	memset(&sim->param, 0, sizeof(sim->param));
	memset(&sim->param_pts, 0, sizeof(sim->param_pts));
	memset(&sim->xfer, 0, sizeof(sim->xfer));
	sim->xfer_ongoing = 0;
	sim->xmt_remaining = 0;
	sim->xmt_pos = 0;
	sim->rcv_count = 0;
	memset(sim->rcv_buffer, 0, SIM_RCV_BUFFER_SIZE);
	memset(sim->xmt_buffer, 0, SIM_XMT_BUFFER_SIZE);
	sim->sim_pts_pps2_flag = 0;
	sim->sim_pts_pps3_flag = 0;
	sim->sim_pts_valid_flag = 0;
	sim->ins_complimentary_flag = 0;
	sim->sim_xmt_last_byte = 0;

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);
};

/* Function: sim_cold_reset
 *
 * Description: cold reset the SIM interface, including card
 * power down and interface hardware reset.
 *
 * Parameters:
 * struct usim_dev *usim_dev              pointer to SIM device handler
 */

static void sim_cold_reset(struct usim_dev *usim_dev)
{
	struct gul_dev *gul_dev = usim_dev->gul_dev;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s\n", __func__);

	if (usim_dev->sim->present != SIM_PRESENT_REMOVED) {
		sim_power_off(usim_dev);
		sim_stop(usim_dev);
		sim_data_reset(usim_dev);
		usim_dev->sim->state = SIM_STATE_DETECTED_ATR_T0;
		usim_dev->sim->present = SIM_PRESENT_DETECTED;
		msleep(50);
		sim_start(usim_dev);
		sim_power_on(usim_dev);
	};

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);
};

/* Function: sim_warm_reset
 *
 * Description: warm reset the SIM interface: just invoke the
 * reset signal and reset the SIM structure for the interface.
 *
 * Parameters:
 * struct usim_dev *usim_dev              pointer to SIM device handler
 */

static void sim_warm_reset(struct usim_dev *usim_dev)
{
	struct usim_regs_map *regs = usim_dev->usim_regs;
	struct gul_dev *gul_dev = usim_dev->gul_dev;
	struct sim_t *sim = usim_dev->sim;
	uint32_t reg_data;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s\n", __func__);

	if (sim->present != SIM_PRESENT_REMOVED) {
		reg_data = readl(&regs->usim_port0_cntl);
		reg_data |= SIM_PORT_CNTL_SRST;
		writel(reg_data, &regs->usim_port0_cntl);
		sim_data_reset(usim_dev);
		msleep(50);
		reg_data = readl(&regs->usim_port0_cntl);
		reg_data &= ~SIM_PORT_CNTL_SRST;
		writel(reg_data, &regs->usim_port0_cntl);
	};

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);
};

/* Function: sim_card_lock
 *
 * Description: physically lock the SIM card.
 *
 * Parameters:
 * struct usim_dev *usim_dev              pointer to SIM device handler
 */

static int sim_card_lock(struct usim_dev *usim_dev)
{
	struct gul_dev *gul_dev = usim_dev->gul_dev;
	struct sim_t *sim = usim_dev->sim;
	int errval;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s state=%x\n",
							__func__, sim->state);

	/* place holder for true physcial locking */
	if (sim->present != SIM_PRESENT_REMOVED)
		errval = SIM_OK;
	else
		errval = -SIM_E_NOCARD;

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);

	return errval;
};

/* Function: sim_card_eject
 *
 * Description: physically unlock and eject the SIM card.
 *
 * Parameters:
 * struct usim_dev *usim_dev              pointer to SIM device handler
 */

static int sim_card_eject(struct usim_dev *usim_dev)
{
	struct gul_dev *gul_dev = usim_dev->gul_dev;
	struct sim_t *sim = usim_dev->sim;
	int errval;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s state=%x\n",
							__func__, sim->state);

	/* place holder for true physcial locking */
	if (sim->present != SIM_PRESENT_REMOVED)
		errval = SIM_OK;
	else
		errval = -SIM_E_NOCARD;

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);

	return errval;
};

/* Function: sim_ioctl
 *
 * Description: handle ioctl calls
 *
 * Parameters: OS specific
 */
static long sim_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	struct usim_device_data *usim_dev_data;
	struct usim_dev *usim_dev;
	struct sim_t *sim;
	int ret, errval = SIM_OK;

	usim_dev_data = (struct usim_device_data *) file->private_data;
	usim_dev = usim_dev_data->usim_dev;
	sim = usim_dev->sim;

	dev_dbg(usim_dev->gul_dev->dev, "USIM: Entering function %s\n",
								__func__);

	switch (cmd) {
	default:
		dev_dbg(usim_dev->gul_dev->dev, "ioctl cmd %d is issued...\n",
									cmd);
		break;

	case SIM_IOCTL_GET_ATR:
		if ((sim->present != SIM_PRESENT_OPERATIONAL) &&
		       (sim->present != SIM_PRESENT_DETECTED)) {
			errval = -SIM_E_NOCARD;
			break;
		};
		ret = copy_to_user((sim_atr_t *) arg, &sim->atr,
				   sizeof(sim_atr_t));
		if (ret)
			errval = -SIM_E_ACCESS;
		break;

	case SIM_IOCTL_POWER_ON:
		if (sim->power == SIM_POWER_ON) {
			errval = -SIM_E_POWERED_ON;
			break;
		};
		sim_power_on(usim_dev);
		break;

	case SIM_IOCTL_POWER_OFF:
		if (sim->power == SIM_POWER_OFF) {
			errval = -SIM_E_POWERED_OFF;
			break;
		};
		sim_power_off(usim_dev);
		break;

	case SIM_IOCTL_COLD_RESET:
		if (sim->power == SIM_POWER_OFF) {
			errval = -SIM_E_POWERED_OFF;
			break;
		};
		sim_cold_reset(usim_dev);
		break;

	case SIM_IOCTL_WARM_RESET:
		sim_warm_reset(usim_dev);
		if (sim->power == SIM_POWER_OFF) {
			errval = -SIM_E_POWERED_OFF;
			break;
		};
		break;

	case SIM_IOCTL_XFER:
		if ((sim->present != SIM_PRESENT_OPERATIONAL) &&
			(sim->present != SIM_PRESENT_DETECTED)) {
			errval = -SIM_E_NOCARD;
			break;
		};

		ret = copy_from_user(&sim->xfer, (sim_xfer_t *) arg,
				     sizeof(sim_xfer_t));
		if (ret) {
			errval = -SIM_E_ACCESS;
			break;
		};

		ret = copy_from_user(sim->xmt_buffer, sim->xfer.xmt_buffer,
				     sim->xfer.xmt_length);
		if (ret) {
			errval = -SIM_E_ACCESS;
			break;
		};

		sim->rcv_count = 0;
		sim->xfer.sw1 = 0;
		sim->xfer.sw2 = 0;

		if (sim->xfer.type == SIM_XFER_TYPE_TPDU) {
			if (sim->xfer.xmt_length < 5) {
				errval = -SIM_E_TPDUSHORT;
				break;
			}
			sim->state = SIM_STATE_OPERATIONAL_COMMAND;
		} else if (sim->xfer.type == SIM_XFER_TYPE_PTS) {
			if (sim->xfer.xmt_length == 0) {
				errval = -SIM_E_PTSEMPTY;
				break;
			}
			sim->state = SIM_STATE_OPERATIONAL_PPSS;
		} else {
			errval = -SIM_E_INVALIDXFERTYPE;
			break;
		};

		if (sim->xfer.xmt_length > SIM_XMT_BUFFER_SIZE) {
			errval = -SIM_E_INVALIDXMTLENGTH;
			break;
		};

		if (sim->xfer.rcv_length > SIM_XMT_BUFFER_SIZE) {
			errval = -SIM_E_INVALIDRCVLENGTH;
			break;
		};

		ret = usim_xfer(usim_dev, sim->xmt_buffer,
				sim->xfer.xmt_length, sim->rcv_buffer,
				sim->xfer.rcv_length);

		if (ret == SIM_E_NOCARD) {
			errval = -SIM_E_NOCARD;
		};

		ret = copy_to_user(sim->xfer.rcv_buffer, sim->rcv_buffer,
				   sim->xfer.rcv_length);
		if (ret) {
			errval = -SIM_E_ACCESS;
			break;
		};

		ret = copy_to_user((sim_xfer_t *) arg, &sim->xfer,
				   sizeof(sim_xfer_t));
		if (ret)
			errval = -SIM_E_ACCESS;
		break;

	case USIM_IOCTL_GET_PHY_ADD:
		ret = copy_to_user((unsigned long *) arg, &usim_dev->usim_phy_add,
				   sizeof(unsigned long));

		if (ret) {
			errval = -SIM_E_ACCESS;
			break;
		};

		break;

	case SIM_IOCTL_CARD_LOCK:
		errval = sim_card_lock(usim_dev);
		break;

	case SIM_IOCTL_CARD_EJECT:
		errval = sim_card_eject(usim_dev);
		break;

	case USIM_IOCTL_STATUS:
		usim_card_detect(usim_dev);

		ret = copy_to_user((int *)arg, &sim->status, sizeof(int));
		if (ret) {
			errval = -SIM_E_ACCESS;
			break;
		};
		break;

	case USIM_IOCTL_INVALIDATE:
		sim->status = USIM_STATE_INVALIDATED;
		break;

	};

	dev_dbg(usim_dev->gul_dev->dev, "USIM: Exiting function %s\n",
								__func__);

	return errval;
};

/* Function: sim_fasync
 *
 * Description: async handler
 *
 * Parameters: OS specific
 */

static int sim_fasync(int fd, struct file *file, int mode)
{
	struct usim_device_data *usim_dev_data;
	struct usim_dev *usim_dev;

	usim_dev_data = (struct usim_device_data *) file->private_data;
	usim_dev = usim_dev_data->usim_dev;

	dev_dbg(usim_dev->gul_dev->dev, "USIM: Entering function %s\n",
								__func__);

	return fasync_helper(fd, file, mode, &usim_dev->sim->fasync);
}

/* Function: sim_open
 *
 * Description: ramp up interface when being opened
 *
 * Parameters: OS specific
 */

static int sim_open(struct inode *inode, struct file *file)
{
	struct usim_device_data *usim_dev_data = NULL;
	int errval = SIM_OK;

	usim_dev_data = container_of(inode->i_cdev,
				struct usim_device_data,
				cdev);

	dev_dbg(usim_dev_data->usim_dev->gul_dev->dev,
				"USIM: Entering function %s\n", __func__);

	file->private_data = usim_dev_data;

	sim_init(usim_dev_data->usim_dev);

	usim_dev_data->usim_dev->sim->status = USIM_STATE_CONNECTED;
	usim_dev_data->usim_dev->sim->present = SIM_PRESENT_OPERATIONAL;

	dev_dbg(usim_dev_data->usim_dev->gul_dev->dev,
				"USIM: Exiting function %s\n", __func__);

	return errval;
};

/* Function: sim_release
 *
 * Description: shut down interface when being closed
 *
 * Parameters: OS specific
 */

static int sim_release(struct inode *inode, struct file *file)
{
	struct usim_device_data *usim_dev_data =
				(struct usim_device_data *) file->private_data;
	struct usim_dev *usim_dev = usim_dev_data->usim_dev;
	struct usim_regs_map *regs = usim_dev->usim_regs;
	uint32_t reg_data;

	dev_dbg(usim_dev->gul_dev->dev, "USIM: Entering function %s\n",
								__func__);

	/* disable presense detection */
	reg_data = readl(&regs->usim_port0_detect);
	writel(reg_data | SIM_PORT_DETECT_SDIM,
		     &regs->usim_port0_detect);

	if (usim_dev->sim->present != SIM_PRESENT_REMOVED) {
		sim_power_off(usim_dev);
		if (usim_dev->sim->fasync)
			kill_fasync(&usim_dev->sim->fasync, SIGIO, POLL_IN);
	};

	sim_stop(usim_dev);

	usim_dev_data->usim_dev->sim->status = USIM_STATE_CONNECTED;

	sim_fasync(-1, file, 0);

	dev_dbg(usim_dev->gul_dev->dev, "USIM: Entering function %s\n",
								__func__);

	return 0;
};

static ssize_t sim_read(struct file *file, char __user *buf,
			size_t count, loff_t *offset)
{
	struct usim_device_data *usim_dev_data;
	struct usim_dev *usim_dev;
	struct sim_t *sim;
	int ret, errval = SIM_OK;

	usim_dev_data = (struct usim_device_data *) file->private_data;
	usim_dev = usim_dev_data->usim_dev;
	sim = usim_dev->sim;

	dev_dbg(usim_dev->gul_dev->dev, "USIM: Entering function %s\n",
						__func__);

	/* wait for sim status to be changed */
	wait_event_interruptible(usim_dev->queue, g_poll_timer_event == 1);
	g_poll_timer_event = 0;

	ret = copy_to_user((int *)buf, &sim->status, count);
	if (ret)
		errval = -SIM_E_ACCESS;

	return errval;
}

/* Function: sim_release
 *
 * Description: shut down interface when being closed
 *
 * Parameters: OS specific
 */

static unsigned int sim_poll(struct file *file, poll_table *wait)
{
	struct usim_device_data *usim_dev_data;
	struct usim_dev *usim_dev;
	struct sim_t *sim;
	unsigned int mask = 0;

	usim_dev_data = (struct usim_device_data *) file->private_data;
	usim_dev = usim_dev_data->usim_dev;
	sim = usim_dev->sim;

	dev_dbg(usim_dev->gul_dev->dev, "USIM: Entering function %s\n",
						__func__);

	/* wait for sim status to be changed */
	wait_event_interruptible(usim_dev->queue, g_poll_timer_event == 1);
	g_poll_timer_event = 0;

	mask |= POLLIN;

	return mask;
}

static const struct file_operations sim_fops = {
	.owner = THIS_MODULE,
	.open = sim_open,
	.read = sim_read,
	.poll = sim_poll,
	.unlocked_ioctl = sim_ioctl,
	.fasync = sim_fasync,
	.release = sim_release
};

/* Function: create_usim_cdev
 *
 * Description: shut down interface when being closed
 *
 * Parameters: OS specific
 */

static int create_usim_cdev(struct usim_device_data **usim_dev_data)
{
	struct usim_dev *usim_dev = usim_dev_data[0]->usim_dev;
	struct gul_dev *gul_dev = usim_dev->gul_dev;
	int ret = 0;

	usim_dev_minor = 0;

	/* sysfs class creation */
	usim_dev_class = class_create(THIS_MODULE, "usimdev");
	if (usim_dev_class == NULL) {
		pr_err("%s:Cannot allocate major number\n", __func__);
		ret = -1;
		goto out_class;
	}

	/*Allocating chardev region and assigning Major number */
	ret = alloc_chrdev_region(&usim_dev_num,
					usim_dev_minor,
					1,
					"usimdev");
	if (ret < 0) {
		pr_err("%s: Failed in getting major number\n",
				 __func__);
		return ret;
	}

	/* Device Major number */
	usim_dev_major = MAJOR(usim_dev_num);

	dev_dbg(gul_dev->dev, "USIM: usim_dev_major=%d, usim_dev_minor=%d\n",
					usim_dev_major, usim_dev_minor);

	cdev_init(&usim_dev_data[0]->cdev, &sim_fops);
	usim_dev_data[0]->cdev.ops = &sim_fops;
	usim_dev_data[0]->cdev.owner = THIS_MODULE;

	/* Adding a device to the system */
	cdev_add(&usim_dev_data[0]->cdev,
			MKDEV(usim_dev_major, usim_dev_minor), 1);
	if ((device_create(usim_dev_class,
					NULL,
					MKDEV(usim_dev_major, usim_dev_minor),
					NULL,
					"usimdev%d", 0)) == NULL) {
		pr_err("%s: Cannot create the usim char device\n", __func__);
		goto out_device;
	}

	return ret;

out_device:
	class_destroy(usim_dev_class);
out_class:
	unregister_chrdev_region(usim_dev_num, 1);
	return ret;
}

/* Function: usim_card_detect
 *
 * Description: Check the card detection status
 *
 * Parameters:
 * struct usim_dev *usim_dev
 */

void usim_card_detect(struct usim_dev *usim_dev)
{
	struct usim_regs_map *regs = usim_dev->usim_regs;
	struct gul_dev *gul_dev = usim_dev->gul_dev;
	uint32_t port_status = 0;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s\n", __func__);

	if (readl(&regs->usim_port0_detect) & SIM_PORT_DETECT_SPDP) {
		dev_dbg(gul_dev->dev, "%s card removed\n", __func__);
		port_status = readl(&regs->usim_port0_detect);
		port_status &= ~SIM_PORT_DETECT_SPDS;
		writel(port_status, &regs->usim_port0_detect);
		usim_dev->sim->present = SIM_PRESENT_REMOVED;
		usim_dev->sim->state = SIM_STATE_REMOVED;
		usim_dev->sim->status = USIM_STATE_DISCONNECTED;
	} else {
		dev_dbg(gul_dev->dev, "USIM: %s card inserted\n", __func__);
		port_status = readl(&regs->usim_port0_detect);
		port_status |= SIM_PORT_DETECT_SPDS;
		writel(port_status, &regs->usim_port0_detect);
		usim_dev->sim->present = SIM_PRESENT_DETECTED;
		usim_dev->sim->state = SIM_STATE_DETECTED_ATR_T0;
		usim_dev->sim->status = USIM_STATE_CONNECTED;
	};

	port_status = readl(&regs->usim_port0_detect);
	port_status |= SIM_PORT_DETECT_SDI;
	port_status &= ~SIM_PORT_DETECT_SDIM;
	writel(port_status, &regs->usim_port0_detect);

	dma_wmb();

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);
	return;
}

/* Function: usim_init_clkInitialize the clock
 *
 * Description: shut down interface when being closed
 *
 * Parameters:
 * struct usim_dev *usim_dev
 */


void usim_init_clk(struct usim_dev *usim_dev)
{
	struct sim_t *sim = usim_dev->sim;
	struct gul_dev *gul_dev = usim_dev->gul_dev;
	int32_t *scfg_config_ctrl1 = NULL;
	uint32_t clk_status = 0;
	int clk_pre = 0;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s\n", __func__);

	/* Set SCFG Registers Base Address */
	scfg_config_ctrl1 = (uint32_t *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			SCFG_CCSR_OFFSET + SCFG_CCSR_CONFIG_CTRL1_OFFSET);
	if (!scfg_config_ctrl1) {
		dev_err(gul_dev->dev, "USIM: SCFG register access failed\n");
		return;
	}

	/* Mid Freq - 0x3e,
	   Low Freq - 0x9a,
	   Very Freq - 0xFF,
	   High Freq = 0x20
	*/
	clk_pre = 0x1F;

	/*  sim_per_clk is divider setting */
	clk_status = readl(scfg_config_ctrl1);

	dev_dbg(gul_dev->dev, "[USIM]: SCFG_CONFIG_CTRL1 value = 0x%x\n",
			clk_status);

	/* 1b - sim_per_clk is divide-by-8 of
	   platform clock = 614.4/8 = 76.75 MHz*/
	writel(clk_status | SCFG_CCSR_CONFIG_CTRL1_SIM_PER_CLK_RAT,
					scfg_config_ctrl1);


	/*  Set cxlk prescaler value */
	sim->clk_prescaler = clk_pre;	/* 76.75/62 = 1.23 MHz */

	dma_wmb();

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);
	return;
}

/* Function: bit_set_polling
 *
 * Description: helper function to poll on a bit of given reg
 *
 * Parameters:
 * uint32_t *addr	Add of reg
 * uint32_t req_value	Value to be polled
 */

void bit_set_polling(uint32_t *addr, uint32_t req_value, int i)
{
	uint32_t value_at_addr = readl(addr);

	while (((value_at_addr) & req_value) != req_value) {

		value_at_addr = readl(addr);
		if (i == 20000000) {
			pr_debug("%sBit Not Set in usim_rcv_status\n",
								__func__);
			break;
		}
		i++;
	}
}

/* Function: delay_clk
 *
 * Description:Providing delay using GPCNT counter
 *
 * Parameters:
 * uint32_t cyclesno of cycles of delay
 * struct usim_dev *usim_dev
 */

void delay_clk(uint32_t cycles, struct usim_dev *usim_dev)
{
	uint32_t cntl_val;
	uint32_t xmt_status_val;
	struct usim_regs_map *regs = usim_dev->usim_regs;

	pr_debug("Providing delay of = %#x", cycles);

	cntl_val = readl(&regs->usim_cntl);

	/* RESET GPCNT COUNTER */
	writel((cntl_val & 0xf9ff), &regs->usim_cntl);
	xmt_status_val = readl(&regs->usim_xmt_status);

	/* CLEAR GPCNT FLAG */
	writel(0x100, &regs->usim_xmt_status);
	/* WRITING DELAY VALUE TO GPCNT */
	writel(cycles, &regs->usim_gpcnt);

	cntl_val = readl(&regs->usim_cntl);
	/* SETTING GPCNT COUNTER TO CARD CLK */
	writel((cntl_val | 0x200), &regs->usim_cntl);

}

/* Function: clear_gpcnt_counter
 *
 * Description:RESETING AND CLEARING GPCNT COUNTER AND FLAG
 *
 * Parameters:
 * struct usim_dev *usim_dev
 */

void clear_gpcnt_counter(struct usim_dev *usim_dev)
{
	struct usim_regs_map *regs = usim_dev->usim_regs;

	/* RESET GPCNT COUNTER */
	writel(((readl(&regs->usim_cntl)) & 0xf9ff), &regs->usim_cntl);
	/* CLEAR GPCNT FLAG */
	writel(0x100, &regs->usim_xmt_status);
}

#ifdef USIM_POLL_TIMER

/* Function: poll_timer
 *
 * Description: Timer Function
 *
 * Parameters:
 */

static void poll_timer(unsigned long data)
{
	struct usim_dev *usim_dev = (void *)data;
	struct sim_t *sim = usim_dev->sim;

	pr_dbg("Inside poll_timer...\n");

	/* check current card status */
	usim_card_detect(usim_dev);

	usim_dev->poll_timer.expires += msecs_to_jiffies(5000);
	g_poll_timer_event = 0;

	/* wake up sim_poll if status of sim has changed */
	if (sim->last_status != sim->status) {
		pr_dbg("Inside poll_timer... status changed\n");
		g_poll_timer_event = 1;
		wake_up_interruptible(&usim_dev->queue);
		sim->last_status = sim->status;
	}
	add_timer(&usim_dev->poll_timer);
}
#endif

/* Function: gul_usim_probe
 *
 * Description: USIM DRiver probe function
 *
 * Parameters:
 */

int gul_usim_probe(struct gul_dev *gul_dev, int virq_count,
		   struct virq_evt_map *virq_map)
{
	struct usim_dev *usim_dev = NULL;
	struct usim_regs_map *usim_regs = NULL;
	phys_addr_t usim_phy_add_map = 0;
	struct sim_t *sim = NULL;
	struct usim_device_data *usim_dev_data[MAX_USIM_DEV];
	int ret = 0, i = 0;

	if (gul_dev->id != 0) {
		dev_dbg(gul_dev->dev, "USIM: %s: Multi-Geul not supported!\n",
				__func__);
		return 0;
	}

	/* Initialize USIM Device */
	usim_dev = kzalloc(sizeof(struct usim_dev), GFP_KERNEL);
	if (!usim_dev) {
		dev_err(gul_dev->dev, "USIM Dev : Memory allocation failed\n");
		ret = -ENOMEM;
		goto out;
	}

	/* Initialize SIM Structure */
	sim = kzalloc(sizeof(struct sim_t), GFP_KERNEL);
	if (!sim) {
		dev_err(gul_dev->dev, "sim str : Memory allocation failed\n");
		kfree(usim_dev);
		ret = -ENOMEM;
		return ret;
	};

	/* Get CCSR Virtual Address */
	usim_regs = (struct usim_regs_map *) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].vaddr +
			USIM_CCSR_OFFSET);
	if (!usim_regs) {
		dev_err(gul_dev->dev, "USIM: CCSR Not Initialized\n");
		kfree(usim_dev);
		kfree(sim);
		ret = -ENOMEM;
		goto out;
	}

	dev_dbg(gul_dev->dev, "%s: ccsr 0x%p, dcs 0x%p, dcs offset 0x%x\n",
		__func__, gul_dev->mem_regions[GUL_MEM_REGION_CCSR].vaddr,
		usim_regs, USIM_CCSR_OFFSET);

	/* Get CCSR Physical */
	usim_phy_add_map = (phys_addr_t) (gul_dev->mem_regions
			[GUL_MEM_REGION_CCSR].phys_addr +
			USIM_CCSR_OFFSET);
	if (!usim_phy_add_map) {
		dev_err(gul_dev->dev, "USIM: CCSR Physical Not Initialized\n");
		kfree(usim_dev);
		kfree(sim);
		ret = -ENOMEM;
		goto out;
	}

	dev_dbg(gul_dev->dev, "ccsr phy 0x%p\n",
		(unsigned int *)usim_phy_add_map);

	usim_dev->usim_regs = usim_regs;
	usim_dev->usim_phy_add = usim_phy_add_map;
	usim_dev->sim = sim;

	gul_dev->usim_priv = (void *) usim_dev;
	usim_dev->gul_dev = gul_dev;

	/* Enable CLKs */
	usim_init_clk(usim_dev);

	for (i = 0; i < MAX_USIM_DEV; i++) {
		usim_dev_data[i] = kmalloc(sizeof(struct usim_device_data),
						GFP_KERNEL);
		if (usim_dev_data[i] == NULL) {
			kfree(usim_dev);
			kfree(sim);
			ret = -ENOMEM;
			goto out;
		}
		usim_dev_data[i]->usim_dev = usim_dev;
		usim_dev_data_g[i] = usim_dev_data[i];
	}

	/* Create Charecter Device */
	ret = create_usim_cdev(usim_dev_data);
	if (ret < 0) {
		pr_err("%s: USIM: Failed to create chardev\n", __func__);
		kfree(usim_dev);
		kfree(sim);
		goto out;
	}

	sim->status = USIM_STATE_INVALIDATED;
	sim->last_status = USIM_STATE_INVALIDATED;

	mutex_init(&usim_dev->mutex);
	init_waitqueue_head(&usim_dev->queue);

#ifdef USIM_POLL_TIMER
	/* Create timer for asynchronus events */
	dev_info(gul_dev->dev, "No IRQ; using timer %s\n", __func__);
	setup_timer(&usim_dev->poll_timer, poll_timer, (unsigned long)usim_dev);
	usim_dev->poll_timer.expires = jiffies + msecs_to_jiffies(10);
	add_timer(&usim_dev->poll_timer);
#endif

out:
	return ret;
}

/* Function: gul_usim_remove
 *
 * Description: USIM DRiver remove function
 *
 * Parameters:
 */

int gul_usim_remove(struct gul_dev *gul_dev)
{
	struct usim_dev *usim_dev = gul_dev->usim_priv;

	dev_dbg(gul_dev->dev, "USIM: Entering function %s\n", __func__);

	if (gul_dev->id != 0) {
		dev_dbg(gul_dev->dev, "USIM: %s: Multi-Geul not supported!\n",
				__func__);
		return 0;
	}

	if (usim_dev) {
		device_destroy(usim_dev_class, usim_dev_num);
		class_destroy(usim_dev_class);
		unregister_chrdev_region(usim_dev_num, 1);

		del_timer_sync(&usim_dev->poll_timer);

		kfree(usim_dev->sim);
		kfree(usim_dev);
		gul_dev->usim_priv = NULL;
	}

	dev_dbg(gul_dev->dev, "USIM: Exiting function %s\n", __func__);

	return 0;
}


