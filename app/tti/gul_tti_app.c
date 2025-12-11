/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2019-2023 NXP
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <string.h>
#include <sched.h>
#include<gul_tti_ioctl.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <fcntl.h>

typedef struct tti_stats {
	suseconds_t micro_sec;
	uint32_t tti_id;
	uint64_t counter_val;
} tti_stats_t;

void *ccsr_info;

#define TBGEN1_BASE_ADDR			0x1120000
#define TBGEN2_BASE_ADDR			0x1124000
#define TBGEN_MASTER_COUNTER_HIGH_OFFSET	0x6F0
#define TBGEN_MASTER_COUNTER_LOW		0x4
#define TICK_TO_MHz_DIV				1000000

static int modem_id;
static int tti_id;
static int tti_count;
static int tti_event_flag;

#ifndef MAX_MODEM
#define MAX_MODEM 4
#endif
/* Every modem can have one to four TTI IDs */
#define MAX_TTI_ID		4
#define MAX_COUNT		10
#define	GUL_RFIC_SCHED_PRIORITY	99
#define MAX_EVENTS 1

void print_usage_message(void)
{
	printf("Usage : ./app_name [options] modem_id tti_id tti_count_max\n");
	printf("\n");
	printf("options:\n");
	printf("\n-e : eventMode\n");
	printf("\n");
	printf("mandatory:\n");
	printf("\tmodem_id         -- decimal |	0..%d\n", MAX_MODEM-1);
	printf("\ttti_id           -- decimal |	0..%d\n", MAX_TTI_ID-1);
	printf("\ttti_count_max	 -- decimal |	0..%d\n", MAX_COUNT-1);
	fflush(stdout);

	exit(EXIT_SUCCESS);
}

void validate_cli_args(int argc, char *argv[])
{

	if (argc <=1)
		print_usage_message();

	if ((strcmp(argv[1], "-h") == 0)  ||
			(strcmp(argv[1], "-H") == 0))
		print_usage_message();

	/* Check Total Argument count */
	if (argc >= 4) {
		modem_id	= atoi(argv[1]);
		tti_id		= atoi(argv[2]);
		tti_count	= atoi(argv[3]);
		if ((argc > 4) && ((strcmp(argv[4], "-e") == 0)  ||
			(strcmp(argv[4], "-E") == 0))) {
			tti_event_flag = 1;
		} else {
			tti_event_flag = 0;
		}
	} else {
		print_usage_message();
	}
}

void *map_gul_tbgen_reg(int modem_id, int tti_id)
{
	ccsr_info = get_ccsr_addr(modem_id);
	if (ccsr_info == NULL)
		return NULL;

	if (tti_id == 0) {
		return (void *)(ccsr_info + TBGEN1_BASE_ADDR +
				TBGEN_MASTER_COUNTER_HIGH_OFFSET);
	} else
		return (void *)(ccsr_info + TBGEN2_BASE_ADDR +
				TBGEN_MASTER_COUNTER_HIGH_OFFSET);
}

struct sched_param param = { .sched_priority = GUL_RFIC_SCHED_PRIORITY };
int main(int argc, char *argv[])
{
	struct tti tti_t;
	ssize_t bytes_read;
	struct timeval tv;
	uint64_t eftd_ctr;
	int ret = 0;
	int index = 0;
	tti_stats_t *stats_arr;
	void *gul_tbgen_master_counter = NULL;
	/* Validate CLI Args */
	validate_cli_args(argc, argv);
	/* Raise app priority to RT */
	/*sched_setscheduler(current, SCHED_FIFO, &param);*/
	sched_setscheduler(0, SCHED_FIFO, &param);

	/* Register modem=0 and TTI=0 */
	stats_arr = (tti_stats_t *)malloc((tti_count + 1) *
			sizeof(tti_stats_t));
	if (!stats_arr) {
		printf("OOM while allocating stats array...\n");
		goto error;
	}
	memset(stats_arr, 0, (tti_count + 1) * sizeof(tti_stats_t));

	/* Register Modem & TTI as per command line params supplied */
	tti_t.ttid = tti_id;
	tti_t.tti_eventfd = -1;
	ret = modem_tti_register(&tti_t, modem_id, tti_event_flag);
	if (ret < 0) {
		printf("%s failed...\n", __func__);
		goto error;
	}
	gul_tbgen_master_counter = map_gul_tbgen_reg(modem_id, tti_id);
	if (gul_tbgen_master_counter == NULL) {
		printf("failed to get master counter info\n");
		goto error;
	}
	if (tti_event_flag == 1) {
		struct epoll_event ev, events[MAX_EVENTS];
		int epollfd, nfds;

		epollfd = epoll_create1(0);
		if (epollfd < 0) {
			printf("epoll create failed !\n");
			goto err;
		}

		ev.events = EPOLLIN;
		ev.data.fd = tti_t.tti_eventfd;
		if (epoll_ctl(epollfd, EPOLL_CTL_ADD,
				tti_t.tti_eventfd, &ev) == -1) {
			printf("Failed to create tti_eventfd\n");
			close(epollfd);
			goto err;
		}
		for (index = 0; index < (tti_count + 1); index++) {
			printf("Waiting for event\n");
			nfds = epoll_wait(epollfd, events,
					MAX_EVENTS, -1);
			if (nfds == 1) {
				gettimeofday(&tv, NULL);
				stats_arr[index].micro_sec = tv.tv_usec;
				stats_arr[index].tti_id = tti_t.ttid;
				stats_arr[index].counter_val =
					*((uint32_t *)gul_tbgen_master_counter);
				stats_arr[index].counter_val <<= 32;
				stats_arr[index].counter_val  |=
				((*((uint32_t *)(gul_tbgen_master_counter +
						 TBGEN_MASTER_COUNTER_LOW))));
				fflush(stdout);
				bytes_read = read(tti_t.tti_eventfd,
						&eftd_ctr,
						sizeof(uint64_t));
				if (bytes_read != sizeof(uint64_t)) {
					printf(
						"Error in reset counter : %ld\n"
						, bytes_read);
				}
			}
		}
		close(epollfd);
	} else {
		for (index = 0; index < (tti_count + 1); index++) {
			bytes_read = read(tti_t.dev_tti_handle, &eftd_ctr,
					sizeof(uint64_t));
			if (bytes_read != sizeof(uint64_t)) {
				printf("Read error. Exiting...\n");
				goto err;
			} else {
				/* Extract timestamp in usecs and populate the
				 *  data structure */
				gettimeofday(&tv, NULL);
				stats_arr[index].micro_sec = tv.tv_usec;
				stats_arr[index].tti_id = tti_t.ttid;
				stats_arr[index].counter_val =
					*((uint32_t *)gul_tbgen_master_counter);
				stats_arr[index].counter_val <<= 32;
				stats_arr[index].counter_val  |=
				((*((uint32_t *)(gul_tbgen_master_counter +
						 TBGEN_MASTER_COUNTER_LOW))));
				fflush(stdout);
			}
		}
	}

	for (index = 0; index < tti_count; index++)
		printf("\nTTI_ID=%d \tTIMESTAMP=%ld usec\t tti_count=%d\t"
			"interrupt_freq: %d\t master_counter_diff: %u\n",
				stats_arr[index].tti_id,
				stats_arr[index].micro_sec,
				index, (int)(stats_arr[index + 1].micro_sec -
					stats_arr[index].micro_sec),
				(uint32_t)(stats_arr[index + 1].counter_val -
					stats_arr[index].counter_val));

	unmap_tbgen_addr(modem_id);
	if (get_tbgen_freq_info(modem_id))
		printf("failed to get tbgen info\n");

	/* Deregister TTI */
	modem_tti_deregister(&tti_t);
	free(stats_arr);
	exit(EXIT_SUCCESS);
	return 0;
err:
	modem_tti_deregister(&tti_t);
error:
	if (stats_arr) {
		free(stats_arr);
		stats_arr = NULL;
	}
	exit(EXIT_FAILURE);
}
