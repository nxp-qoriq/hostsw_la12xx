/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright 2020-2022 NXP
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdint.h>
#include <mpic_gb_timestamp.h>

int main(int argc, char **argv)
{
	int open, close;
	uint64_t prev_count = 0, current_count = 0;

	open = open_mpic_gbtimer();
	if (open) {
		printf("%s Failed to mmap MPIC GB Timer\n", __func__);
		return -1;
	}

	while (1)
	{
		printf(" Current Count is %lu\n", get_mpic_current_count());
		usleep(1000000);
	}

	prev_count = get_mpic_current_count();
	usleep(100);
	current_count = get_mpic_current_count();
	printf("usleep(100) is  %lu \r\n", (current_count - prev_count));

	prev_count = get_mpic_current_count();
	usleep(1000);
	current_count = get_mpic_current_count();
	printf("usleep(1000) is %lu \r\n", (current_count - prev_count));

	prev_count = get_mpic_current_count();
	usleep(10000);
	current_count = get_mpic_current_count();
	printf("usleep(10000) is %lu \r\n", (current_count - prev_count));

	prev_count = get_mpic_current_count();
	usleep(100000);
	current_count = get_mpic_current_count();
	printf("usleep(100000) is %lu \r\n", (current_count - prev_count));

	prev_count = get_mpic_current_count();
	usleep(1000000);
	current_count = get_mpic_current_count();
	printf("usleep(1000000) is %lu \r\n", (current_count - prev_count));

	close = close_mpic_gbtimer();
	if (close) {
		printf("%s Failed to unmap MPIC GB Timer\n", __func__);
		return -1;
	}
	return 0;
}
