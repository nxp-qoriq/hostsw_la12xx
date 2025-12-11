/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2021-2022 NXP
 *
 * @usim_ioctl.h
 * @brief Function prototypes for usim ioctl commands.
 *
 * This contains the prototypes for usim ioct command
 * functions and eventually any macros, constants,
 * or global variables you will need.
 *
 */

#ifndef _USIM_IOCTL_H
#define _USIM_IOCTL_H

#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>

/**
 * A structure to represent apdu command.
 */
typedef struct {
	/**
	 * @brief Buffer pointer for command data.
	 */
	uint8_t *cmd_buff;
	/**
	 * @brief Length of command data.
	 */
	uint32_t cmd_len;
	/**
	 * @brief Buffer pointer for response data.
	 */
	uint8_t *rsp_buff;
	/**
	 * @brief Length of response data.
	 */
	uint32_t rcv_len;
} apdu_data_t;

/**
 * A structure to represent Transfer data for SIM_IOCTL_XFER.
 */
typedef struct {
	uint8_t *xmt_buffer;    /**< transmit buffer pointer. */
	int32_t xmt_length;		/**< transmit buffer length. */
	uint8_t *rcv_buffer;    /**< receive buffer pointer. */
	int32_t rcv_length;		/**< receive buffer length. */
	int type;				/**< transfer type: TPDU = 0, PTS = 1. */
	int timeout;			/**< transfer timeout in milliseconds. */
	uint8_t sw1;			/**< status word 1. */
	uint8_t sw2;			/**< status word 2. */
	uint8_t xmt_position;   /**< xmt position. */
} sim_xfer_t;

/**
 * @brief TPDU Transfer type for SIM_IOCTL_XFER.
 */
#define SIM_XFER_TYPE_TPDU 0

/**
 * @brief PTS Transfer type for SIM_IOCTL_XFER.
 */
#define SIM_XFER_TYPE_PTS  1


/**
 * @brief value of base ioctl command.
 */
#define SIM_IOCTL_BASE 0xc0

/**
 * @brief value of ioctl xfer command.
 */
#define SIM_IOCTL_XFER           _IOR(SIM_IOCTL_BASE, 6, sim_xfer_t)

/**
 * @brief value of ioctl command for poweron.
 */
#define SIM_IOCTL_POWER_ON       _IO(SIM_IOCTL_BASE, 7)

/**
 * @brief value of ioctl command for poweroff.
 */
#define SIM_IOCTL_POWER_OFF      _IO(SIM_IOCTL_BASE, 8)

/**
 * @brief value of ioctl command to get USIM status.
 */
#define USIM_IOCTL_STATUS        _IO(SIM_IOCTL_BASE, 13)

/**
 * @brief value of ioctl command to invalidate USIM.
 */
#define USIM_IOCTL_INVALIDATE_USIM  _IO(SIM_IOCTL_BASE, 14)

/**
 * @brief value of ioctl command to get physical address of usim controller.
 */
#define USIM_IOCTL_GET_PHY_ADD	 _IO(SIM_IOCTL_BASE, 15)

/** @brief Sends ioctl command to the driver.
 *
 *  This function
 *  transparent. This contains data as sequence of bytes.
 *
 *  @param[in] cmd ioctl command.
 *  @param[in] cmdlen length of ioctl command.
 *  @param[out] resp buffer which holds respose data from USIM for the
 *				command send.
 *  @param[in]  resplen length of the response data to receive from USIM.
 *  @return 0 if success or -1 if failure.
 */
extern int SendReceiveAPDU(unsigned char *cmd, int cmdlen, unsigned char *resp,
	int resplen);

/** @brief This function is used to poweron the USIM device..
 *
 *  @return 0 if success or -1 if failure.
 */
int usim_poweron(void);

/** @brief This function is used to poweroff the USIM device..
 *
 *  @return 0 if success or -1 if failure.
 */
int usim_poweroff(void);

#endif /* _USIM_IOCTL_H */
