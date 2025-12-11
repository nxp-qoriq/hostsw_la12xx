/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright 2021-2023 NXP
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <modem_gpio.h>

#define MAX_GPIO_CTRL	4
#define MAX_GPIO_PIN	31
#define GPIO_INIT_API	1
#define GPIO_SET_DATA_API	2
#define GPIO_GET_DATA_API	3

int gpio_ctrl, gpio_dir, api_id;
uint8_t gpio_pin;
uint32_t gpio_val;

void print_usage_message(void)
{
	printf("Usage : ./app_name modem_id api_id gpio_ctrl_no <gpio_pin>");
	printf("<gpio_direction> <value>\n");
	printf("\n");
	printf("\t modem_id: -- decimal | 0,1,2,3\n");
	printf("\t api_id  -- decimal | 1,2,3\n");
	printf("\t 1 - GPIO_INIT_API ,2 - GPIO_SET_DATA_API, 3 - GPIO_GET_DATA_API\n");
	printf("\t Params for 1 ap_id: gpio_ctrl_no, gpio_pin, gpio_direction\n");
	printf("\t Params for 2 ap_id: gpio_ctrl_no, gpio_pin, value\n");
	printf("\t Params for 3 ap_id: gpio_ctrl_no\n");
	printf("\t gpio_ctrl_no	-- decimal | 1..%d\n", MAX_GPIO_CTRL);
	printf("\t gpio_pin	-- decimal | 0..%d\n", MAX_GPIO_PIN);
	printf("\t gpio_direction -- decimal | 0=input,1=output,2=opendrain\n");
	printf("\t value -- decimal | 0,1\n");
	fflush(stdout);

	exit(EXIT_SUCCESS);
}

void validate_cli_args(int argc, char *argv[])
{
	int modem_id = -1;
	if (argc >= 2) {
		if ((strcmp(argv[1], "-h") == 0)  || (strcmp(argv[1], "-H") == 0)) {
			print_usage_message();
		}
		modem_id  = atoi(argv[1]);
		if (modem_id < 0 && modem_id > 3)
			print_usage_message();

		api_id = atoi(argv[2]);
		switch (api_id) {
			case GPIO_INIT_API:
				if (argc == 6) {
					gpio_ctrl = atoi(argv[3]) - 1;
					gpio_pin = atoi(argv[4]);
					gpio_dir = atoi(argv[5]);
				} else {
					print_usage_message();
				}
				break;
			case GPIO_SET_DATA_API:
				if (argc == 6) {
					gpio_ctrl = atoi(argv[3]) - 1;
					gpio_pin = atoi(argv[4]);
					gpio_val = atoi(argv[5]);
				} else {
					print_usage_message();
				}
				break;
			case GPIO_GET_DATA_API:
				if (argc == 4)
					gpio_ctrl = atoi(argv[3]) - 1;
				else
					print_usage_message();
				break;
			default:
				print_usage_message();
		}
	} else {
		print_usage_message();
	}
}

int main(int argc, char *argv[])
{
	int open, close, ret = 0;
	uint32_t gpio_data_val;

	validate_cli_args(argc, argv);
	open = lib_open_modem_gpio(atoi(argv[1]));
	if (open) {
		printf("%s Failed to mmap MODEM GPIO\n", __func__);
		return -1;
	}

	switch (api_id) {
		case GPIO_INIT_API:
			ret = lib_modem_gpio_init(gpio_ctrl, gpio_pin, gpio_dir);
			break;
		case GPIO_SET_DATA_API:
			ret = lib_modem_gpio_setdata(gpio_ctrl, gpio_pin, gpio_val);
			break;
		case GPIO_GET_DATA_API:
			ret = lib_modem_gpio_getdata(gpio_ctrl, &gpio_data_val);
			if (ret == 0)
				printf("value=0x%x\n", gpio_data_val);
			break;
	}

	switch (ret) {
		case PIN_NOT_SUPPORTED:
			printf("GPIO pin is out of range\n");
			break;
		case PIN_TYPE_NOT_SET:
			printf("GPIO pin in not in output mode\n");
			break;
		case CTRL_NOT_SUPPORTED:
			printf("GPIO controller not valid\n");
			break;
	}

	close = lib_close_modem_gpio();
	if (close) {
		printf("%s Failed to unmap MODEM GPIO\n", __func__);
		return -1;
	}

	return 0;
}
