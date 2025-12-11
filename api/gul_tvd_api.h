/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)*/
/*
 * Copyright 2020-2022 NXP
 */


#ifndef __GUL_TVD_API__
#define __GUL_TVD_API__

#include "gul_tvd_ioctl.h"
#include <gul_host_if.h>
/**
 * @file gul_tvd_api.h
 * @brief TVD API support
 *
 * @addtogroup  HOST_LIBTVD_API
 * @{
 */

/*
 * @details  Open TVD device node
 * @param[in] modem_id (0...MAX_MODEM_TMU_INSTANCE)
 * @param[out] tvd_t   reference to struct tvd, gets open file descriptor
 * @return
 *	- On success return 0
 *	- On failure return negative error number
 */
int libtvd_register(struct tvd *tvd_t, int modem_id);

/*
 * @details  Unregister TVD instance.
 * @param[in] tvd_t  reference to struct tvd
 * @return
 *      - On success return 0
 *      - On failure return negative error number
 */
int libtvd_deregister(struct tvd *tvd_t);

/*
 * @details  Wait for TVD event, which can be Modem or CPU TMU event.
 * Blocking read.
 * @param[in]  fd     TVD device fd
 * @param[out] tvd_t  reference to struct tvd, event details filled in tvd_t
 * @param[int] count  number of bytes to read
 * @return
 *      - On success return 0
 *      - On failure return negative error number
 */
int libtvd_blockforEvent_read(int fd, struct tvd *tvd_t, int count);

/*
 * @details Wait for TVD event, which can be Modem TMU or CPU TMU event.
 * This function same as like libtvd_blockforEvent_read,
 * we can use either one of them
 * @param[in]  fd     TVD device fd
 * @param[out] tvd_t  reference to struct tvd, event details filled in tvd_t
 * @return
 *      - On success return 0
 *      - On failure return negative error number
 */
int libtvd_get_thermal_event(int fd, struct tvd *tvd_t);

/*
 * @details   Update MTD thresholds.
 * @param[in] fd  TVD device fd
 * @param[in] threshold_t  reference to struct thermal_threshold
 * @return
 *	- On success return 0
 *	- On failure return negative error number
 */
int libtvd_mtd_threshold_update(int fd, struct tvd *tvd_t);

/*
 * @details   Update RTD thresholds.
 * @param[in] fd  TVD device fd
 * @param[in] threshold_t  reference to struct thermal_threshold
 * @return
 *	- On success return 0
 *	- On failure return negative error number
 */
int libtvd_rtd_threshold_update(int fd, struct tvd *tvd_t);

/*
 * @details   Update CTD thresholds.
 * @param[in] fd  TVD device fd
 * @param[in] threshold_t  reference to struct thermal_threshold
 * @return
 *      - On success return 0
 *      - On failure return negative error number
 */
int libtvd_ctd_threshold_update(int fd, struct tvd *tvd_t);

/*
 * @details  Update CTD Hysteresis value
 * Hysteresis value added to Threshold value
 * So for Temp rising, if temp crosses (Threshold + Hystereis)
 * then will get thermal event.
 * For Temp falling, if temp comes below (Threshold - Hystereis),
 * then will get thermal event.
 * @param[in] fd  TVD device fd
 * @param[in] hysteresis_t  reference to struct thermal_hysteresis
 * @return
 *	- On success return 0
 *	- On failure return negative error number
 */
int libtvd_ctd_hysteresis_update(int fd, struct tvd *tvd_t);

/*
 * @details  Update MTD Hysteresis value
 * Hysteresis value added to Threshold value
 * So for Temp rising, if temp crosses (Threshold + Hystereis)
 * then will get thermal event.
 * For Temp falling, if temp comes below (Threshold - Hystereis),
 * then will get thermal event.
 * @param[in] fd  TVD device fd
 * @param[in] hysteresis_t  reference to struct thermal_hysteresis
 * @return
 *      - On success return 0
 *      - On failure return negative error number
 */
int libtvd_mtd_hysteresis_update(int fd, struct tvd *tvd_t);

/*
 * @details  Gets the current MTD temp
 * @param[in] fd  TVD device fd
 * @param[out] tvd_t  reference to struct tvd, gets current temp
 * @return
 *      - On success return 0
 *      - On failure return negative error number
 */
int libtvd_mtd_get_temp(int fd, struct tvd *tvd_t);

/*
 * @details  Gets the current CTD temp
 * @param[in] fd  TVD device fd
 * @param[out] tvd_t  reference to struct tvd, gets current temp
 * @return
 *      - On success return 0
 *      - On failure return negative error number
 */
int libtvd_ctd_get_temp(int fd, struct tvd *tvd_t);

/*
 * @details  Gets the current RTD temp
 * @param[in] fd  TVD device fd
 * @param[out] tvd_t  reference to struct tvd, gets current temp
 * @return
 *      - On success return 0
 *      - On failure return negative error number
 */
int libtvd_rtd_get_temp(int fd, struct tvd *tvd_t);

/*
 * @details  Gets the power info from MTD i2c sensor.
 * @param[in] fd  TVD device fd
 * @param[out] tvd_t  reference to struct tvd, gets power info
 * @return
 *      - On success return 0
 *      - On failure return negative error number
 */
int libtvd_mtd_get_power_info(int fd, struct tvd *tvd_t);
#endif /* __GUL_TVD_API__ */

