/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2022-2023 NXP
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "gul_cli_ioctl.h"

#define DEVNAME_PREFIX    "gul_cli_dev"

static inline int open_dev(int modem_id)
{
	char dev_name[50];
	int dev;

	sprintf(dev_name, "/dev/%s%d", DEVNAME_PREFIX, modem_id);
#if DEBUG_CLI
	printf("%s:Trying to open device(%d) : %s\n", __func__,
				modem_id, dev_name);
#endif

	dev = open(dev_name, O_RDWR | O_NONBLOCK);
	if (dev < 0)
		printf("Error(%d) when openning %s\n", dev, dev_name);

	return dev;
}

int libcli_register(struct cli *cli_t, int modem_id, int core_id)
{
	int ret = CLI_OK;

	cli_t->id = modem_id;
	cli_t->core_id = core_id;

	cli_t->handle = open_dev(modem_id);
	if (cli_t->handle < 0) {
		printf("Unable to open dev.\n");
		ret = -CLI_OPEN_FAIL;
		goto err;
	}

	cli_t->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

	if (cli_t->event_fd < 0)
		printf("Failed to create eventfd.\n");
	else {
		ret = ioctl(cli_t->handle, IOCTL_GUL_CLI_REGISTER_EVENTFD,
				cli_t);
		if (ret < 0) {
			printf("IOCTL_GUL_CLI_REGISTER_EVENTFD failed\n");
			close(cli_t->event_fd);
			cli_t->event_fd = -1;
		}
	}
err:
	return ret;
}

static inline int close_dev(int dev_handle)
{
	return close(dev_handle);
}

int libcli_deregister(struct cli *cli_t)
{
	int32_t ret = CLI_OK;

	if (!(cli_t->event_fd < 0)) {
		ret = ioctl(cli_t->handle, IOCTL_GUL_CLI_DEREGISTER_EVENTFD,
			    cli_t);

		if (ret < 0)
			printf("IOCTL_GUL_CLI_DEREGISTER_EVENTFD failed\n");

		close(cli_t->event_fd);
		cli_t->event_fd = -1;
	}

	ret = close_dev(cli_t->handle);
	if (ret < 0) {
		printf("Error closing cliApp device.\n");
		goto err;
	}
err:
	return ret;
}

int libcli_blockforEvent_read(int fd, struct cli *cli_t, int count)
{
	int32_t ret = CLI_OK;

	ret = read(fd, cli_t, count);
	if (ret < 0)
		perror("read");

	return ret;
}

int libcli_send_msi_sync(int fd, struct cli *cli_t)
{
	int32_t ret = CLI_OK;

	ret = ioctl(fd, IOCTL_GUL_CLI_SEND_MSI_TO_MODEM, cli_t);
	if (ret < 0)
		perror("ioctl sync");

	return ret;
}

int libcli_send_msi_async(int fd, struct cli *cli_t)
{
	int32_t ret = CLI_OK;

	ret = ioctl(fd, IOCTL_GUL_CLI_SEND_MSI_TO_MODEM, cli_t);
	if (ret < 0)
		perror("ioctl async");

	return ret;
}

int libcli_send_msi_wait(int fd, struct cli *cli_t)
{
	int32_t ret = CLI_OK;

	ret = ioctl(fd, IOCTL_GUL_CLI_SEND_MSI_TO_MODEM, cli_t);
	if (ret < 0)
		perror("ioctl wait");

	return ret;
}

int libcli_dump_core_log(int fd, struct cli *cli_t)
{
	int32_t ret = CLI_OK;

	ret = ioctl(fd, IOCTL_GUL_CLI_DUMP_CORE_LOG, cli_t);
	if (ret < 0)
		perror("ioctl dump logs");

	return ret;
}
