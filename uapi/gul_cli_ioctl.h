/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2022 NXP
 */

#ifndef __GUL_CLI_IOCTL__
#define __GUL_CLI_IOCTL__

#include <linux/ioctl.h>

#define MAGIC   'K'

#define IOCTL_GUL_CLI_SEND_MSI_TO_MODEM	_IOWR(MAGIC, 1, struct cli *)
#define IOCTL_GUL_CLI_REGISTER_EVENTFD	_IOWR(MAGIC, 4, struct cli *)
#define IOCTL_GUL_CLI_DEREGISTER_EVENTFD	_IOWR(MAGIC, 5, struct cli *)
#define IOCTL_GUL_CLI_DUMP_CORE_LOG	_IOWR(MAGIC, 6, struct cli *)

struct cli_mbox_u {
	uint32_t msg_id;
	uint32_t data;
	uint32_t data2;
	uint32_t data3;
} __attribute__ ((packed));

struct cli {
	uint32_t id;		/* modem id */
	uint32_t core_id;	/* core id */
	int handle;		/* cli handle for fd */
	int event_fd;           /* eventfd for tvd events */
	struct cli_mbox_u mbox; /* msg_id + data */
};

#define CLI_OK				0
#define CLI_OPEN_FAIL			1
#define CLI_WRITE_FAIL			2

#endif /*__GUL_CLI_IOCTL__*/
