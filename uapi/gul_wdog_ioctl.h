/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2020-2022 NXP
 */

#ifndef __GUL_WDOG_IOCTL__
#define __GUL_WDOG_IOCTL__

#include <linux/ioctl.h>

#define GUL_WDOG_MAGIC   'W'

#define IOCTL_GUL_MODEM_WDOG_REGISTER    _IOWR(GUL_WDOG_MAGIC, 1, struct wdog *)
#define IOCTL_GUL_MODEM_WDOG_DEREGISTER  _IOWR(GUL_WDOG_MAGIC, 2, struct wdog *)
#define IOCTL_GUL_MODEM_WDOG_RESET       _IOWR(GUL_WDOG_MAGIC, 3, struct wdog *)
#define IOCTL_GUL_MODEM_WDOG_GET_STATUS  _IOWR(GUL_WDOG_MAGIC, 4, struct wdog *)
#define IOCTL_GUL_MODEM_WDOG_GET_DOMAIN  _IOWR(GUL_WDOG_MAGIC, 5, struct wdog *)

/** \addtogroup  HOST_LIBWDOG_API
 *  @{
 */

/**
 * Maximum number of watchdog instances
 */
#ifndef MAX_MODEM
#define MAX_MODEM 4
#endif
/**
 * A structure holds modem watchdog information
 */
struct wdog {
	uint32_t	wdogid; /**< modem watchdog id */
	int32_t		dev_wdog_handle; /**< fd of modem watchdog device */
	int32_t		wdog_eventfd; /**< eventfd for watchdog events */
	int32_t		wdog_modem_status; /**< modem status */
	int32_t		domain_nr; /**< pci domain assigned by linux */
};

/**
 * A modem watchdog status
 */
enum wdog_modem_status {
	WDOG_MODEM_NOT_READY = 0,/**< Modem not ready */
	WDOG_MODEM_READY,	/**< Modem ready */
	WDOG_MODEM_HSDCS_ERR, /*Modem reset due to hsdcs error*/
	WDOG_MODEM_WARMUP_RESET /*Modem reset due to warmup reset*/
};

/** @} */

#define PCI_DOMAIN_NR_INVALID		-1
#define MODEM_PCI_RESCAN_FILE		"/sys/bus/pci/rescan"
#define MODEM_PCI_DEVICE_PATH		"/sys/bus/pci/devices"

/* WDOG Lib error codes */
#define MODEM_WDOG_OK			0
#define MODEM_WDOG_OPEN_FAIL		1
#define MODEM_WDOG_WRITE_FAIL		2

#endif /*__GUL_WDOG_IOCTL__*/

