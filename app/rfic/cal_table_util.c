/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @ cal_table_util
 *
 * Copyright 2017-2022 NXP
 */

#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define PAGE_SIZE	4096
#define FILE_PATH_LEN	150


int read_cal_table(void *virt, char *xcvr, char *path);
int write_cal_table(void *virt, char *xcvr, char *path);

unsigned long long addr;
unsigned long size;

int main(int argc, char *argv[])
{
	int fd, cnt, i, ret = 0;
	char buf[FILE_PATH_LEN+1];
	const char ch1 = '[';
	char *str1, *str2;
	void *map_base;

	/* Validate the arguments */
	if (argc < 5) {
		printf("ERROR: Incorrect Parameter\n");
		exit(-1);
	}

	if (strlen(argv[1]) > 4) {
		printf("ERROR: xcvr name is too long\n");
		exit(-1);
	}

	if (strlen(argv[3]) > 136) {
		printf("ERROR: File path is too long\n");
		exit(-1);
	}

	if (strlen(argv[4]) > 128) {
		printf("ERROR: Sysfs file path is too long\n");
		exit(-1);
	}

	/* Get the calibration table pointer and size */
	sprintf(buf, "%srhbsysfs/rfic_cal_ptr", argv[4]);
	fd = open((const char *)buf, O_RDWR);
	if (-1 == fd) {
		printf("ERROR: Sysfs open failed.\n");
		exit(-1);
	}

	write(fd, argv[1], 4);
	cnt = read(fd, buf, FILE_PATH_LEN);
	if (0 != errno || 0 > cnt) {
		printf("ERROR: Sysfs read failed errno [%d], cnt [%d]\n",
		       errno, cnt);
		close(fd);
		exit(-1);
	}
	close(fd);

	/* Parsing the buffer received from sysfs and update the
	 * calibration table pointers and size */
	buf[cnt] = '\0';
	str2 = buf;
	for (i = 0; i < 2; i++) {
		str1 = strchr(str2, ch1);
		if (str1 == NULL)
			break;

		if (i % 2 == 0)
			addr = strtoull(++str1, &str2, 10);
		else
			size = strtoul(++str1, &str2, 10);
	}

	/* Map the kernel physical address to virtual address */
	size = ((size/PAGE_SIZE)+1)*PAGE_SIZE;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (-1 == fd) {
		printf("ERROR: /dev/mem open failed.\n");
		exit(-1);
	}

	map_base = mmap(NULL, size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd,
			addr & ~(unsigned long long)(PAGE_SIZE-1));

	if (map_base == (void *) -1) {
		printf("ERROR: mmap failed, errno [%d]\n", errno);
		close(fd);
		exit(-1);
	}
	map_base += (addr & (unsigned long long)(PAGE_SIZE-1));

	/* Read or Write ? */
	if (*argv[2] == 'r')
		ret = read_cal_table(map_base, argv[1], argv[3]);
	else if (*argv[2] == 'w')
		ret = write_cal_table(map_base, argv[1], argv[3]);
	else
		printf("ERROR: Invalid second argument\n");

	/* Exit: Clean-up */
	close(fd);

	return ret;
}

int write_cal_table(void *virt, char *xcvr, char *path)
{
	char i, file_name[FILE_PATH_LEN], buf[PAGE_SIZE], cnt;
	int fd, rem_size, cur_size, no_of_pages, total_size, ret = 0;

	sprintf(file_name, "%scal_table_%s", path, xcvr);

	fd = open(file_name, O_RDWR | O_CREAT, 0740);
	if (-1 == fd) {
		printf("ERROR: file open failed errno [%d]\n", errno);
		exit(-1);
	}

	total_size = 0;

	no_of_pages = (size/PAGE_SIZE)+1;
	rem_size = size;
	total_size = 0;
	for (i = 0; i < no_of_pages; i++) {
		if (rem_size > PAGE_SIZE) {
			rem_size -= PAGE_SIZE;
			cur_size = PAGE_SIZE;
		} else {
			cur_size = rem_size;
			rem_size = 0;
		}

		cnt = read(fd, buf, cur_size);
		if (errno != 0) {
			printf("ERROR: sysfs read failed[%d], cnt [%d]\n",
			       errno, cnt);
			close(fd);
			exit(-1);
		}
		memcpy((((unsigned char *)virt)+total_size), buf,
		       cur_size);
		total_size += cur_size;
	}
	close(fd);
	return ret;
}

int read_cal_table(void *virt, char *xcvr, char *path)
{
	char file_name[FILE_PATH_LEN];
	int fd, ret = 0;

	sprintf(file_name, "%scal_table_%s", path, xcvr);

	fd = open(file_name, O_RDWR | O_CREAT, 0740);
	if (-1 == fd) {
		printf("ERROR: file open failed errno [%d]\n", errno);
		exit(-1);
	}

	write(fd, (unsigned char *)virt, size);
	if (errno != 0) {
		printf("ERROR: file write failed errno [%d]\n", errno);
		close(fd);
		exit(-1);
	}
	close(fd);
	return ret;
}
