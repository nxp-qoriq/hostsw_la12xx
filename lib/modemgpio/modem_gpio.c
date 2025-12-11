/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright 2021-2023 NXP
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <endian.h>
#include <stdint.h>
#include <modem_gpio.h>
#include <sys/ioctl.h>
#include "gul_modinfo.h"

#define GUL_DEV_NAME_PREFIX "gul"
int gpio_devmem_fd;
void *virt_addr_gpio, *map_base_gpio;
int ccsr_size;

static int get_ccsr_base_addr(modinfo_t *mod_info, int modem_id)
{
	char dev_name[32];
	int fd, ret = 0;

	sprintf(dev_name, "/dev/%s%d", GUL_DEV_NAME_PREFIX, modem_id);
	fd = open(dev_name, O_RDONLY);
	if (-1 == fd) {
		perror("dev open failed:");
		return -1;
	}

	ret = ioctl(fd, IOCTL_GUL_MODINFO_GET, (modinfo_t *)mod_info);
	if (ret < 0) {
		printf("IOCTL_GUL_MODINFO_GET failed.\n");
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}
int lib_open_modem_gpio(int modem_id)
{
	modinfo_t mil = {0};
	modinfo_t *mi = &mil;

	if (get_ccsr_base_addr(&mil, modem_id)) {
		printf("failed to get ccsr base address value\n");
		return -1;
	}
	ccsr_size = mi->ccsr.size;
	if ((gpio_devmem_fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;

	map_base_gpio = mmap(0, ccsr_size, PROT_READ | PROT_WRITE, MAP_SHARED,
			gpio_devmem_fd, mi->ccsr.host_phy_addr);
	if (map_base_gpio == (void *) -1) FATAL;

	virt_addr_gpio = map_base_gpio + GPIO_BASE_ADDR;
	return 0;
}

int lib_is_pin_supported(uint8_t pin)
{
	if (pin <= MAX_PIN)
		return SUCCESS;

	return PIN_NOT_SUPPORTED;
}

int lib_is_ctrl_supported(int ctrl_no)
{
	if (ctrl_no >= 0 && ctrl_no <= GPIO4)
		return SUCCESS;

	return CTRL_NOT_SUPPORTED;
}

int lib_modem_gpio_init(enum gpio_interface gpio, uint8_t pin,
		enum gpio_type gpio_type)
{
	uint32_t gp_dir, gp_ibe, gp_odr;
	struct gpio_regs *gpio_regs = (struct gpio_regs *)
		(virt_addr_gpio + (gpio * GPIO_REG_OFFSET));

	if (lib_is_ctrl_supported(gpio))
		return CTRL_NOT_SUPPORTED;

	if (lib_is_pin_supported(pin))
		return PIN_NOT_SUPPORTED;

	if (gpio_type == GPIO_INPUT)
	{
		gp_dir = gpio_regs->gp_dir;
		gp_ibe = gpio_regs->gp_ibe;
		gpio_regs->gp_dir = gp_dir & ~(GPIO_BITS(pin));
		gpio_regs->gp_ibe = gp_ibe | (GPIO_BITS(pin));
	} else if (gpio_type == GPIO_OUTPUT) {
		gp_dir = gpio_regs->gp_dir;
		gp_ibe = gpio_regs->gp_ibe;
		gpio_regs->gp_dir = gp_dir | (GPIO_BITS(pin));
		gpio_regs->gp_ibe = gp_ibe | (GPIO_BITS(pin));
	} else {
		gp_odr = gpio_regs->gp_odr;
		gpio_regs->gp_odr = gp_odr | (GPIO_BITS(pin));
	}

	return SUCCESS;
}

int lib_modem_gpio_setdata(enum gpio_interface gpio,
		uint8_t pin, uint32_t val)
{
	uint32_t gp_dir, gp_dat;
	struct gpio_regs *gpio_regs = (struct gpio_regs *)
		(virt_addr_gpio + (gpio * GPIO_REG_OFFSET));

	if (lib_is_ctrl_supported(gpio))
		return CTRL_NOT_SUPPORTED;

	if (lib_is_pin_supported(pin))
		return PIN_NOT_SUPPORTED;

	gp_dir = gpio_regs->gp_dir;

	if (gp_dir & GPIO_BITS(pin))
	{
		if (val)
		{
			gp_dat = gpio_regs->gp_dat;
			gpio_regs->gp_dat = gp_dat | (GPIO_BITS(pin));
		} else {
			gp_dat = gpio_regs->gp_dat;
			gpio_regs->gp_dat = gp_dat & ~(GPIO_BITS(pin));
		}
	} else {
		return PIN_TYPE_NOT_SET;
	}

	return SUCCESS;
}

int lib_modem_gpio_getdata(enum gpio_interface gpio, uint32_t *gp_dat)
{
	struct gpio_regs *gpio_regs = (struct gpio_regs *)
		(virt_addr_gpio + (gpio * GPIO_REG_OFFSET));

	if (lib_is_ctrl_supported(gpio))
		return CTRL_NOT_SUPPORTED;

	*gp_dat = gpio_regs->gp_dat;

	return SUCCESS;
}

int lib_close_modem_gpio(void)
{
	if (munmap(map_base_gpio, ccsr_size) == -1)
		FATAL;

	close(gpio_devmem_fd);

	return 0;
}
