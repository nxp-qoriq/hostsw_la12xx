/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2021-2022 NXP
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "aka_functions.h"
#include "usim_library.h"
#include "usim_ioctl.h"
#include "usim_apdu.h"
#include "usim_event.h"

extern int debug;

/* Function: usim_card_detect
 *
 * Description: Check the card detection status
 *
 * Parameters:
 * event_handle_t *event_handle_p
 */

int usim_card_detect(event_handle_t *event_handle_p)
{
	struct usim_regs_map *regs = event_handle_p->usim_regs;
	uint32_t port_status = 0;

	usim_dbg(LOG_INFO, "usim_event: entering function %s\n", __func__);

	event_handle_p->state = event_handle_p->state_cur;

	if (iord32(MOD_LE, &regs->usim_port0_detect) & SIM_PORT_DETECT_SPDP) {
		usim_dbg(LOG_INFO, "%s card removed\n", __func__);
		port_status = iord32(MOD_LE, &regs->usim_port0_detect);
		port_status &= ~SIM_PORT_DETECT_SPDS;
		iowr32(MOD_LE, port_status, &regs->usim_port0_detect);
		event_handle_p->state_cur = USIM_STATE_DISCONNECTED;
	} else {
		usim_dbg(LOG_INFO, "%s card inserted\n", __func__);
		port_status = iord32(MOD_LE, &regs->usim_port0_detect);
		port_status |= SIM_PORT_DETECT_SPDS;
		iowr32(MOD_LE, port_status, &regs->usim_port0_detect);
		event_handle_p->state_cur = USIM_STATE_CONNECTED;
	};

	port_status = iord32(MOD_LE, &regs->usim_port0_detect);
	port_status |= SIM_PORT_DETECT_SDI;
	port_status &= ~SIM_PORT_DETECT_SDIM;
	iowr32(MOD_LE, port_status, &regs->usim_port0_detect);

	usim_dbg(LOG_INFO, "usim_event: exiting function %s\n", __func__);

	return 0;
}

void delay_clk(uint32_t cycles, event_handle_t *event_handle_p)
{
	uint32_t cntl_val;
	struct usim_regs_map *regs = event_handle_p->usim_regs;

	usim_dbg(LOG_INFO, "Providing delay of = %#x\n", cycles);

	cntl_val = iord32(MOD_LE, &regs->usim_cntl);

	/* RESET GPCNT COUNTER */
	iowr32(MOD_LE, (cntl_val & 0xf9ff), &regs->usim_cntl);
	iord32(MOD_LE, &regs->usim_xmt_status);

	/* CLEAR GPCNT FLAG */
	iowr32(MOD_LE, 0x100, &regs->usim_xmt_status);
	/* WRITING DELAY VALUE TO GPCNT */
	iowr32(MOD_LE, cycles, &regs->usim_gpcnt);

	cntl_val = iord32(MOD_LE, &regs->usim_cntl);
	/* SETTING GPCNT COUNTER TO CARD CLK */
	iowr32(MOD_LE, (cntl_val | 0x200), &regs->usim_cntl);

}

void bit_set_polling(uint32_t *addr, uint32_t req_value, int i)
{
	uint32_t value_at_addr = iord32(MOD_LE, addr);

	while (((value_at_addr) & req_value) != req_value) {

		value_at_addr = iord32(MOD_LE, addr);
		if (i == 20000000) {
			 usim_dbg(LOG_INFO, "%sBit Not Set in usim_rcv_status\n",
								__func__);
			break;
		}
		i++;
	}
}

void clear_gpcnt_counter(event_handle_t *event_handle_p)
{
	struct usim_regs_map *regs = event_handle_p->usim_regs;

	/* RESET GPCNT COUNTER */
	iowr32(MOD_LE, ((iord32(MOD_LE, &regs->usim_cntl)) & 0xf9ff), &regs->usim_cntl);
	/* CLEAR GPCNT FLAG */
	iowr32(MOD_LE, 0x100, &regs->usim_xmt_status);
}

void wait_for_tc(event_handle_t *event_handle_p)
{
	struct usim_regs_map *regs = event_handle_p->usim_regs;

	/* polling for tc, etc and tfe to set */
	bit_set_polling(&regs->usim_xmt_status, 0x38, 0);
	/* clearing the above set flags */
	iowr32(MOD_LE, 0x38, &regs->usim_xmt_status);
}

void atr_us(event_handle_t *event_handle_p)
{
	struct usim_regs_map *regs = event_handle_p->usim_regs;
	int i = 0;
	uint32_t value;
	uint32_t atr_data;
	uint32_t atr_length = 0;

	usim_card_detect(event_handle_p);

	/* SIM CARD ACTIVATION */
	iowr32(MOD_LE, 0x40, &regs->usim_port0_cntl);       //3VOLT0 = 1
	iowr32(MOD_LE, 0x42, &regs->usim_port0_cntl);       //SVEN = 1
	iowr32(MOD_LE, 0x1, &regs->usim_od_config);         //OD_P0 = 1
	iowr32(MOD_LE, 0x1F, &regs->usim_clk_prescaler);    //CLK_PRESCALER = 0XE
	//iowr32(MOD_LE, 0xFF, &regs->usim_clk_prescaler);    //CLK_PRESCALER = 0XE
	iowr32(MOD_LE, 0x46, &regs->usim_port0_cntl);       //STEN0 = 1
	iowr32(MOD_LE, 0xfe, &regs->usim_guard_cntl);       //GUARD_CNTL = 0XFE
	iowr32(MOD_LE, 0x16, &regs->usim_cntl);             //ANACK=1,ICM =1,SAMPLE12=1
	iowr32(MOD_LE, 0x3, &regs->usim_xmt_threshold);     //XMT_THRESHOLD =3
	iowr32(MOD_LE, 0xf23, &regs->usim_rcv_status);      //CLEARING RCV_STATUS
	iowr32(MOD_LE, 0x56, &regs->usim_port0_cntl);       //SCEN0 = 1


	/* STARTING COUNTER FOR 400 CYCLES DELAY BETWEEN SCEN AND SRST */
	delay_clk(0x190, event_handle_p);

	/* WAITING TILL GPCNT = 1 , MEANS COUNTER REACHED THE VALUE */
	bit_set_polling(&regs->usim_xmt_status, 0x100, 0);
	clear_gpcnt_counter(event_handle_p);
	/* REMOVING RESET */
	iowr32(MOD_LE, 0x5e, &regs->usim_port0_cntl);
	/* starting gpcnt counter to count for 40,000 cycles */
	delay_clk(0x9c40, event_handle_p);

	/* CHAR WAIT COUNTER VALUE SETTING AND ENABLING */
	/* UNMASKING CWT INTERRUPT */
	iowr32(MOD_LE, ((iord32(MOD_LE, &regs->usim_int_mask)) & 0x3dff), &regs->usim_int_mask);
	/* SET THE VALUE TO 0x2580 = 0d9600 */
	iowr32(MOD_LE, 0x2580, &regs->usim_char_wait);
	//iowr32(MOD_LE, 0x3C00, &regs->usim_char_wait);
	/* ENABLING CWT COUNTE */
	iowr32(MOD_LE, ((iord32(MOD_LE, &regs->usim_cntl)) | 0x800), &regs->usim_cntl);
	/* ENABLING RECEIVER TO RECEIVE ATR */
	iowr32(MOD_LE, 0x01, &regs->usim_enable);

	/* ATR Test */


	value = iord32(MOD_LE, &regs->usim_xmt_status);

	bit_set_polling(&regs->usim_rcv_status, 0x10, 0);

	 usim_dbg(LOG_INFO, "Reading ATR - %s\n", __func__);

	/*reading ATR */
	value = iord32(MOD_LE, &regs->usim_rcv_status);
	while ((value & 0x10) == 0x10) {
		i = 0;
		atr_length++;
		atr_data = iord32(MOD_LE, &regs->usim_rcv_buff);
		 usim_dbg(LOG_INFO, " %x", atr_data);

		value = iord32(MOD_LE, &regs->usim_rcv_status);
		while ((value & 0x10) != 0x10) {
			if (i == 1000000)
				break;
			value = iord32(MOD_LE, &regs->usim_rcv_status);
			i++;
		}
		if (i == 1000000)
			break;
	}
	 usim_dbg(LOG_INFO, "\nReading ATR Compleate - %s\n", __func__);
}

vtq_usim_state_t vtq_usim_get_status(event_handle_t *event_handle_p)
{
	int status, ret, len = 10;
#ifdef REG_READ_TEST
	uint32_t *usim_reg = (uint32_t *)event_handle_p->usim_ccsr_map;
	uint32_t reg[30];
	int i;

	/* Register Read Test */
	for (i = 0 ; i < 30 ; i++) {
		reg[i] = iord32(MOD_LE, usim_reg+i);
		usim_dbg(LOG_INFO, "reg[%d]=0x%x\n", i, reg[i]);
	}
#endif
	usim_dbg(LOG_INFO, "usim_event: entering function %s\n", __func__);

	ret = usim_card_detect(event_handle_p);
	if (ret) {
		usim_dbg(LOG_ERROR, "%s:%d unable to get updated status value:%d\n",
							__func__, __LINE__, ret);
		return -1;
	}

	/* check if new sim card is inserted */
	if (event_handle_p->state_cur != USIM_STATE_DISCONNECTED) {
		/* save ICCID of the old sim card */
		memcpy(event_handle_p->iccid_old, event_handle_p->iccid_new,
						event_handle_p->iccid_len_new);
		event_handle_p->iccid_len_old = event_handle_p->iccid_len_new;

		/* save ICCID of the current sim card */
		ret = usim_read_transparent(USIM_ICCID_FILEID, &len,
					&event_handle_p->iccid_new[0], USIM_MF_FILEID);
		if (ret == 0) {
			usim_dbg(LOG_INFO, "current icicid of len=%d\n", len);
			hex_dump(&event_handle_p->iccid_new[0], len);
			event_handle_p->iccid_len_new = len;

			if (memcmp(&event_handle_p->iccid_old[0],
					&event_handle_p->iccid_new[0],
					event_handle_p->iccid_len_new) != 0) {
				event_handle_p->state_cur = USIM_STATE_CHANGED;
			}

		} else {
			usim_dbg(LOG_ERROR, "%s:: read for current ICCID failed\n", __func__);
		}


	}
	status = event_handle_p->state_cur;

	usim_dbg(LOG_INFO, "usim_event: exiting function %s\n", __func__);

	return status;
}

