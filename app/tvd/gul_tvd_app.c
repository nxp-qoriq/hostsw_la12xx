/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2020-2023 NXP
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
#include <gul_tvd_api.h>


#ifndef MAX_MODEM
#define MAX_MODEM 4
#endif
#define MAX_EVENTS 1
#define	EINVAL		22	/* Invalid argument */
#ifdef DEBUG_THERMAL
#define pr_debug(...) printf(__VA_ARGS__)
#else
#define pr_debug(...)
#endif

#define MAX_TEMP_THRESHOLD 2
#define TEMP_HYSTERESIS	3
#define SET_TEMP_ABOVE_HYSTERESIS (TEMP_HYSTERESIS + 1)
#define SET_TEMP_BELOW_HYSTERESIS (TEMP_HYSTERESIS + 2)
#define INCREMENT_DECREMENT_TEMP_WITHIN_HYSTERESIS 1
#define INCREMENT_DECREMENT_TEMP_FROM_CURRENT_TEMP 20

static int modem_id;
pthread_t tid;
struct tvd tvd_t = {0};
static bool modem_monitor;
static bool cpu_monitor;
static bool rf_monitor;
static bool modem_power_monitor;
static int time_delay = 5;
static bool event_mode;
static bool single_run = true;

void print_usage_message(void)
{
	printf("Usage: ./gul_tvd_testapp modem_id -m -c -r -p -t 5\n");
	printf("       OR\n");
	printf("     : ./gul_tvd_testapp modem_id -e\n\n");

	printf("\tmodem_id    -- decimal | 0..(%d)\n", MAX_MODEM-1);
	printf("Options:\n");
	printf("\t–h\tHelp\n");
	printf("\t–m\tMonitor Modem temperature\n");
	printf("\t–c\tMonitor Host temperature\n");
	printf("\t–r\tMonitor RF card temperature\n");
	printf("\t–p\tMonitor Modem Power Info\n");
	printf("\t-t\tTime Interval (in seconds)\n");

	fflush(stdout);
	exit(EXIT_SUCCESS);
}

void *temp_test_thread(void *arg)
{
	printf("cli thread started\n");
	while (1) {
		/*
		 * Example let's say current temperature is  50
		 * Below setting set  avgerage temperature threshold to  49
		 * and critical threshold to  52.
		 * Once above setting is done,
		 * it will trigger avgerage high threshold interrupt and event
		 */
		printf("\n\nTest-1 CTD avg high threshold crossed test\n");
		libtvd_ctd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		printf("%s: current ctd temp is %d\n", __func__, tvd_t.get_ctd_curr_temp);
		tvd_t.thermal_threshold_source.ctd_threshold.threshold_count = MAX_TEMP_THRESHOLD;
		tvd_t.thermal_threshold_source.ctd_threshold.threshold[0] =
			tvd_t.get_ctd_curr_temp - INCREMENT_DECREMENT_TEMP_FROM_CURRENT_TEMP;
		tvd_t.thermal_threshold_source.ctd_threshold.threshold[1] =
			tvd_t.get_ctd_curr_temp - INCREMENT_DECREMENT_TEMP_WITHIN_HYSTERESIS;
		libtvd_ctd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
		sleep(20);

		printf("\n\nTest-1 MTD avg high threshold crossed test\n");
		tvd_t.mtd_site = VSPA_TEMP;
		libtvd_mtd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		printf("%s: current mtd temp is %d\n", __func__, tvd_t.get_mtd_curr_temp);
		tvd_t.thermal_threshold_source.mtd_threshold.threshold_count = MAX_TEMP_THRESHOLD;
		tvd_t.thermal_threshold_source.mtd_threshold.threshold[0] =
			tvd_t.get_mtd_curr_temp - INCREMENT_DECREMENT_TEMP_FROM_CURRENT_TEMP;
		tvd_t.thermal_threshold_source.mtd_threshold.threshold[1] =
			tvd_t.get_mtd_curr_temp - INCREMENT_DECREMENT_TEMP_WITHIN_HYSTERESIS;
		libtvd_mtd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
		sleep(20);

		 /* Example let's say current temperature is  50
		  * Below setting set  avgerage temperature threshold to  46
		  * and critical threshold to 49 .
		  * Once above setting is done,
		  * it will trigger critical high threshold interrupt and event
		  */
		printf("\n\n Test-2 CTD critical high threshold crossed test\n");
		libtvd_ctd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		printf("%s: current ctd temp is %d\n", __func__, tvd_t.get_ctd_curr_temp);
		tvd_t.thermal_threshold_source.ctd_threshold.threshold_count = MAX_TEMP_THRESHOLD;
		tvd_t.thermal_threshold_source.ctd_threshold.threshold[0] =
			tvd_t.get_ctd_curr_temp - INCREMENT_DECREMENT_TEMP_FROM_CURRENT_TEMP;
		tvd_t.thermal_threshold_source.ctd_threshold.threshold[1] =
			tvd_t.get_ctd_curr_temp - SET_TEMP_ABOVE_HYSTERESIS;
		libtvd_ctd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
		sleep(20);

		printf("\n\n Test-2 MTD critical high threshold crossed test\n");
		tvd_t.mtd_site = VSPA_TEMP;
		libtvd_mtd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		printf("%s: current mtd temp is %d\n", __func__, tvd_t.get_mtd_curr_temp);
		tvd_t.thermal_threshold_source.mtd_threshold.threshold_count = MAX_TEMP_THRESHOLD;
		tvd_t.thermal_threshold_source.mtd_threshold.threshold[0] =
			tvd_t.get_mtd_curr_temp - INCREMENT_DECREMENT_TEMP_FROM_CURRENT_TEMP;
		tvd_t.thermal_threshold_source.mtd_threshold.threshold[1] =
			tvd_t.get_mtd_curr_temp - SET_TEMP_ABOVE_HYSTERESIS;
		libtvd_mtd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
		sleep(20);


		 /* Example let's say current temperature is  50
		  * Below setting set  low avgerage temperature threshold to  51
		  * and critical threshold to 48 .
		  * Once above setting is done,
		  * it will trigger low average threshold interrupt and event
		  */
		printf("\n\nTest-3 CTD threshold crossed and comes below avg test\n");
		libtvd_ctd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		printf("%s: current ctd temp is %d\n", __func__, tvd_t.get_ctd_curr_temp);
		tvd_t.thermal_threshold_source.ctd_threshold.threshold_count = MAX_TEMP_THRESHOLD;
		tvd_t.thermal_threshold_source.ctd_threshold.threshold[0] =
			tvd_t.get_ctd_curr_temp + INCREMENT_DECREMENT_TEMP_WITHIN_HYSTERESIS;
		tvd_t.thermal_threshold_source.ctd_threshold.threshold[1] =
			tvd_t.get_ctd_curr_temp + INCREMENT_DECREMENT_TEMP_FROM_CURRENT_TEMP;
		libtvd_ctd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
		sleep(20);


		printf("\n\nTest-3 MTD threshold crossed and comes below avg test\n");
		tvd_t.mtd_site = VSPA_TEMP;
		libtvd_mtd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		printf("%s: current mtd temp is %d\n", __func__, tvd_t.get_mtd_curr_temp);
		/* Test MTD crit threshold crossed and avg crossed*/
		tvd_t.thermal_threshold_source.mtd_threshold.threshold_count = MAX_TEMP_THRESHOLD;
		tvd_t.thermal_threshold_source.mtd_threshold.threshold[0] =
			tvd_t.get_mtd_curr_temp + INCREMENT_DECREMENT_TEMP_WITHIN_HYSTERESIS;
		tvd_t.thermal_threshold_source.mtd_threshold.threshold[1] =
			tvd_t.get_mtd_curr_temp + INCREMENT_DECREMENT_TEMP_FROM_CURRENT_TEMP;
		libtvd_mtd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
		sleep(20);


		 /* Example let's say current temperature is  50
		  * Below setting set  low avgerage temperature threshold to  55
		  * and critical threshold to 52 .
		  * Once above setting is done,
		  * it will trigger low critical threshold interrupt and event
		  */
		printf("\n\nTest-4 CTD  comes below crit threshold test\n");
		libtvd_ctd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		printf("%s: current ctd temp is %d\n", __func__, tvd_t.get_ctd_curr_temp);
		tvd_t.thermal_threshold_source.ctd_threshold.threshold_count = MAX_TEMP_THRESHOLD;
		tvd_t.thermal_threshold_source.ctd_threshold.threshold[0] =
			tvd_t.get_ctd_curr_temp + SET_TEMP_BELOW_HYSTERESIS;
		tvd_t.thermal_threshold_source.ctd_threshold.threshold[1] =
			tvd_t.get_ctd_curr_temp + INCREMENT_DECREMENT_TEMP_FROM_CURRENT_TEMP;
		libtvd_ctd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
		sleep(20);

		printf("\n\nTest-4 MTD  comes below crit threshold test\n");
		tvd_t.mtd_site = VSPA_TEMP;
		libtvd_mtd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		printf("%s: current mtd temp is %d\n", __func__, tvd_t.get_mtd_curr_temp);
		/* Test MTD crit threshold crossed and avg crossed*/
		tvd_t.thermal_threshold_source.mtd_threshold.threshold_count = MAX_TEMP_THRESHOLD;
		tvd_t.thermal_threshold_source.mtd_threshold.threshold[0] =
			tvd_t.get_mtd_curr_temp + SET_TEMP_BELOW_HYSTERESIS;
		tvd_t.thermal_threshold_source.mtd_threshold.threshold[1] =
			tvd_t.get_mtd_curr_temp + INCREMENT_DECREMENT_TEMP_FROM_CURRENT_TEMP;
		libtvd_mtd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
		sleep(20);

		if (tvd_t.get_rtd_curr_temp != RTD_TEMP_INVALID) {
			printf("\n\n=====RTD avg threshold crossed test====\n");
			/* Test RTD crit threshold crossed */
			tvd_t.thermal_threshold_source.rtd_threshold.threshold_count = 2;
			tvd_t.thermal_threshold_source.rtd_threshold.threshold[0] = tvd_t.get_rtd_curr_temp - 10;
			tvd_t.thermal_threshold_source.rtd_threshold.threshold[1] = tvd_t.get_rtd_curr_temp;
			libtvd_rtd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
			sleep(10);
			/* Test RTD crit threshold crossed threshold*/
			printf("\n\n=====RTD crit threshold crossed test=====\n");
			tvd_t.thermal_threshold_source.rtd_threshold.threshold_count = 2;
			tvd_t.thermal_threshold_source.rtd_threshold.threshold[0] = tvd_t.get_rtd_curr_temp;
			tvd_t.thermal_threshold_source.rtd_threshold.threshold[1] = tvd_t.get_rtd_curr_temp - 10;
			libtvd_rtd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
			sleep(10);
			printf("\n\n=====RTD comes below crit threshold test====\n");
			/* Test RTD crit threshold crossed and avg crossed*/
			tvd_t.thermal_threshold_source.rtd_threshold.threshold_count = 2;
			tvd_t.thermal_threshold_source.rtd_threshold.threshold[0] = tvd_t.get_rtd_curr_temp;
			tvd_t.thermal_threshold_source.rtd_threshold.threshold[1] = tvd_t.get_rtd_curr_temp + 10;
			libtvd_rtd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
			sleep(10);
			printf("\n\n=====RTD comes below avg threshold test====\n");
			/* Test RTD crit threshold crossed and avg crossed*/
			tvd_t.thermal_threshold_source.rtd_threshold.threshold_count = 2;
			tvd_t.thermal_threshold_source.rtd_threshold.threshold[0] = tvd_t.get_rtd_curr_temp + 10;
			tvd_t.thermal_threshold_source.rtd_threshold.threshold[1] = tvd_t.get_rtd_curr_temp;
			libtvd_rtd_threshold_update(tvd_t.dev_tvd_handle, &tvd_t);
		}

	}

}

void validate_cli_args(int argc, char *argv[])
{
	int opt;

	if (argc <= 1)
		print_usage_message();

	modem_id  = atoi(argv[1]);
	while ((opt = getopt(argc, argv, "mcrpet:h")) != -1) {
		switch (opt) {
		case 'm':
			modem_monitor = true;
			break;
		case 'c':
			cpu_monitor = true;
			break;
		case 'r':
			rf_monitor = true;
			break;
		case 'p':
			modem_power_monitor = true;
			break;
		case 't':
			time_delay = atoi(optarg);
			single_run = false;
			break;
		case 'e':
			printf("event_mode\n");
			event_mode = true;
			break;
		case 'h':
			print_usage_message();
			break;

		case '?':
			printf("Unknown option: -%c\n", optopt);
			print_usage_message();
			break;
		default:
			print_usage_message();
			break;
		}
	}
}

void ctd_tvd_event_type(enum ctd_event_type event_type)
{
		switch (event_type) {
		case CTD_HIGH_AVRG_TEMP_EVENT:
				printf("CTD_EVENT:(Test-1 Passed) Temp crossed the Avg temp threshold\n");
				break;
		case CTD_HIGH_CRITIC_TEMP_EVENT:
				printf("CTD_EVENT:(Test-2 Passed) Temp crossed the Critical temp threshold\n");
				break;
		case CTD_LOW_AVRG_TEMP_EVENT:
				printf("CTD_EVENT:(Test-3 Passed) Temp is below Avg temp threshold\n");
				break;
		case CTD_LOW_CRITIC_TEMP_EVENT:
				printf("CTD_EVENT:(Test-4 Passed) Temp is below Critical temp threshold\n");
				break;
		default:
				printf("Unknown Event type\n");
				break;
		}
}

void mtd_tvd_event_type(enum mtd_event_type event_type)
{
		switch (event_type) {
		case MTD_HIGH_TEMP_EVENT:
				printf("MTD_EVENT:(Test-1 Passed) Temp crossed the High temp threshold\n");
				break;
		case MTD_HIGH_CRITIC_TEMP_EVENT:
				printf("MTD_EVENT:(Test-2 Passed) Temp crossed the Critical temp threshold\n");
				break;
		case MTD_LOW_TEMP_EVENT:
				printf("MTD_EVENT:(Test-3 Passed) Temp is below High temp threshold\n");
				break;
		case MTD_LOW_CRITIC_TEMP_EVENT:
				printf("MTD_EVENT:(Test-4 Passed) Temp is below Critical temp threshold\n");
				break;
		default:
				printf("Unknown Event type\n");
				break;
		}
}

void rtd_tvd_event_type(enum rtd_event_type event_type)
{
	switch (event_type) {
	case RTD_HIGH_CRITIC_TEMP_EVENT:
		printf("RTD_EVENT: Temp crossed the Critical temp threshold\n");
		break;

	case RTD_HIGH_TEMP_EVENT:
		printf("RTD_EVENT: Temp crossed the High temp threshold\n");
		break;

	case RTD_LOW_TEMP_EVENT:
		printf("RTD_EVENT: Temp comes to below High temp threshold\n");
		break;

	case RTD_LOW_CRITIC_TEMP_EVENT:
		printf("RTD_EVENT: Temp comes to below Critical temp threshold\n");
		break;

	case RTD_INVALID_TEMP_EVENT:
		printf("RTD_EVENT: Invalid temp\n");
		break;
	default:
		printf("Unknown Event type\n");
		break;
	}
}

int main(int argc, char *argv[])
{
	ssize_t bytes_read;
	int ret = 0;
	union mtdcurentTemp mtd_temp;
	union mtdpowerInfo mtd_power;
	modem_id = 0;
	modem_monitor = false;
	cpu_monitor = false;
	rf_monitor = false;
	time_delay = 5;
	event_mode = false;
	modem_power_monitor = false;

	/* Validate CLI Args */
	validate_cli_args(argc, argv);

	/* Register TVD module  as per command line params supplied */
	ret = libtvd_register(&tvd_t, modem_id);
	if (ret < 0) {
		printf("%s Registration failed\n", __func__);
		goto error;
	}

	/* Changing MTD hysteresis value */
	tvd_t.hysteresis.mtd_hysteresis_val = 3;
	libtvd_mtd_hysteresis_update(tvd_t.dev_tvd_handle, &tvd_t);

	/* Changing CTD hysteresis value */
	tvd_t.hysteresis.ctd_hysteresis_val = 3;
	libtvd_ctd_hysteresis_update(tvd_t.dev_tvd_handle, &tvd_t);

	if (event_mode == true)	{
		struct epoll_event ev, events[MAX_EVENTS];
		int epollfd = -1, nfds;
		uint64_t eftd_ctr;

		pr_debug("%s: event mode\n", __func__);
		tvd_t.mtd_site = VSPA_TEMP;
		libtvd_mtd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		pr_debug("%s: current mtd temp is %d\n", __func__, tvd_t.get_mtd_curr_temp);
		libtvd_ctd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		pr_debug("%s: current ctd temp is %d\n", __func__, tvd_t.get_ctd_curr_temp);
		libtvd_rtd_get_temp(tvd_t.dev_tvd_handle, &tvd_t);
		pr_debug("%s: current rtd temp is %d\n", __func__,  tvd_t.get_rtd_curr_temp);

		pthread_create(&tid, NULL, temp_test_thread, NULL);

		if (!(tvd_t.tvd_eventfd < 0)) {
			epollfd = epoll_create1(0);

			if (!(epollfd == -1)) {
				ev.events = EPOLLIN;
				ev.data.fd = tvd_t.tvd_eventfd;
				if (epoll_ctl(epollfd, EPOLL_CTL_ADD,
					tvd_t.tvd_eventfd, &ev) == -1) {
					printf("failed to add tvd_eventfd\n");
					close(epollfd);
					epollfd = -1;
				}
			} else
				printf("epoll_create1 failed\n");
		}

		while (1) {
			if (!(tvd_t.tvd_eventfd < 0)) {
				if (!(epollfd < 0)) {
					pr_debug("Waiting for event\n");
					nfds = epoll_wait(epollfd, events,
							MAX_EVENTS, -1);

					if (nfds == 1) {
						pr_debug("Read eventfd to avoid stale data in buffer\n");
						bytes_read = read(tvd_t.tvd_eventfd,
								&eftd_ctr, sizeof(uint64_t));
						pr_debug("epoll_wait: event received from tvd bytes_read %ld\n", bytes_read);
					}

					pr_debug(" Waiting on eventfd... Done\n");
				}
			}

			bytes_read = libtvd_blockforEvent_read(tvd_t.dev_tvd_handle,
					&tvd_t,
					sizeof(uint64_t));

			if (bytes_read != sizeof(uint64_t)) {
				printf("%s:Read error. Exiting...\n", __func__);
				goto error;
			} else {
				printf("\nThermal EventId=%u\n", tvd_t.thermal_event_source.thermal_id);
				switch (tvd_t.thermal_event_source.thermal_id) {
				case CTD_EVENT:
						pr_debug("CTD Thermal Events are:\n");
						pr_debug(" Event Type: %u, Current Temp: %d\n",
							tvd_t.thermal_event_source.ctd.event_type,
							tvd_t.thermal_event_source.ctd.curr_temp);
						ctd_tvd_event_type(tvd_t.thermal_event_source.ctd.event_type);
						break;
				case MTD_EVENT:
						pr_debug("MTD Thermal Events are:\n");
						pr_debug(" Event Type: %u, Current Temp: %d\n",
							tvd_t.thermal_event_source.mtd.event_type,
							tvd_t.thermal_event_source.mtd.curr_temp);
						mtd_tvd_event_type(tvd_t.thermal_event_source.mtd.event_type);
						break;
				case BOTH_CTD_MTD_EVENT:
						pr_debug("Both Thermal Events are:\n");
						ctd_tvd_event_type(tvd_t.thermal_event_source.ctd.event_type);
						mtd_tvd_event_type(tvd_t.thermal_event_source.mtd.event_type);
						break;
				case RTD_EVENT:
						pr_debug("RTD Thermal Events are:\n");
						if (tvd_t.thermal_event_source.rtd.curr_temp == RTD_TEMP_INVALID) {
							pr_debug(" RF Card Temp Not Known\n");
						} else {
							pr_debug(" Event Type: %u, Current Temp: %d\n",
							tvd_t.thermal_event_source.rtd.event_type,
							tvd_t.thermal_event_source.rtd.curr_temp);
							rtd_tvd_event_type(tvd_t.thermal_event_source.rtd.event_type);
						}
						break;
				default:
						printf("Not a valid thermal event\n");
						break;
				}
			}
		}

		if (!(epollfd < 0))
			close(epollfd);

	} else {
		pr_debug("%s: polling mode\n", __func__);

		if (modem_monitor == false && cpu_monitor == false &&
				rf_monitor == false &&
				modem_power_monitor == false) {
			printf("Not a valid monitor mode\n");
			print_usage_message();
			goto error;
		}

		if (modem_monitor == true)
			printf("\nVSPA:\tFECA:\tPCI:\tDiode:\t");
		if (cpu_monitor == true)
			printf("CPU:\t");
		if (rf_monitor == true)
			printf("RF:\t");
		if (modem_power_monitor == true)
			printf("Modem:Current:\tShunt-Voltage:\tBus-Voltage:\t"
				"Power:");


		while (1) {
			printf("\n");
			if (modem_monitor == true) {
				tvd_t.mtd_site = COMBINED_TEMP;
				libtvd_mtd_get_temp(tvd_t.dev_tvd_handle,
						&tvd_t);
				mtd_temp.temp = tvd_t.get_mtd_curr_temp;
				printf("%d°C \t %d°C \t %d°C\t",
				TMU_ADJUST_TEMP_HOST_CTXT(mtd_temp.vspa_temp),
				TMU_ADJUST_TEMP_HOST_CTXT(mtd_temp.feca_temp),
				TMU_ADJUST_TEMP_HOST_CTXT(mtd_temp.pci_temp));

				if ((TMU_ADJUST_TEMP_HOST_CTXT(mtd_temp.diode_temp)) != MTD_TEMP_INVALID) {
					printf("%d°C\t",
					TMU_ADJUST_TEMP_HOST_CTXT(mtd_temp.diode_temp));
				} else {
					printf("N/A\t");
				}
			}
			if (cpu_monitor == true) {
				libtvd_ctd_get_temp(tvd_t.dev_tvd_handle,
						&tvd_t);
				printf("%d°C\t", tvd_t.get_ctd_curr_temp);
			}
			if (rf_monitor == true) {
				libtvd_rtd_get_temp(tvd_t.dev_tvd_handle,
						&tvd_t);
				if (tvd_t.get_rtd_curr_temp != RTD_TEMP_INVALID)
					printf("%d°C\t",
						tvd_t.get_rtd_curr_temp);
			}
			if (modem_power_monitor == true) {
				libtvd_mtd_get_power_info(tvd_t.dev_tvd_handle,
						&tvd_t);
				mtd_power.power_info = tvd_t.get_mtd_power_info;
				if (mtd_power.power_info == 0) {
					printf("Power sensor not available / disabled!");
				} else if (mtd_power.power_info == -EINVAL) {
					printf("Error: Unable to get power info.");
				} else {
					printf("%d.%dA\t\t%d.%dV\t\t%d.%dV\t\t%d.%dW",
						(mtd_power.current_val / 1000),
						(mtd_power.current_val % 1000),
						(mtd_power.shunt_volt / 1000),
						(mtd_power.shunt_volt % 1000),
						(mtd_power.bus_volt / 1000),
						(mtd_power.bus_volt % 1000),
						(mtd_power.power_val / 1000),
						(mtd_power.power_val % 1000));
				}
			}
			fflush(stdout);
			if (single_run)
				break;
			sleep(time_delay);
		}
	}

	/* Deregister TVD */
	libtvd_deregister(&tvd_t);
	pr_debug("%s: Done...\n", __func__);
	exit(EXIT_SUCCESS);
	return 0;

error:
	exit(EXIT_FAILURE);
}
