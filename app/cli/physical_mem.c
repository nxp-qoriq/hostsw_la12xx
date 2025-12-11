/* SPDX-License-Identifier: BSD-3-Clause /
 * Copyright 2022 NXP
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <ctype.h>
#include "physical_mem.h"

#define PAGE_SIZE 4096UL
#define PAGE_MASK (PAGE_SIZE - 1)
#define PRINT_MAP_INFO 0

int dev_mem_fd;
static int dev_mem_ref_ctr;
void open_dev_mem(void)
{
	if (dev_mem_ref_ctr == 0) {
		/* Open /dev/mem driver - requires root permission */
		dev_mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
		if (dev_mem_fd == -1) {
			printf("Error: /dev/mem could not be opened.\n");
			perror("open");
			exit(1);
		}
	}
	dev_mem_ref_ctr++;
}

void close_dev_mem(void)
{
	dev_mem_ref_ctr--;
	if (dev_mem_ref_ctr == 0)
		close(dev_mem_fd);
}

void map_physical_region(MEM_MAP_T *map,
						 unsigned long physical_addr,
						 unsigned long size)
{
	unsigned long end_addr;
	unsigned long start_page, end_page;

	open_dev_mem();
	/* Calculate page boundaries */
	end_addr = physical_addr + size - 1;
	start_page = physical_addr & ~PAGE_MASK;
	end_page = end_addr & ~PAGE_MASK;
	map->map_size = end_page - start_page + PAGE_SIZE;

	/* Map page(s) */
	map->map_base = mmap(0, map->map_size,
						 PROT_READ | PROT_WRITE,
						 MAP_SHARED,
						 dev_mem_fd, start_page);
	if (map->map_base == (void *) -1) {
		printf("Error: Memory map failed.\n");
		perror("mmap");
		close(dev_mem_fd);
		exit(1);
	} else {
		map->phy_addr  = (void *)physical_addr;
		map->virt_addr = map->map_base + (physical_addr & PAGE_MASK);
	}
}

void unmap_physical_region(MEM_MAP_T *map)
{
	if (munmap(map->map_base, map->map_size) == -1) {
		printf("Error: Memory unmap failed.\n");
		exit(1);
	}
	close_dev_mem();
}
