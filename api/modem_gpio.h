/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)*/
/*
 * Copyright 2021-2023 NXP
 */
#ifndef __MODEM_GPIO__
#define __MODEM_GPIO__

#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
				__LINE__, __FILE__, errno, strerror(errno)); return -1; } while (0)

#define MAX_PIN			(31)
#define GPIO_BITS(x)	((uint32_t) (1 << (MAX_PIN - x)))

#define PIN_NOT_SUPPORTED	-1
#define PIN_TYPE_NOT_SET	-2
#define CTRL_NOT_SUPPORTED	-3
#define SUCCESS 0

/*
 * GPIO_BASE_ADDR = PCIe BAR0 address + CCSR Base Address of Modem + GPIO Controller 1 address
 */
#define GPIO_BASE_ADDR		0x1130000
#define GPIO_REG_OFFSET		(0x4000)

enum gpio_type {
	GPIO_INPUT,    /* GPIO pin type input */
	GPIO_OUTPUT,   /* GPIO pin type output */
	GPIO_OPENDRAIN /* GPIO pin type open drain */
};

enum gpio_interface {
	/* 16kb offset */
	GPIO1 = 0,
	GPIO2,
	GPIO3,
	GPIO4,
};

struct gpio_regs {
	uint32_t gp_dir;
	uint32_t gp_odr;
	uint32_t gp_dat;
	uint32_t gp_ier;
	uint32_t gp_imr;
	uint32_t gp_icr;
	uint32_t gp_ibe;
};

/*
 * Open MODEM GPIO device node
 *
 * @param[in] modem_id
 *     modem_id (e.g. 0,1,2,3)
 * @return
 *	- On success return 0
 *	- On failure return negative error number
 */
int lib_open_modem_gpio(int modem_id);

/*
 * Close MODEM GPIO device node
 * @return
 *	- On success return 0
 *	- On failure return negative error number
 */

int lib_close_modem_gpio(void);

/**
 * Initialize the GPIO pins with direction
 *
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

int lib_modem_gpio_init(enum gpio_interface gpio, uint8_t pin,
		enum gpio_type gpio_type);
/**
 * Set data of particular GPIO pin
 *
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
int lib_modem_gpio_setdata(enum gpio_interface gpio,
		uint8_t pin, uint32_t val);
/**
 * Read GPIO data Register
 *
 * @param[in] gpio
 *     GPIO interface (e.g. GPIO1,GPIO2,GPIO3,GPIO4)
 * @param[in] gp_dat
 *     address of variable which contains data reg value
 *
 * @return
 *  - On success, returns SUCCESS
 *  - On failure, returns FAILURE
 */
int lib_modem_gpio_getdata(enum gpio_interface gpio, uint32_t *gp_dat);

/**
 * How to use GPIO APIs:
 * 1. User needs to call below API once to map MODEM GPIO address space.
 * After that User can use any 2,3,4 no. GPIO APIs.
 * int lib_open_modem_gpio(void);
 *
 * 2. Initialize any GPIO pin of any GPIO controller using below API. It sets the direction of GPIO pin.
 * int lib_modem_gpio_init(enum gpio_interface gpio, uint8_t pin,
 *		   enum gpio_type gpio_type);
 * e.g. lib_modem_gpio_init(GPIO2, 3, GPIO_OUTPUT);
 *
 * 3. Drive any value to GPIO output pin using below API.
 * int lib_modem_gpio_setdata(enum gpio_interface gpio, uint8_t pin,
 *		   uint32_t val);
 * e.g. lib_modem_gpio_setdata(GPIO2, 3, 1);
 *
 * 4. Read any GPIO data Register value using below API:
 * int lib_modem_gpio_getdata(enum gpio_interface gpio, uint32_t *gp_dat)
 * e.g. lib_modem_gpio_getdata(GPIO2, &gp_data);
 *
 * 5. After User used GPIO APIs, User needs to close gpio using below API:
 * int lib_close_modem_gpio(void);
 */

#endif /* __MODEM_GPIO__ */
