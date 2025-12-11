// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright 2022-2024 NXP
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "gul_modinfo.h"

#ifndef MAX_MODEM
#define MAX_MODEM 4
#endif
#define GUL_DEV_NAME_PREFIX "gul"
char modem_pci_id[15];
int modem_pci_id_match;
int modem_id = -1;

static const char *SOC_DCS[] = {
	"NO_HSDCS",
	"HSDCS_RXONLY",
	"HSDCS_FULL"
};

typedef enum {
	HSDCS_NO,
	HSDCS_RXONLY,
	HSDCS_FULL,
} soc_type_t;

uint32_t get_hsdcs_support(uint32_t rev)
{
	switch (rev & GEUL_SVR_HSDCS_MASK) {
	case GEUL_SVR_HSDCS_NO:
		return HSDCS_NO;
	case GEUL_SVR_HSDCS_RXONLY:
		return HSDCS_RXONLY;
	default:
		return HSDCS_FULL;
	}
	return HSDCS_FULL;
}

void print_usage_message(void)
{
	printf("Usage : ./modem_info [optional] modem_pci_id\n");
	printf("\n");
	printf("\t-h or -H    help and usages\n");
	printf("\tmodem_pci_id    - as Modem PCI address e.g. 0002:01:00.0\n");
	printf("\t-m <dev id>\n");
	fflush(stdout);

	exit(EXIT_SUCCESS);
}

void validate_cli_args(int argc, char *argv[])
{
	int strsize;

	if (argc < 2)
		return;

	if ((strcmp(argv[1], "-h") == 0)  ||
			(strcmp(argv[1], "-H") == 0))
		print_usage_message();

	if ((strcmp(argv[1], "-m") == 0)  && argc == 3) {
		modem_id = atoi(argv[2]);
		return;
	}

	strsize = strlen(argv[1]);
	if (strsize < 12)
		print_usage_message();
	strncpy (modem_pci_id, argv[1], 15);
	modem_pci_id_match = 1;
}

static char *socrev_to_str(uint32_t rev)
{
	switch (rev) {
		case GEUL_SVR_REVA_VAL:
			return "REVA";
		case GEUL_SVR_REVB_VAL:
			return "REVB";
		case GEUL_SVR_UNKNOWN_VAL:
			return "UNKNOWN";
		default:
			printf("invalid geul rev[%d] returned from ioctl\n", rev);
			return "";
	}
}

void print_modem_info(int id)
{
	int fd;
	modinfo_t mil = {0};
	modinfo_t *mi = &mil;
	int i = 0, scr_i, ret;
	char dev_name[32];

	sprintf(dev_name, "/dev/%s%d", GUL_DEV_NAME_PREFIX, id);

	fd = open(dev_name, O_RDWR);
	if (fd < 0)
		return;

	ret = ioctl(fd, IOCTL_GUL_MODINFO_GET, (modinfo_t *) &mil);
	if (ret < 0) {
		printf("IOCTL_GUL_MODINFO_GET failed.\n");
		close(fd);
		return;
	}

	if (mi->active && (!modem_pci_id_match ||
		(modem_pci_id_match && !strcmp(mi->name, modem_pci_id)))) {

		printf("Modinfo for gul_dev at idx[%d], active =%d\n", i, mi->active);
		printf("LA12xx ID:%d\n", mi->id);
		printf("PCI DEV Name - %s\n", mi->name);
		printf("SOC SVR 0x%x- Revision:%s - %s\n", mi->rev,
			socrev_to_str(mi->rev & GEUL_SVR_REV_MASK),
			SOC_DCS[get_hsdcs_support(mi->rev)]);
		printf("TBGen1 Freq = %u KHz\n", mi->clk_info.tbgen1_freq);
		printf("TBGen1 Clk Src = %s\n", mi->clk_info.tbgen1_src);
		printf("TBGen2 Freq = %u KHz\n", mi->clk_info.tbgen2_freq);
		printf("TBGen2 Clk Src = %s\n", mi->clk_info.tbgen2_src);
		printf("LS ADC SPS = %d Msps\n", mi->clk_info.ls_adc_sps);
		printf("LS DAC SPS = %d Msps\n", mi->clk_info.ls_dac_sps);
		printf("HS DCS SPS = %d Msps\n", mi->clk_info.hs_dcs_sps);
		printf("HIF start 0x%lx Size:0x:%x\n",
			mi->hif.host_phy_addr, mi->hif.size);

		printf("Modem log to Host Status: ");
		(mi->modem_host_uart) ? printf("Enable\r\n") : printf("Disable\r\n");

		printf("CCSR phys 0x%lx Size:0x%x\n",
			mi->ccsr.host_phy_addr, mi->ccsr.size);
		printf("DCSR start 0x%lx Size:0x%x\n",
			mi->dcsr.host_phy_addr, mi->dcsr.size);
		printf("PEB start 0x%lx Size:0x%x\n",
			mi->peb.host_phy_addr, mi->peb.size);
		printf("FECA start 0x%lx Size:0x%x\n",
			mi->feca.host_phy_addr, mi->feca.size);

		printf("Scratch buf phys 0x%lx Size:0x%x\n",
			mi->scratchbuf.host_phy_addr, mi->scratchbuf.size);

		printf("Scrach Buf Regions\n");
		printf("%s| %s|%s| %s\n", "Region", "Host Phy Addr", "Modem Phy Addr", "Size");
		for (scr_i = 0; scr_i < GUL_SCRATCH_END; scr_i++) {
			printf("%5d | 0x%lx |   0x%x | 0x%x\n",
				scr_i, mi->scratchregions[scr_i].host_phy_addr,
				mi->scratchregions[scr_i].modem_phy_addr,
				mi->scratchregions[scr_i].size);
		}
	}
	close(fd);
}

int main(int argc, char *argv[])
{
	int i = 0;

	/* Validate CLI Args */
	validate_cli_args(argc, argv);


	if (modem_id != -1)
		print_modem_info(modem_id);
	else {
		for (i = 0; i < MAX_MODEM; i++)
			print_modem_info(i);
	}
	return 0;
}
