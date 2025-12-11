/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2021-2022 NXP
 */
#include <linux/types.h>
#include <asm/io.h>
#include "gul_base.h"
#include "gul_modem_gpio.h"

int modem_gpio_init(struct gul_dev *gul_dev,
		enum modem_gpio_interface gpio, uint8_t pin,
		enum modem_gpio_type gpio_type)
{
	uint32_t gp_dir, gp_ibe, gp_odr;
	struct gul_mem_region_info *ccsr_mem =
		&(gul_dev->mem_regions[GUL_MEM_REGION_CCSR]);
	struct modem_gpio_regs *modem_gpio_regs =
		(struct modem_gpio_regs *)(ccsr_mem->vaddr +
				MODEM_GPIO_BASE_ADDR_OFFSET + (gpio * MODEM_GPIO_REG_OFFSET));

	pin = GPIO_PIN(pin);

	if (gpio_type == GPIO_INPUT) {
		gp_dir = ioread32(&(modem_gpio_regs->gp_dir));
		gp_ibe = ioread32(&(modem_gpio_regs->gp_ibe));
		iowrite32(gp_dir & ~(BITS(pin)), &(modem_gpio_regs->gp_dir));
		iowrite32(gp_ibe | (BITS(pin)), &(modem_gpio_regs->gp_ibe));
	} else if (gpio_type == GPIO_OUTPUT) {
		gp_dir = ioread32(&(modem_gpio_regs->gp_dir));
		gp_ibe = ioread32(&(modem_gpio_regs->gp_ibe));
		iowrite32(gp_dir | (BITS(pin)), &(modem_gpio_regs->gp_dir));
		iowrite32(gp_ibe | (BITS(pin)), &(modem_gpio_regs->gp_ibe));
	} else {
		gp_odr = ioread32(&(modem_gpio_regs->gp_odr));
		iowrite32(gp_odr | (BITS(pin)), &(modem_gpio_regs->gp_odr));
	}

	return GPIO_SUCCESS;
}

int modem_gpio_setdata(struct gul_dev *gul_dev,
		enum modem_gpio_interface gpio, uint8_t pin, uint32_t val)
{
	uint32_t gp_dir, gp_dat;
	struct gul_mem_region_info *ccsr_mem =
		&(gul_dev->mem_regions[GUL_MEM_REGION_CCSR]);
	struct modem_gpio_regs *modem_gpio_regs =
		(struct modem_gpio_regs *)(ccsr_mem->vaddr +
				MODEM_GPIO_BASE_ADDR_OFFSET + (gpio * MODEM_GPIO_REG_OFFSET));

	pin = GPIO_PIN(pin);

	gp_dir = ioread32(&(modem_gpio_regs->gp_dir));

	if (gp_dir & BITS(pin)) {
		if (val) {
			gp_dat = ioread32(&(modem_gpio_regs->gp_dat));
			iowrite32(gp_dat | (BITS(pin)), &(modem_gpio_regs->gp_dat));
		} else {
			gp_dat = ioread32(&(modem_gpio_regs->gp_dat));
			iowrite32(gp_dat & ~(BITS(pin)), &(modem_gpio_regs->gp_dat));
		}
	} else {
		pr_err("%s: pin is input mode\n\r", __func__);
		return GPIO_TYPE_NOT_SET;
	}

	return GPIO_SUCCESS;
}

int modem_gpio_getdata(struct gul_dev *gul_dev,
		enum modem_gpio_interface gpio, uint32_t *gp_dat)
{
	struct gul_mem_region_info *ccsr_mem =
		&(gul_dev->mem_regions[GUL_MEM_REGION_CCSR]);
	struct modem_gpio_regs *modem_gpio_regs =
		(struct modem_gpio_regs *)(ccsr_mem->vaddr +
				MODEM_GPIO_BASE_ADDR_OFFSET + (gpio * MODEM_GPIO_REG_OFFSET));

	*gp_dat = ioread32(&(modem_gpio_regs->gp_dat));

	return GPIO_SUCCESS;
}
