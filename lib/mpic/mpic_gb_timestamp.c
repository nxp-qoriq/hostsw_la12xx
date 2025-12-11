/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2019-2022 NXP
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
#include <mpic_gb_timestamp.h>

int devmem_fd = 0, align_check = 0;
void *virt_addr_gtccrb1, *virt_addr_gtccrb0, *map_base_gtccrb0;

int open_mpic_gbtimer(void)
{
	off_t gtccrb1, gtccrb0;

	gtccrb1 = MPIC_BASE_ADDR + MPIC_REG_GTCCRB1;
	gtccrb0 = MPIC_BASE_ADDR + MPIC_REG_GTCCRB0;

	if ((devmem_fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;

	/* Map MPIC Global Timer B Register upto one page */
	map_base_gtccrb0 = mmap(0, MAP_SIZE, PROT_READ, MAP_SHARED, devmem_fd, gtccrb0 & ~MAP_MASK);
	if (map_base_gtccrb0 == (void *) -1) FATAL;

	virt_addr_gtccrb0 = map_base_gtccrb0 + (gtccrb0 & MAP_MASK);
	virt_addr_gtccrb1 = map_base_gtccrb0 + (gtccrb1 & MAP_MASK);

	return 0;
}

uint64_t get_mpic_current_count(void)
{
	uint64_t timer_count;
	u_int32_t count_hi = 0, count_lo = 0;

	count_hi = *((unsigned int *) virt_addr_gtccrb1);
	count_hi = htobe32(count_hi) & 0x7FFFFFFF;

	count_lo = *((unsigned int *) virt_addr_gtccrb0);
	count_lo = htobe32(count_lo);

	timer_count = (0x7FFFFFFF - count_hi);
	timer_count = timer_count * 0xFFFFFFFF;
	timer_count = timer_count + (0xFFFFFFFF - count_lo);

	return MPIC_TIMER_COUNT_TO_US(timer_count);
}

int close_mpic_gbtimer(void)
{
	if (munmap(map_base_gtccrb0, MAP_SIZE) == -1) FATAL;
	close(devmem_fd);
	return 0;
}
