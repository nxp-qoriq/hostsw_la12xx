/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2022-2023 NXP
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sched.h>
#include <pthread.h>
#include <signal.h>
#include "physical_mem.h"
#include "gul_modinfo.h"
#include <gul_cli_api.h>

#ifndef MAX_MODEM
#define MAX_MODEM 4
#endif
#define MAX_EVENTS   10
#define EPOLL_SIZE   256

#ifdef DEBUG_CLI
#define pr_debug(...) printf(__VA_ARGS__)
#else
#define pr_debug(...)
#endif

volatile int keep_running = 1;
volatile int command_file = 0;

struct cli cli_t = {0};

#define MAX_CMD_LEN 128
#define MAX_CMD_FILE_LEN 128

#define CMDLINE_OPTIONS	"m:c:r:e:h:f"

typedef struct {
	unsigned int modem_id;
	unsigned int core_id;
	int mode;
	bool cmd_send;
	bool wait_events;
	char cmd_file_name[MAX_CMD_FILE_LEN];
	char cmd_line[MAX_CMD_LEN];
} run_opt_t;

run_opt_t run_options;

enum cmd_mode {
	SYNC_MODE = 0,
	ASYNC_MODE,
	WAIT_MODE,
};

void print_help(char *argv0)
{
	printf("Build date/time: %s/%s\n", __DATE__, __TIME__);
	printf("Usage guide:\n");
	printf("Options: -m:c:r:f:eh\n");
	printf("Examples:\n");
	printf("\t%s -m <modem_id> -r <core_id> -f <Command Script file> -c <l1c shell command>\n", argv0);
	printf("\tchoice -f with command script file is optional\n");
	printf("\t For async mode : %s -c <l1c shell command>\n", argv0);
	printf("\t For wait event in sync mode : %s -c  <l1c shell command> -e\n", argv0);
}

void parse_arguments(int argc, char *argv[], run_opt_t *ropt)
{
	int opt;
	memset(ropt, 0, sizeof(run_opt_t));

	while ((opt = getopt(argc, argv, ":m:r:c:f:eh")) != -1) {
		switch (opt) {
		case 'm':
			ropt->modem_id = strtoul(optarg, NULL, 0);
			break;
		case 'r':
			ropt->core_id = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			ropt->mode = ASYNC_MODE;
			ropt->cmd_send = 1;
			if(optarg != NULL)
				snprintf(ropt->cmd_line, MAX_CMD_LEN - 1, "%s", optarg);
			else {
				perror("No command file supplied\r\n");
				exit(-1);
			}
			
			break;
		case 'f':
			if (optarg != NULL)
			{
				strncpy(ropt->cmd_file_name, optarg, MAX_CMD_FILE_LEN - 1);
				printf("\r\ncmd file name %s\r\n",ropt->cmd_file_name);
				ropt->cmd_file_name[MAX_CMD_FILE_LEN - 1] = '\0';
				command_file = 1;
			}
			else
			{
				perror("No command file supplied\r\n");
				exit(-1);
			}
			break;
		case 'e':
			ropt->wait_events = 1;
			break;
		case ':':
			printf("option requires a value!\n");
			exit(-1);
		case '?':
			/* FALLTHROUGH */
		case 'h':
			/* FALLTHROUGH */
		default:
			print_help(argv[0]);
			exit(0);
		}
	}

	if (ropt->modem_id >= MAX_MODEM) {
		printf("Modem should be from 0..%d\n", (MAX_MODEM - 1));
		exit(0);
	}

	if (ropt->core_id >= GEUL_E200_CORE_REVB_NUM) {
		printf("Core should be from 0..%d\n", GEUL_E200_CORE_REVB_NUM);
		exit(0);
	}

	printf("Modem Id=%d, Core =%d\n", ropt->modem_id, ropt->core_id);
}

void send_cli_cmdline(char *cmd)
{
	MEM_MAP_T temp_map;
	uint64_t phys_addr;
	modinfo_t mil;
	modinfo_t *mi = &mil;
	int fd;
	int ret;
	char time_str[32];
	char sleep_cmd[32];
	int cmd_len = strlen(run_options.cmd_line);
	char dev_name[32];
	char substr[5];

	if (!cmd)
		return;

	strncpy(substr, cmd, 4);
	substr[4] = '\0';
	/* get the HIF-related addrs from module info */
	sprintf(dev_name, "/dev/%s%d", GUL_MODINFO_DEVNAME_PREFIX,
		run_options.modem_id);
	fd = open(dev_name, O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("Could not open gul modinfo dev");
		return;
	}

	memset(&mil, 0, sizeof(mil));

	ret = ioctl(fd, IOCTL_GUL_MODINFO_GET, (modinfo_t *) &mil);
	close(fd);
	if (ret < 0) {
		printf("IOCTL_GUL_MODINFO_GET failed.\n");
		return;
	}

	memset(&temp_map, 0, sizeof(temp_map));

	phys_addr = mi->scratchregions[GUL_SCRATCH_MODEM_SHARE].host_phy_addr;
	phys_addr += mi->scratchbuf.size - MAX_CMD_LEN;
	map_physical_region(&temp_map, phys_addr, MAX_CMD_LEN);

	if (!temp_map.virt_addr) {
		fprintf(stderr, "Map error! Check address and/or size!");
		unmap_physical_region(&temp_map);
		return;
	}

	memcpy(temp_map.virt_addr, cmd, MAX_CMD_LEN);
	unmap_physical_region(&temp_map);

	if (strncmp(substr, "test", 4) == 0)
		cli_t.mbox.msg_id = GUL_CLI_MSG_ID_GENERIC;
	else
		cli_t.mbox.msg_id = GUL_CLI_MSG_ID_L1REFAPP;
	cli_t.mbox.data =
	    mi->scratchregions[GUL_SCRATCH_MODEM_SHARE].modem_phy_addr;
	cli_t.mbox.data += mi->scratchbuf.size - MAX_CMD_LEN;
	cli_t.mbox.data2 = run_options.core_id;
	switch (run_options.mode) {
	case SYNC_MODE:
		libcli_send_msi_sync(cli_t.handle, &cli_t);
		break;
	case ASYNC_MODE:
		libcli_send_msi_async(cli_t.handle, &cli_t);
		break;
	case WAIT_MODE:
		memmove(time_str, run_options.cmd_line + 5, cmd_len - 6);
		sprintf(sleep_cmd, "%s %d", "sleep ", atoi(time_str));
		system(sleep_cmd);
		break;
	default:
		libcli_send_msi_sync(cli_t.handle, &cli_t);
		break;
	}
}

void sigint_handler(int sig)
{
	keep_running = 0;
}

void select_cmd_mode(run_opt_t *ropt)
{
	int cmd_len = strlen(ropt->cmd_line);

	//selecting sync mode by default
	ropt->mode = SYNC_MODE;
	keep_running = 1;
	if (cmd_len > 3 && (ropt->cmd_line[cmd_len - 3] == '-') &&
			    (ropt->cmd_line[cmd_len - 2] == 's')) {

		ropt->mode = SYNC_MODE;
	// trimming white space , new line and null character.
		ropt->cmd_line[cmd_len - 4] = '\0';
		cmd_len = cmd_len - 4;
	} else if (cmd_len > 3 && (ropt->cmd_line[cmd_len - 2] == '&')) {

		ropt->mode = ASYNC_MODE;
		// trimming white space , new line and null character.
		ropt->cmd_line[cmd_len - 3] = '\0';
		cmd_len = cmd_len - 3;
	} else if (strncmp(ropt->cmd_line, "wait", 4) == 0) {
		ropt->mode = WAIT_MODE;
		if (cmd_len < 7) {
			print_help("gul_cli");
			exit(0);
		}
	}

	else {
		// trimming new line char at the end.
		ropt->cmd_line[cmd_len - 1] = '\0';
	}

}

char *run_command(char *cmd_str)
{
	FILE *fp;
	static char buffer[256] = {};

	/* Open the command for reading. */
	fp = popen(cmd_str, "r");
	if (fp == NULL) {
		printf("Failed to run command\n");
		return NULL;
	}

	/* Read the output a line at a time - output it. */
	fgets(buffer, sizeof(buffer), fp);

	/* close */
	pclose(fp);
	return buffer; //returning the first line only.

}
void run_loop_for_cmd(void)
{
	fd_set input_set;
	int select_out=0;
	struct timeval  timeout;
	bool isfirsttime = true;
	bool selftimeout = false;
	char line[MAX_CMD_LEN];
	FILE* file;
	if(command_file)
	{
		file = fopen(run_options.cmd_file_name, "r");
		if(!file){
			printf("\n Unable to open command file : %s \r\n", run_options.cmd_file_name);
			command_file = 0;
			exit(-1);
		}
	}
	while (keep_running) {
		/* Empty the FD Set */
		FD_ZERO(&input_set);
		/* Listen to the input descriptor */
		FD_SET(STDIN_FILENO, &input_set);

		if (run_options.mode != ASYNC_MODE)
			libcli_dump_core_log(cli_t.handle, &cli_t);
		if(isfirsttime || (selftimeout == false)) {
			printf("\nPress Ctrl+C to exit");
			printf("\nFrtos> ");
		}
		fflush(NULL);
		memset(run_options.cmd_line, '\0', MAX_CMD_LEN);
		isfirsttime = false;
		/* Waiting for some seconds */
		timeout.tv_sec = 10;    // WAIT seconds
		timeout.tv_usec = 0;    // 0 milliseconds

		if(command_file)
		{
			while (fgets(line, sizeof(line), file)) {
				printf("Send command to  Modem %s", line);
				send_cli_cmdline(line);
				while (libcli_blockforEvent_read(cli_t.handle,
					&cli_t, sizeof(struct cli_mbox_u)) < 0);
				libcli_dump_core_log(cli_t.handle, &cli_t);
			}
			fclose(file);
			command_file = 0;
			file = NULL;
			/* handling remaining logs */
			sleep(1);
			libcli_dump_core_log(cli_t.handle, &cli_t);
			exit(0);
		}

		select_out = select(1, &input_set, NULL, NULL, &timeout);
		if (select_out == -1) {
			printf("Select_out is -1\r\n");
			break;
		}
		else if (select_out)
		{
			read(0, run_options.cmd_line, MAX_CMD_LEN);
			selftimeout = false;
		}
		else {
			selftimeout = true;
			continue;
		}
		select_cmd_mode(&run_options);
		switch (run_options.mode) {
		case SYNC_MODE:
			send_cli_cmdline(run_options.cmd_line);
			while (libcli_blockforEvent_read(cli_t.handle,
				&cli_t, sizeof(struct cli_mbox_u)) < 0);
			libcli_dump_core_log(cli_t.handle, &cli_t);
			break;
		case ASYNC_MODE:
			send_cli_cmdline(run_options.cmd_line);
			break;
		case WAIT_MODE:
			send_cli_cmdline(run_options.cmd_line);
			break;
		}
	}

	if (command_file) {
		fclose(file);
		file  = NULL;
	}
}

void disable_timestamps_print(void)
{
	system("echo \"N\" > /sys/module/printk/parameters/time");
}

void enable_timestamps_printk(void)
{
	system("echo \"Y\" > /sys/module/printk/parameters/time");
}

int main(int argc, char *argv[])
{
	struct sigaction act = { 0 };

	act.sa_handler = sigint_handler;
	sigaction(SIGINT, &act, NULL);

	parse_arguments(argc, argv, &run_options);

	if (libcli_register(&cli_t,
		run_options.modem_id, run_options.core_id) < 0) {
		printf("%s Registration failed\n", __func__);
		goto error;
	}
	if (run_options.cmd_send) {
		send_cli_cmdline(run_options.cmd_line);
		if (run_options.wait_events) {
			printf("Waiting events from LA12XX Modem...\n");
			while (keep_running) {
				if (!(libcli_blockforEvent_read(cli_t.handle,
							&cli_t,
							sizeof(struct cli_mbox_u)) < 0))
					break;

				printf("dbg: msg_id=%#x data=%#x, data2=%#x, data3=%#x\n",
						cli_t.mbox.msg_id,
						cli_t.mbox.data,
						cli_t.mbox.data2,
						cli_t.mbox.data3);
			}
		}
		goto done;
	}

	disable_timestamps_print();
	run_loop_for_cmd();
	enable_timestamps_printk();
done: 
	libcli_deregister(&cli_t);
	exit(EXIT_SUCCESS);

error:
	exit(EXIT_FAILURE);
}
