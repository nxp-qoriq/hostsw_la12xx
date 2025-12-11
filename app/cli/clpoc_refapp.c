/* SPDX-License-Identifier: BSD-3-Clause /
 * Copyright 2022-2023 NXP
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <string.h>
#include <sched.h>
#include <pthread.h>
#include <signal.h>
#include "physical_mem.h"
#include <gul_cli_api.h>

#ifndef MAX_MODEM
#define MAX_MODEM 4
#endif
#define MAX_EVENTS   10
#define EPOLL_SIZE   256

#ifdef DEBUG_REFAPP
#define pr_debug(...) printf(__VA_ARGS__)
#else
#define pr_debug(...)
#endif

volatile int keep_running = 1;
struct cli refapp_t = {0};

typedef struct {
	unsigned int modem_id;
} run_opt_t;

run_opt_t run_options;

void print_help(char *argv0)
{
	printf("Build date/time: %s/%s\n", __DATE__, __TIME__);
	printf("Usage guide:\n");
	printf("Options: -m\n");
	printf("Examples:\n");
	printf("\t%s -m <modem_id>\n", argv0);
}

void parse_arguments(int argc, char *argv[], run_opt_t *ropt)
{
	int opt;

	memset(ropt, 0, sizeof(run_opt_t));

	while ((opt = getopt(argc, argv, ":m:h")) != -1) {
		switch (opt) {
		case 'm':
			ropt->modem_id = strtoul(optarg, NULL, 0);
			break;
		case ':':
			printf("option requires a value!\n");
			exit(-1);
		case '?':
		case 'h':
		default:
			print_help(argv[0]);
			exit(0);
		}
	}

	if (ropt->modem_id >= MAX_MODEM) {
		printf("Modem should be from 0..%d\n", MAX_MODEM);
		exit(0);
	}
}

void sigint_handler(int sig)
{
	keep_running = 0;
}


/* CL-DPD placeholder */
void do_training(uint32_t msg_id, uint32_t data1,
				 uint32_t data2, uint32_t data3)
{
	/* avoid warnings */
	(void)msg_id;
	(void)data1;
	(void)data2;
	(void)data3;

	system("./dpd_training_script.sh");
}

int main(int argc, char *argv[])
{
	struct sigaction act = {
		.sa_handler = sigint_handler,
		.sa_flags = 0
	};

	sigemptyset(&act.sa_mask);

	sigaction(SIGINT, &act, NULL);

	parse_arguments(argc, argv, &run_options);

	if (libcli_register(&refapp_t, run_options.modem_id, 0) < 0) {
		printf("%s Registration failed\n", __func__);
		goto error;
	}

	printf("Waiting events from Geul...\n");

	while (keep_running) {
		if (libcli_blockforEvent_read(refapp_t.handle,
				&refapp_t,
				sizeof(struct cli_mbox_u)) < 0)
			break;

		/* CL-DPD training placeholder */
		do_training(refapp_t.mbox.msg_id,
					refapp_t.mbox.data,
					refapp_t.mbox.data2,
					refapp_t.mbox.data3);
	}

	libcli_deregister(&refapp_t);
	exit(EXIT_SUCCESS);

error:
	exit(EXIT_FAILURE);
}
