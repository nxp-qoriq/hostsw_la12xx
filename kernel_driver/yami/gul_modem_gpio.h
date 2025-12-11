/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2021-2022 NXP
 */

#ifndef __GUL_MODEM_GPIO_H__
#define __GUL_MODEM_GPIO_H__

#include <linux/types.h>
#define MODEM_GPIO_BASE_ADDR_OFFSET	(0x1130000)
#define MODEM_GPIO_REG_OFFSET		(0x4000)
#define GPIO_PIN_MAX	   (31)
#define GPIO_PIN(x)          ((uint8_t)  (x))
#define BITS(x)              (( uint32_t) (1 << (GPIO_PIN_MAX - x)))

/* Status */
#define GPIO_TYPE_NOT_SET	-1
#define GPIO_SUCCESS		0

enum modem_gpio_type {
	GPIO_INPUT = 0,    /* GPIO pin type input */
	GPIO_OUTPUT,   /* GPIO pin type output */
	GPIO_OPENDRAIN /* GPIO pin type open drain */
};

enum modem_gpio_interface {
	/* 16kb offset */
	GPIO1 = 0,
	GPIO2,
	GPIO3,
	GPIO4,
};

struct modem_gpio_regs {
	uint32_t gp_dir;
	uint32_t gp_odr;
	uint32_t gp_dat;
	uint32_t gp_ier;
	uint32_t gp_imr;
	uint32_t gp_icr;
	uint32_t gp_ibe;
};

/**
 * Initialize the GPIO pins with direction
 *
 * @param[in] gul_dev
 *     Geul device
 * @param[in] gpio
 *     GPIO interface (e.g. GPIO1,GPIO2,GPIO3,GPIO4)
 * @param[in] pin
 *     GPIO pin number to control
 * @param[in]  gpio_type
 *     GPIO pin direction
 *
 * @return
 *  - On success, returns SUCCESS
 *  - On failure, returns FAILURE
 */
int modem_gpio_init(struct gul_dev *gul_dev,
		enum modem_gpio_interface gpio, uint8_t pin,
		enum modem_gpio_type gpio_type);
/**
 * Set data of particular GPIO pin
 *
 * @param[in] gul_dev
 *     Geul device
 * @param[in] gpio
 *     GPIO interface (e.g. GPIO1,GPIO2,GPIO3,GPIO4)
 * @param[in] pin
 *     GPIO pin number to control
 * @param[in]  val
 *     GPIO pin value to write
 *
 * @return
 *  - On success, returns SUCCESS
 *  - On failure, returns FAILURE
 */
int modem_gpio_setdata(struct gul_dev *gul_dev,
		enum modem_gpio_interface gpio, uint8_t pin, uint32_t val);
/**
 * Read GPIO data Register
 *
 * @param[in] gul_dev
 *     Geul device
 * @param[in] gpio
 *     GPIO interface (e.g. GPIO1,GPIO2,GPIO3,GPIO4)
 * @param[in] gp_dat
 *     address of variable which contains data reg value
 *
 * @return
 *  - On success, returns SUCCESS
 *  - On failure, returns FAILURE
 */
int modem_gpio_getdata(struct gul_dev *gul_dev,
		enum modem_gpio_interface gpio, uint32_t *gp_dat);

/**
 * How to use GPIO APIs:
 * 1. Initialize any GPIO pin of any GPIO controller using below API. It sets the direction of GPIO pin.
 * int modem_gpio_init(struct gul_dev *gul_dev,
 *                enum modem_gpio_interface gpio, uint8_t pin,
 *                enum modem_gpio_type gpio_type);
 * e.g. modem_gpio_init(gul_dev, GPIO2, 3, GPIO_OUTPUT);
 *
 * 2. Drive any value to GPIO output pin using below API.
 * int modem_gpio_setdata(struct gul_dev *gul_dev,
 *                enum modem_gpio_interface gpio, uint8_t pin, uint32_t val);
 * e.g. modem_gpio_setdata(gul_dev, GPIO2, 3, 1);
 *
 * 3. Read any GPIO data Register value using below API:
 * int modem_gpio_getdata(struct gul_dev *gul_dev,
 *                enum modem_gpio_interface gpio, uint32_t *gp_dat);
 * e.g. modem_gpio_getdata(gul_dev, GPIO2, &gpdat);
 */
#endif
