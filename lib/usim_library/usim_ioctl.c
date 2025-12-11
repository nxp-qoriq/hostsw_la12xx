/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2021-2022 NXP
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include "usim_ioctl.h"
#include "usim_library.h"

extern int debug;
extern int usim_fd;

/* unsigned char * cmd      transmit buffer
 * int   cmdlen   transmit length
 * unsigned char * resp     receive buffer
 * int   resplen  expected receive length
 *
 * Return Value:
 * 0x00006xxx                ISO 7816-3 error codes
 * 0x00009xxx                Application specific codes
 * -SIM_E_NOCARD             No card inserted
 * -SIM_E_ACCESS             Memory violation error
 * -SIM_E_TPDUSHORT          TPDU less than 5 bytes
 * -SIM_E_INVALIDXMTLENGTH   Requested transmit is too long
 * -SIM_E_INVALIDRCVLENGTH   Requested receive is too long
 * -SIM_E_TIMEOUT            Transfer Timeout
 * -SIM_E_NACK               No ACK received
 */

int SendReceiveAPDU(unsigned char *cmd, int cmdlen, unsigned char *resp,
	int resplen)
{
	int errval;

	sim_xfer_t tpdu = { cmd, cmdlen, resp, resplen,
		SIM_XFER_TYPE_TPDU, 100
	};

	errval = ioctl(usim_fd, SIM_IOCTL_XFER, &tpdu);

	return errval;
};

int usim_poweron(void)
{
	int errval;

	errval = ioctl(usim_fd, SIM_IOCTL_POWER_ON, NULL);
	if (!errval) {
		usim_dbg(LOG_INFO, "%s::%d Success\n", __func__, __LINE__);
	} else {
		usim_dbg(LOG_ERROR, "%s::%d Failed\n", __func__, __LINE__);
	}

	return errval;
}

int usim_poweroff(void)
{
	int errval;

	errval = ioctl(usim_fd, SIM_IOCTL_POWER_OFF, NULL);
	if (!errval) {
		usim_dbg(LOG_INFO, "%s::%d Success\n", __func__, __LINE__);
	} else {
		usim_dbg(LOG_ERROR, "%s::%d Failed\n", __func__, __LINE__);
	}

	return errval;
}

