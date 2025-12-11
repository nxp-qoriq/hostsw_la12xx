/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2020-2022 NXP
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "gul_tvd_ioctl.h"

#define TMU_DEVNAME_PREFIX		"gultvddev"

static inline int open_devtvd(int modem_id)
{
	char tvd_dev_name[50];
	int devtvd;

	sprintf(tvd_dev_name, "/dev/%s%d", TMU_DEVNAME_PREFIX, modem_id);
	printf("%s:Trying to open device(%d) : %s\n", __func__,
				modem_id, tvd_dev_name);

	devtvd = open(tvd_dev_name, O_RDWR);
	printf("%s: Open success\n", __func__);
	if (devtvd < 0) {
		printf("Error(%d): Cannot open %s\n", devtvd, tvd_dev_name);
		return devtvd;
	}
	return devtvd;
}

int libtvd_register(struct tvd *tvd_t, int modem_id)
{
	int ret = TVD_OK;

	tvd_t->tvdid = modem_id;

	/* Register with TVD  module */
	tvd_t->dev_tvd_handle = open_devtvd(modem_id);
	if (tvd_t->dev_tvd_handle < 0) {
		printf("Unable to open TVD dev.\n");
		ret = -TVD_OPEN_FAIL;
		goto err;
	}

	tvd_t->tvd_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

	if (tvd_t->tvd_eventfd < 0)
		printf("Failed to create eventfd.\n");
	else {
		ret = ioctl(tvd_t->dev_tvd_handle, IOCTL_GUL_TVD_REGISTER_EVEFD, tvd_t);
		if (ret < 0) {
			printf("IOCTL_GUL_TVD_REGISTER_EVEFD failed\n");
			close(tvd_t->tvd_eventfd);
			tvd_t->tvd_eventfd = -1;
		}
	}
err:
	return ret;
}

static inline int close_devtvd(int dev_tvd_handle)
{
	return close(dev_tvd_handle);
}

int libtvd_deregister(struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	if (!(tvd_t->tvd_eventfd < 0)) {
		ret = ioctl(tvd_t->dev_tvd_handle, IOCTL_GUL_TVD_DEREGISTER_EVEFD, tvd_t);

		if (ret < 0)
			printf("IOCTL_GUL_TVD_DEREGISTER_EVEFD failed\n");

		close(tvd_t->tvd_eventfd);
		tvd_t->tvd_eventfd = -1;
	}

	/* Close Dev Watchdog File */
	ret = close_devtvd(tvd_t->dev_tvd_handle);
	if (ret < 0) {
		printf("Error closing TVD device.\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_blockforEvent_read(int fd, struct tvd *tvd_t, int count)
{
	int32_t ret = TVD_OK;

	ret = read(fd, tvd_t, count);
	if (ret < 0) {
		printf("read failed.\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_get_thermal_event(int fd, struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	/* Call IOCTL for getting thermal events */
	ret = ioctl(fd, IOCTL_GUL_TVD_GET_THERMAL_EVENT, tvd_t);
	if (ret < 0) {
		printf("IOCTL_GUL_TVD_GET_THERMAL_EVENT failed\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_mtd_threshold_update(int fd, struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	/* Call IOCTL for Updating MTD thresholds */
	ret = ioctl(fd, IOCTL_GUL_TVD_PROGRAM_MTD_THRESHOLD, tvd_t);
	if (ret < 0) {
		printf("IOCTL_GUL_TVD_PROGRAM_MTD_THRESHOLD failed\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_rtd_threshold_update(int fd, struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	/* Call IOCTL for Updating RTD thresholds */
	ret = ioctl(fd, IOCTL_GUL_TVD_PROGRAM_RTD_THRESHOLD, tvd_t);
	if (ret < 0) {
		printf("IOCTL_GUL_TVD_PROGRAM_RTD_THRESHOLD failed\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_ctd_threshold_update(int fd, struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	/* Call IOCTL for Updating CTD thresholds */
	ret = ioctl(fd, IOCTL_GUL_TVD_PROGRAM_CTD_THRESHOLD, tvd_t);
	if (ret < 0) {
		printf("IOCTL_GUL_TVD_PROGRAM_CTD_THRESHOLD failed\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_mtd_get_power_info(int fd, struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	/* Call IOCTL to power info */
	ret = ioctl(fd, IOCTL_GUL_TVD_MTD_GET_POWER_INFO, tvd_t);
	if (ret < 0) {
		printf("IOCTL_GUL_TVD_MTD_GET_POWER_INFO failed\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_mtd_get_temp(int fd, struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	/* Call IOCTL for Updating MTD thresholds */
	ret = ioctl(fd, IOCTL_GUL_TVD_MTD_GET_TEMP, tvd_t);
	if (ret < 0) {
		printf("IOCTL_GUL_TVD_MTD_GET_TEMP failed\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_ctd_get_temp(int fd, struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	/* Call IOCTL for Updating CTD thresholds */
	ret = ioctl(fd, IOCTL_GUL_TVD_CTD_GET_TEMP, tvd_t);
	if (ret < 0) {
		printf("IOCTL_GUL_TVD_CTD_GET_TEMP failed\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_rtd_get_temp(int fd, struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	/* Call IOCTL for Updating RTD thresholds */
	ret = ioctl(fd, IOCTL_GUL_TVD_RTD_GET_TEMP, tvd_t);
	if (ret < 0) {
		printf("IOCTL_GUL_TVD_CTD_GET_TEMP failed\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_mtd_hysteresis_update(int fd, struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	/* Call IOCTL for Updating MTD Hysteresis value */
	ret = ioctl(fd, IOCTL_GUL_TVD_PROGRAM_MTD_HYSTERESIS, tvd_t);
	if (ret < 0) {
		printf("IOCTL_GUL_TVD_PROGRAM_MTD_HYSTERESIS failed\n");
		goto err;
	}
err:
	return ret;
}

int libtvd_ctd_hysteresis_update(int fd, struct tvd *tvd_t)
{
	int32_t ret = TVD_OK;

	/* Call IOCTL for Updating CTD Hysteresis value */
	ret = ioctl(fd, IOCTL_GUL_TVD_PROGRAM_CTD_HYSTERESIS, tvd_t);
	if (ret < 0) {
		printf("IOCTL_GUL_TVD_PROGRAM_CTD_HYSTERESIS failed\n");
		goto err;
	}
err:
	return ret;
}
