/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2020-2023 NXP
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <string.h>
#include <sched.h>
#include <gul_wdog.h>


/* Every modem can have one watchdog ID */
#define MODEM_WDOG_INSTANCE    0
#define MAX_EVENTS 1

#define GUL_WDOG_SCHED_PRIORITY 98

static int modem_id;
static int wdog_event_flag;

void print_usage_message(void)
{
	printf("Usage : ./gul_wdog_testapp [options] modem_id\n");
	printf("\n");
	printf("mandatory:\n");
	printf("\toptions: -f (or) -w\n");
	printf("\t\t-f : Force Reset\n");
	printf("\n");
	printf("\t\t-w : Watchdog Reset\n");
	printf("\n");
	printf("\tmodem_id         -- decimal | 0..(%d)\n", MAX_MODEM-1);
	fflush(stdout);

	exit(EXIT_SUCCESS);
}

void validate_cli_args(int argc, char *argv[])
{
	if (argc < 2)
		print_usage_message();

	if ((strcmp(argv[1], "-h") == 0)  ||
			(strcmp(argv[1], "-H") == 0))
		print_usage_message();

	/* Check Total Argument count */
	if (argc == 3) {
		modem_id  = atoi(argv[2]);

		if ((strcmp(argv[1], "-f") == 0)  ||
				(strcmp(argv[1], "-F") == 0)) {
			wdog_event_flag = 1;
		}

		else if ((strcmp(argv[1], "-w") == 0)  ||
				(strcmp(argv[1], "-W") == 0)) {
			wdog_event_flag = 0;
		}

		else {
			print_usage_message();
		}

	} else {
		print_usage_message();
	}

}

struct sched_param param = { .sched_priority = GUL_WDOG_SCHED_PRIORITY };
int main(int argc, char *argv[])
{
	struct wdog wdog_t;
	ssize_t bytes_read;
	uint64_t eftd_ctr;
	int ret = 0;

	/* Validate CLI Args */
	validate_cli_args(argc, argv);
	/* Raise app priority to RT */
	/*sched_setscheduler(current, SCHED_FIFO, &param);*/
	sched_setscheduler(0, SCHED_FIFO, &param);

	/* Register Modem & Watchdog as per command line params supplied */
	ret = libwdog_register(&wdog_t, modem_id);
	if (ret < 0) {
		printf("%s failed...\n", __func__);
		goto error;
	}

	if (wdog_event_flag != 0)
	{
		ret = libwdog_reinit_modem(&wdog_t, 300);
		if (ret < 0) {
			printf("modem reinit failed\n");
		}
	}

	else {
		if (wdog_t.wdog_eventfd < 0)
			bytes_read = libwdog_readwait(wdog_t.dev_wdog_handle,
					&eftd_ctr, sizeof(uint64_t));
		else {
			struct epoll_event ev, events[MAX_EVENTS];
			int epollfd, nfds;

			epollfd = epoll_create1(0);

			if (!(epollfd == -1)) {
				ev.events = EPOLLIN;
				ev.data.fd = wdog_t.wdog_eventfd;
				if (epoll_ctl(epollfd, EPOLL_CTL_ADD,
					wdog_t.wdog_eventfd, &ev) == -1) {
					printf("failed to add wdog_eventfd\n");
					close(epollfd);
				} else {
					printf("Waiting for event\n");
					nfds = epoll_wait(epollfd, events,
							MAX_EVENTS, -1);

					close(epollfd);
					if (nfds == 1)
						printf("epoll_wait: event received from wdog\n");
					else
						goto done;
				}
			} else
				printf("epoll_create1 failed\n");
			/*
			 * To get the watchdog modem reset reason from driver.
			 * Modem reset reasons can be drived from below enum
			 * defined in uapi/gul_wdog_ioctl.h.
			 * enum wdog_modem_status {
			 * WDOG_MODEM_NOT_READY = 0,
			 * WDOG_MODEM_READY,
			 * WDOG_MODEM_HSDCS_ERR,
			 * WDOG_MODEM_WARMUP_RESET
			 * };
			 */
			libwdog_get_modem_status(&wdog_t);
			switch (wdog_t.wdog_modem_status) {
			case 0:
				printf("WDOG_MODEM_NOT_READY\n");
				break;
			case 2:
				printf("WDOG_MODEM_HSDCS_ERR\n");
				break;
			case 3:
				printf("WDOG_MODEM_WARMUP_RESET\n");
				break;
			default:
				printf("Invalid modem reset reason\n");
				break;
			}

			printf("Read eventfd to avoid stale data in buffer\n");
			bytes_read = libwdog_readwait(wdog_t.wdog_eventfd,
					&eftd_ctr, sizeof(uint64_t));
			printf(" Waiting on eventfd... Done\n");
		}

		if (bytes_read != sizeof(uint64_t)) {
			printf("Read error. Exiting...\n");
			goto done;
		} else {
			ret = libwdog_reinit_modem(&wdog_t, 300);
			if (ret < 0) {
				printf("WDOG Event: modem reinit failed\n");
			}
		}
	}

done:
	/* Deregister Watchdog */
	libwdog_deregister(&wdog_t);
	exit(EXIT_SUCCESS);
	return 0;

error:
	exit(EXIT_FAILURE);
}
