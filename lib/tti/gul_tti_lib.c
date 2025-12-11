/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2019-2023 NXP
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "gul_modinfo.h"
#include <gul_tti_ioctl.h>

#define GUL_TTI_DEVNAME_PREFIX "gulttidev"

#define TBGEN1_BASE_ADDR			0x1120000
#define TBGEN2_BASE_ADDR			0x1124000
#define TBGEN_TIME_STAMP_REGISTER		0x20
#define TICK_TO_MHz_DIV				1000000

struct gul_ccsr_info *ccsr_info[MAX_MODEM] = {NULL};
void *addr[MAX_MODEM] = {NULL};
static int tti_fd[MAX_MODEM];

static inline int open_devtti(struct tti *tti_t, int modem_id)
{
	char tti_dev_name[50];
	int devtti;

	sprintf(tti_dev_name, "/dev/%s%d-%d", GUL_TTI_DEVNAME_PREFIX,
					modem_id, tti_t->ttid);

	printf("Trying to open device : %s\n", tti_dev_name);
	devtti = open(tti_dev_name, O_RDWR);
	if (devtti < 0) {
		printf("Error(%d): Cannot open %s\n", devtti, tti_dev_name);
		return devtti;
	}
	return devtti;
}

int modem_tti_register(struct tti *tti_t, int modem_id, int tti_event_flag)
{
	int32_t ret = 0;

	/* Register TTI */
	tti_t->dev_tti_handle = open_devtti(tti_t, modem_id);
	if (tti_t->dev_tti_handle < 0) {
		printf("Unable to open device.\n");
		ret = -2;
		goto err;
	}

	if (tti_event_flag) {
		tti_t->tti_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (tti_t->tti_eventfd < 0) {
			printf("Failed to create eventfd\n");
			close(tti_t->dev_tti_handle);
			ret = tti_t->tti_eventfd;
			goto err;
		}
	}

	/* IOCTL - register FD with kernel */
	ret = ioctl(tti_t->dev_tti_handle, IOCTL_GUL_MODEM_TTI_REGISTER, tti_t);
	if (ret < 0) {
		printf("IOCTL_GUL_MODEM_TTI_REGISTER failed.\n");
		close(tti_t->dev_tti_handle);
		goto err;
	}

err:
	return ret;
}
int get_tbgen_counter_freq(off_t offset, long size)
{
	int loop, fd;
	void *addr, *tbgen1, *tbgen2;
	uint32_t tbgen1_timer_count[2], tbgen2_timer_count[2];

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (-1 == fd) {
		perror("dev open failed:");
		return -1;
	}

	addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
	if (addr == MAP_FAILED) {
		perror("mmap failed:");
		close(fd);
		return -1;
	}
	tbgen1 = addr + TBGEN1_BASE_ADDR + TBGEN_TIME_STAMP_REGISTER;
	tbgen2 = addr + TBGEN2_BASE_ADDR + TBGEN_TIME_STAMP_REGISTER;

	for (loop = 0; loop < 2; loop++) {
		tbgen1_timer_count[loop] = *((uint32_t *)tbgen1);
		tbgen2_timer_count[loop] = *((uint32_t *)tbgen2);
		sleep(1);
	}

	printf("\nTBGEN1 freq as per counter: %.3f Mhz\n",
		(float)(tbgen1_timer_count[1] -
			tbgen1_timer_count[0])/TICK_TO_MHz_DIV);
	printf("TBGEN2 freq as per counter: %.3f Mhz\n",
		(float)(tbgen2_timer_count[1] -
			tbgen2_timer_count[0])/TICK_TO_MHz_DIV);
	munmap(addr, size);
	close(fd);
	return 0;
}
void *get_ccsr_addr(int modem_id)
{
	if (addr[modem_id] != NULL)
		return addr[modem_id];

	char dev_name[32];
	int fd, ret;
	modinfo_t mil = {0};
	modinfo_t *mi = &mil;

	sprintf(dev_name, "/dev/gul%d", modem_id);
	fd = open(dev_name, O_RDONLY);
	if (-1 == fd) {
		perror("dev open failed:");
		return NULL;
	}
	ret = ioctl(fd, IOCTL_GUL_MODINFO_GET, (modinfo_t *) &mil);
	if (ret < 0) {
		printf("IOCTL_GUL_MODINFO_GET failed.\n");
		close(fd);
		return NULL;
	}
	if (ccsr_info[modem_id] == NULL)
		ccsr_info[modem_id] =
			(struct gul_ccsr_info *)
			malloc(sizeof(struct gul_ccsr_info));

	ccsr_info[modem_id]->offset = mi->ccsr.host_phy_addr;
	ccsr_info[modem_id]->size = mi->ccsr.size;
	close(fd);

	tti_fd[modem_id] = open("/dev/mem", O_RDWR);
	if (-1 == tti_fd[modem_id]) {
		perror("dev open failed:");
		return NULL;
	}
	addr[modem_id] = mmap(NULL, ccsr_info[modem_id]->size,
			PROT_READ | PROT_WRITE, MAP_SHARED,
			tti_fd[modem_id], ccsr_info[modem_id]->offset);
	if (addr == MAP_FAILED) {
		perror("mmap failed:");
		close(tti_fd[modem_id]);
		return NULL;
	}
	return addr[modem_id];
}

void unmap_tbgen_addr(int modem_id)
{
	if (addr[modem_id] != NULL) {
		munmap(addr[modem_id], ccsr_info[modem_id]->size);
		addr[modem_id] = NULL;
		close(tti_fd[modem_id]);
	}
}

int get_tbgen_freq_info(int modem_id)
{
	char dev_name[32];
	int fd, ret = 0;
	modinfo_t mil = {0};
	modinfo_t *mi = &mil;

	sprintf(dev_name, "/dev/gul%d", modem_id);
	fd = open(dev_name, O_RDONLY);
	if (-1 == fd) {
		perror("dev open failed:");
		return -1;
	}
	ret = ioctl(fd, IOCTL_GUL_MODINFO_GET, (modinfo_t *) &mil);
	if (ret < 0) {
		printf("IOCTL_GUL_MODINFO_GET failed.\n");
		close(fd);
		return -1;
	}
	printf("\nTBGEN_1 src:%s \tfreq:%dMhz\tls_adc_sps :%d ls_dac_sps:%d\n",
			mi->clk_info.tbgen1_src, mi->clk_info.tbgen1_freq/1000,
			mi->clk_info.ls_adc_sps, mi->clk_info.ls_dac_sps);
	printf("TBGEN_2 src:\%s\t\tfreq:%d Mhz\thsdcs_sps:%d\n", mi->clk_info.tbgen2_src,
			mi->clk_info.tbgen2_freq/1000,  mi->clk_info.hs_dcs_sps);

	if (get_tbgen_counter_freq(mi->ccsr.host_phy_addr, mi->ccsr.size))
		printf("Failed to get tbgen counter val\n");

	close(fd);
	return 0;
}


static inline int close_devtti(int dev_tti_handle)
{
	return close(dev_tti_handle);
}

int modem_tti_deregister(struct tti *tti_t)
{
	int32_t ret = 0;

	/* Free IRQs */
	ret = ioctl(tti_t->dev_tti_handle, IOCTL_GUL_MODEM_TTI_DEREGISTER,
			tti_t);
	if (ret < 0) {
		printf("IOCTL_GUL_MODEM_TTI_DEREGISTER failed.\n");
		goto err;
	}

	if (!(tti_t->tti_eventfd < 0)) {
		close(tti_t->tti_eventfd);
		tti_t->tti_eventfd = -1;
	}

	/* Close Dev TTI File */
	ret = close_devtti(tti_t->dev_tti_handle);
	if (ret < 0) {
		printf("Error closing TTI device.\n");
		goto err;
	}
err:
	return ret;
}
