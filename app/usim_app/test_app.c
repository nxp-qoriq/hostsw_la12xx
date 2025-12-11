/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2021-2022 NXP
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usim_library.h"
#include "usim_ioctl.h"
#include "usim_apdu.h"


extern sim_t sim;
extern int debug;

void read_nasconfig(void)
{
	int ret = -1;
	vtq_usim_nasconfig_t nasconfig;

	nasconfig.signalling_priority.value = (unsigned char *)malloc(4);
	nasconfig.nmo_i_behaviour.value = (unsigned char *)malloc(4);
	nasconfig.attach_with_imsi.value = (unsigned char *)malloc(4);
	nasconfig.min_p_search_timer.value = (unsigned char *)malloc(4);
	nasconfig.ext_access_barring.value = (unsigned char *)malloc(4);
	nasconfig.timer_t3245_behaviour.value = (unsigned char *)malloc(4);

	nasconfig.o_nas_signalling_low_priority.value =
						(unsigned char *)malloc(4);

	nasconfig.o_extended_access_barring.value = (unsigned char *)malloc(4);
	nasconfig.fast_first_higher_priority_plmn.value =
						(unsigned char *)malloc(4);
	nasconfig.eutra_disabling_capable.value = (unsigned char *)malloc(4);
	nasconfig.sm_retry_waittime.value = (unsigned char *)malloc(4);
	nasconfig.sm_retry_atrat_change.value = (unsigned char *)malloc(4);
	nasconfig.default_dcn_id.value = (unsigned char *)malloc(4);
	nasconfig.e_data_reporting_allowed.value = (unsigned char *)malloc(4);
	printf("Reading NASCONIFG\n");

	ret = vtq_usim_read_nasconfig(&nasconfig, 1024);
	if (ret < 0) {
		printf("failed to GET nas confguration from USIM\n");
	} else {
		printf("NAS Config\n");
		printf("Signalling priority :");
		hex_dump(nasconfig.signalling_priority.value,
						nasconfig.signalling_priority.length);
		printf("nmo_i_behaviour:");
		hex_dump(nasconfig.nmo_i_behaviour.value,
					nasconfig.nmo_i_behaviour.length);
		printf("attach_with_imsi:");
		hex_dump(nasconfig.attach_with_imsi.value,
					nasconfig.attach_with_imsi.length);
		printf("min_p_search_timer:");
		hex_dump(nasconfig.min_p_search_timer.value,
					nasconfig.min_p_search_timer.length);
		printf("ext_access_barring:");
		hex_dump(nasconfig.ext_access_barring.value,
					nasconfig.ext_access_barring.length);
		printf("timer_t3245_behaviour:");
		hex_dump(nasconfig.timer_t3245_behaviour.value,
					nasconfig.timer_t3245_behaviour.length);
		printf("o_nas_signalling_low_priority:");
		hex_dump(nasconfig.o_nas_signalling_low_priority.value,
					nasconfig.o_nas_signalling_low_priority.length);
		printf("o_extended_access_barring:");
		hex_dump(nasconfig.o_extended_access_barring.value,
					nasconfig.o_extended_access_barring.length);
		printf("fast_first_higher_priority_plmn:");
		hex_dump(nasconfig.fast_first_higher_priority_plmn.value,
					nasconfig.fast_first_higher_priority_plmn.length);
		printf("eutra_disabling_capable:");
		hex_dump(nasconfig.eutra_disabling_capable.value,
					nasconfig.eutra_disabling_capable.length);
		printf("sm_retry_waittime:");
		hex_dump(nasconfig.sm_retry_waittime.value,
					nasconfig.sm_retry_waittime.length);
		printf("sm_retry_atrat_change :");
		hex_dump(nasconfig.sm_retry_atrat_change.value,
					nasconfig.sm_retry_atrat_change.length);
		printf("default_dcn_id :");
		hex_dump(nasconfig.default_dcn_id.value,
					nasconfig.default_dcn_id.length);
		printf("e_data_reporting_allowed:");
		hex_dump(nasconfig.e_data_reporting_allowed.value,
					nasconfig.e_data_reporting_allowed.length);
	}

	free(nasconfig.signalling_priority.value);
	free(nasconfig.nmo_i_behaviour.value);
	free(nasconfig.attach_with_imsi.value);
	free(nasconfig.min_p_search_timer.value);
	free(nasconfig.ext_access_barring.value);
	free(nasconfig.timer_t3245_behaviour.value);
	free(nasconfig.o_nas_signalling_low_priority.value);
	free(nasconfig.o_extended_access_barring.value);
	free(nasconfig.fast_first_higher_priority_plmn.value);
	free(nasconfig.eutra_disabling_capable.value);
	free(nasconfig.sm_retry_waittime.value);
	free(nasconfig.sm_retry_atrat_change.value);
	free(nasconfig.default_dcn_id.value);
	free(nasconfig.e_data_reporting_allowed.value);
}

void read_suci(void)
{
	int ret = -1;
	vtq_usim_suci_calc_info_t suci_calc_info;
	vtq_usim_suci_t suci;
	int i = 0;
	uint8_t buffer[256];

	printf("Reading SUCI\n");
	ret = vtq_usim_read_check_suci();
	if (ret == 0) {
		printf("suci to be generated in ME\n");
		ret = vtq_usim_read_suci_calc_info(&suci_calc_info,
							1024);
		if (debug) {
			printf("SUCI calc info\n");
			printf("number of protection scheme id's %d\n",
							suci_calc_info.num_protection_schemes);
			for (i = 0; i < suci_calc_info.num_protection_schemes; i++) {
				printf("protection scheme id %d %x\n", i,
							suci_calc_info.num_protection_schemes);
				printf("home network public id%d %x\n", i,
							suci_calc_info.home_nw_public_key_id[i]);
				printf("home network public key len%d %x\n", i,
							suci_calc_info.home_nw_pub_key_len[i]);
				printf("home network public key id%d:", i);
				hex_dump(suci_calc_info.home_nw_public_key[i],
							suci_calc_info.home_nw_pub_key_len[i]);
			}
		}
	} else {
		printf("usci is available in usim\n");
		memset(buffer, 0, sizeof(buffer));
		suci.value = buffer;
		vtq_usim_read_suci(&suci, 1024);
		printf("suci length: %d\n", suci.length);
		printf("suci value:");
		hex_dump(suci.value, suci.length);
	}
}

void read_op_plmn(void)
{
	vtq_usim_op_plmn_list_t op_plmn;
	int ret = -1;

	printf("%s Reading opertor plmn list\n", __func__);
	ret = vtq_usim_read_op_plmn(&op_plmn);
	if (ret == 0) {
		printf("plmnid:");
		hex_dump(op_plmn.plmn.plmn_id, 3);
		printf("start tac:");
		hex_dump(op_plmn.start_tac, 3);
		printf("end tac:");
		hex_dump(op_plmn.end_tac, 3);
		printf("pnnid:%02x", op_plmn.pnn_id);
	} else
		printf("Reading operator plmn failed\n");

}

void read_rid(void)
{
	int ret = -1;
	unsigned short rid = 0;

	ret = vtq_usim_read_rid(&rid);
	if (ret == 0)
		printf("RID value :%x\n", rid);
	else
		printf("Readin rid failed\n");
}

void write_rid(unsigned short rid)
{
	int ret = -1;

	ret = vtq_usim_write_rid(rid);
	if (ret == 0)
		printf("RID value :%x written successfully\n", rid);
	else
		printf("RID value :%x written failed\n", rid);
}

void read_imsi(void)
{
	int ret = -1;
	vtq_usim_imsi_t imsi;

	ret = vtq_usim_read_imsi(&imsi);
	if (ret == 0) {
		printf("imsi length :%d\n", imsi.length);
		printf("imsi value:");
		hex_dump(imsi.value, imsi.length);
	} else {
		printf("Reading imsi failed\n");
	}
}

void read_hplmnact(void)
{
	int ret = -1;
	int count = 0;
	vtq_usim_hplmnact_t hplmn_act[USIM_PLMN_MAX_LENGTH];
	int i = 0;

	ret = vtq_usim_read_hplmnact_list_count(&count);
	if (ret == 0) {
		printf("Number of hplmn id's %d\n", count);
		ret = vtq_usim_read_hplmnact(hplmn_act, count);
		if (ret == 0) {
			for (i = 0; i < count; i++) {
				printf("hplmnid%d:", i);
				hex_dump(hplmn_act[i].plmn.plmn_id, 3);
				printf("hplmnact id%d %x\n", i,
						hplmn_act[i].act_id);
			}
		} else {
			printf("Reading hplmnact info failed\n");
		}

	} else {
		printf("reading hplmnact count failed\n");
	}
}

void read_ehplmn(void)
{
	int ret = -1;
	int count = 0;
	vtq_usim_ehplmn_t eplmn[USIM_PLMN_MAX_LENGTH];
	int i = 0;

	ret = vtq_usim_read_ehplmn_list_count(&count);
	if (ret == 0) {
		printf("Number of equi plmn id's %d\n", count);
		ret = vtq_usim_read_ehplmn(eplmn, count);
		if (ret == 0) {
			for (i = 0; i < count; i++) {
				printf("eplmnid%d:", i);
				hex_dump(eplmn[i].plmn.plmn_id, 3);
			}
		} else {
			printf("Reading eplmn info failed\n");
		}

	} else {
		printf("reading eplmn count failed\n");
	}
}

void read_user_plmnact(void)
{
	int ret = -1;
	int count = 0;
	vtq_usim_uplmnact_t uplmn_act[USIM_PLMN_MAX_LENGTH];
	int i = 0;

	ret = vtq_usim_read_user_plmnact_list_count(&count);
	if (ret == 0) {
		printf("Number of user plmn id's %d\n", count);
		ret = vtq_usim_read_uplmnact(uplmn_act, count);
		if (ret == 0) {
			for (i = 0; i < count; i++) {
				printf("uplmnid%d:", i);
				hex_dump(uplmn_act[i].plmn.plmn_id, 3);
				printf("ulmnact id%d %x\n", i,
						uplmn_act[i].act_id);
			}
		} else {
			printf("Reading eplmn info failed\n");
		}

	} else {
		printf("reading eplmn count failed\n");
	}
}

void read_op_plmnact(void)
{
	int ret = -1;
	int count = 0;
	vtq_usim_oplmnact_t oplmn_act[USIM_PLMN_MAX_LENGTH];
	int i = 0;
	int j = 0;

	ret = vtq_usim_read_opc_plmnact_list_count(&count);
	if (ret == 0) {
		printf("Number of operator controlled  plmn id's %d\n", count);
		ret = vtq_usim_read_opc_plmnact(oplmn_act, count);
		if (ret == 0) {
			for (i = 0; i < count; i++) {
				printf("oplmnid%d:", i);
				hex_dump(oplmn_act[i].plmn.plmn_id, 3);
				printf("ulmnact id%d %02x\n", j,
						oplmn_act[i].act_id);
			}
		} else {
			printf("Reading oplmn act info failed\n");
		}

	} else {
		printf("reading oplmn act count failed\n");
	}
}

void read_fplmn(void)
{   int count = 0, i = 0;
	vtq_usim_fplmn_t frbplmn[USIM_PLMN_MAX_LENGTH];
	int ret = -1;

	printf("%s Reading forbidden plmn list\n", __func__);
	ret = vtq_usim_read_forbidden_plmn_list_count(&count);
	if (ret == 0) {
		ret = vtq_usim_read_forbidden_plmns(frbplmn, count);
		if (ret == 0) {
			printf("number of fplmn id's:%d\n", count);
			for (i = 0; i < count; i++) {
				hex_dump(frbplmn[i].plmn.plmn_id, 3);
			}
		} else
			printf("Reading forbidden plmn failed\n");

	} else
		printf("Reading fplmn failed\n");

}

void append_fplmn(unsigned char *fplmnid)
{
	int ret;
	vtq_usim_fplmn_t frbplmn[1];

	memcpy(frbplmn[0].plmn.plmn_id, fplmnid, 3);
	ret = vtq_usim_append_forbidden_plmns(frbplmn, 1);
	if (ret < 0) {
		printf("appending fplmn id failed\n");
	}
}
void delete_fplmn(unsigned char *fplmnid)
{
	int ret;
	vtq_usim_fplmn_t frbplmn;

	memcpy(frbplmn.plmn.plmn_id, fplmnid, 3);
	ret = vtq_usim_delete_forbidden_plmns(&frbplmn, 1);
	if (ret < 0) {
		printf("appending fplmn id failed\n");
	}
}

void read_plmn_nn(void)
{

	int ret = -1;
	unsigned char fullname_buf[256];
	unsigned char shortname_buf[256];
	vtq_usim_plmn_nn_t plmn_nn;

	plmn_nn.full_name.value = fullname_buf;
	plmn_nn.short_name.value = shortname_buf;

	printf("Reading plmn network name\n");
	ret = vtq_usim_read_plmn_nn(&plmn_nn, 1024);
	if (ret == 0) {
		if (plmn_nn.full_name.tag == PLMN_NN_FULLNAME_TAG) {
			printf("full name len:%d\n", plmn_nn.full_name.length);
		printf("full name:");
			hex_dump(plmn_nn.full_name.value,
						plmn_nn.full_name.length);
		}

		if (plmn_nn.short_name.tag == PLMN_NN_SHORTNAME_TAG) {
			printf("short name len:%d\n",
						plmn_nn.short_name.length);
		printf("short name:\n");
			hex_dump(plmn_nn.short_name.value,
						plmn_nn.short_name.length);
		}
	} else {
		printf("Reading plmn nn failed\n");
	}
}

void read_uac(void)
{
	int ret = -1;
	vtq_usim_uac_t uac;

	printf("Reading UAC\n");

	ret = vtq_usim_read_uac(&uac);
	if (ret == 0) {
		printf("UAC id :\n");
		hex_dump(uac.uac_acc_id, 4);
	} else {
		printf("Reading uac failed\n");
	}

}

void write_uac(unsigned char *uac_val)
{
	int ret = -1;
	vtq_usim_uac_t uac;

	printf("Writing UAC\n");
	memcpy(uac.uac_acc_id, uac_val, 4);
	ret = vtq_usim_write_uac(&uac);
	if (ret == 0) {
		printf("UAC accid:%x %x %x %x write success\n",
				uac.uac_acc_id[0], uac.uac_acc_id[1],
				uac.uac_acc_id[2], uac.uac_acc_id[3]);
	} else {
		printf("UAC accid:%x %x %x %x write failed\n",
				uac.uac_acc_id[0], uac.uac_acc_id[1],
				uac.uac_acc_id[2], uac.uac_acc_id[3]);
	}

}

void read_acc(void)
{
	int ret = -1;
	vtq_usim_acc_t acc_id;

	printf("Reading ACC id\n");
	ret = vtq_usim_read_acc(&acc_id);
	if (ret == 0) {
		printf("ACC id:\n");
		hex_dump(acc_id.acc, 2);
	} else {
		printf(" Reading ACC failed\n");
	}

}

void read_5gs_loc_info(void)
{
	int ret = -1;
	vtq_usim_5gs_loc_info_t loc_info_data;

	printf("Reading 5gs location info\n");
	ret = vtq_usim_read_5gs_loc_info(&loc_info_data);
	if (ret == 0) {
		printf("guti:");
		hex_dump(loc_info_data.guti, 13);
		printf("plmn id:");
		hex_dump(loc_info_data.tai.plmn.plmn_id, 3);
		printf("tac:\n");
		hex_dump(loc_info_data.tai.tac, 3);
		printf("status update:%x\n", loc_info_data.status_update);
	} else {
		printf("Reading 5gs location failed\n");
	}

}

void write_5gs_loc_info(unsigned char *guti, unsigned char *tac,
	unsigned char *plmnid)
{
	int ret = -1;
	vtq_usim_5gs_loc_info_t loc_info_data;

	memcpy(loc_info_data.guti, guti, 13);
	memcpy(loc_info_data.tai.tac, tac, 3);
	memcpy(loc_info_data.tai.plmn.plmn_id, plmnid, 3);
	loc_info_data.status_update = 0;

	ret = vtq_usim_write_5gs_loc_info(&loc_info_data);
	if (ret == 0) {
		printf("writing 5gs location info success\n");
		printf("guti:");
		hex_dump(guti, 13);
		printf("tac:");
		hex_dump(tac, 3);
		printf("plmn_id:");
		hex_dump(plmnid, 3);
	} else {
		printf("writing 5gs location info failed\n");
		printf("guti:");
		hex_dump(guti, 13);
		printf("tac:");
		hex_dump(tac, 3);
		printf("plmn_id:");
		hex_dump(plmnid, 3);
	}
}


void read_nassecctx(void)
{
	int ret = -1;
	vtq_usim_nas_security_ctx_t nas_sec_context;

	nas_sec_context.ngksi.value = (uint8_t *) malloc(1);
	nas_sec_context.kamf.value = (uint8_t *) malloc(32);
	nas_sec_context.ul_nascount.value = (uint8_t *) malloc(4);
	nas_sec_context.dl_nascount.value = (uint8_t *) malloc(4);
	nas_sec_context.int_enc_id.value = (uint8_t *) malloc(1);
	nas_sec_context.eps_int_enc_id.value = (uint8_t *) malloc(1);

	printf("Reading nas security context\n");
	ret = vtq_usim_read_nas_sec_ctx(&nas_sec_context,
				1024);
	if (ret == 0) {
		if ((nas_sec_context.ngksi.length > 0) &&
				(nas_sec_context.ngksi.length <= 1)) {
			printf("ksi:\n");
			hex_dump(nas_sec_context.ngksi.value,
					nas_sec_context.ngksi.length);
		}

		if ((nas_sec_context.kamf.length > 0) &&
				(nas_sec_context.kamf.length <= 32)) {
			printf("kamf:");
			hex_dump(nas_sec_context.kamf.value,
					nas_sec_context.kamf.length);
		}

		if ((nas_sec_context.ul_nascount.length > 0) &&
				(nas_sec_context.ul_nascount.length <= 4)) {
			printf("uplink nas count:");
			hex_dump(nas_sec_context.ul_nascount.value,
					nas_sec_context.ul_nascount.length);
		}

		if ((nas_sec_context.dl_nascount.length > 0) &&
				(nas_sec_context.dl_nascount.length <= 4)) {
			printf("downlink nas count:");
			hex_dump(nas_sec_context.dl_nascount.value,
					nas_sec_context.dl_nascount.length);
		}

		if ((nas_sec_context.int_enc_id.length > 0) &&
				(nas_sec_context.int_enc_id.length <= 1)) {
			printf("int enc algo id:\n");
			hex_dump(nas_sec_context.int_enc_id.value,
					nas_sec_context.int_enc_id.length);
		}

		if ((nas_sec_context.eps_int_enc_id.length > 0) &&
				(nas_sec_context.eps_int_enc_id.length <= 1)) {
			printf("eps int enc algo id:\n");
			hex_dump(nas_sec_context.eps_int_enc_id.value,
					nas_sec_context.eps_int_enc_id.length);
		}

	} else {
		printf("Reading nas security context failed\n");
	}

	free(nas_sec_context.ngksi.value);
	free(nas_sec_context.kamf.value);
	free(nas_sec_context.ul_nascount.value);
	free(nas_sec_context.dl_nascount.value);
	free(nas_sec_context.int_enc_id.value);
	free(nas_sec_context.eps_int_enc_id.value);

}

void read_nas_auth_keys(void)
{
	int ret = -1;
	vtq_usim_nas_auth_keys_t nas_auth_keys;

	nas_auth_keys.kausf.value = (uint8_t *)malloc(32);
	nas_auth_keys.kseaf.value = (uint8_t *)malloc(32);

	ret = vtq_usim_read_nas_auth_keys(&nas_auth_keys, 1024);
	if (ret == 0) {
		printf("Reading nas authentication keys successful\n");
		if (nas_auth_keys.kausf.tag == USIM_NAS_AUTH_KEY_AUSF_TAG) {
			printf("kausf tag:%x\n", nas_auth_keys.kausf.tag);
			printf("kausf length:%d\n", nas_auth_keys.kausf.length);
		printf("kausf value:");
			hex_dump(nas_auth_keys.kausf.value,
						nas_auth_keys.kausf.length);
		}

		if (nas_auth_keys.kseaf.tag == USIM_NAS_AUTH_KEY_SEAF_TAG) {
			printf("kseaf tag:%x\n", nas_auth_keys.kseaf.tag);
			printf("kseaf length:%d\n", nas_auth_keys.kseaf.length);
	printf("kseaf value:");
			hex_dump(nas_auth_keys.kseaf.value,
						nas_auth_keys.kseaf.length);
		}
	} else {
		printf("Reading nas authentication keys failed\n");
	}

}

void write_nas_auth_keys(unsigned char *kseaf, unsigned char *kausf)
{
	int ret = -1;
	vtq_usim_nas_auth_keys_t nas_auth_keys;

	nas_auth_keys.kausf.tag = 0x80;
	nas_auth_keys.kausf.length = 32;
	nas_auth_keys.kausf.value = malloc(32);
	memcpy(nas_auth_keys.kausf.value, kausf, 32);
	nas_auth_keys.kseaf.tag = 0x81;
	nas_auth_keys.kseaf.length = 32;
	nas_auth_keys.kseaf.value = malloc(32);
	memcpy(nas_auth_keys.kseaf.value, kseaf, 32);

	ret = vtq_usim_write_nas_auth_keys(&nas_auth_keys);
	if (ret < 0) {
		printf("ausf key:\n");
		hex_dump(nas_auth_keys.kausf.value, 32);
		printf("seaf key\n");
		hex_dump(nas_auth_keys.kseaf.value, 32);
		printf("Writing nas auh keys failed\n");
	} else {
		printf("ausf key:\n");
		hex_dump(nas_auth_keys.kausf.value, 32);
		printf("seaf key\n");
		hex_dump(nas_auth_keys.kseaf.value, 32);
		printf("Writing nas auth keys success\n");
	}

	free(nas_auth_keys.kseaf.value);
	free(nas_auth_keys.kausf.value);
}


void write_nassecctx(unsigned char *ngksi, unsigned char *kamf,
	unsigned char *ul_nas, unsigned char *dl_nas, unsigned char *int_enc_id,
	unsigned char *eps_int_enc_id)
{
	int ret = -1;
	vtq_usim_nas_security_ctx_t nas_sec_context;

	nas_sec_context.nas_sec_ctx_tag = 0xA0;

	nas_sec_context.ngksi.tag = 0x80;
	nas_sec_context.ngksi.length = 1;
	nas_sec_context.ngksi.value = malloc(1);
	*(nas_sec_context.ngksi.value) = *ngksi;

	nas_sec_context.kamf.tag = 0x81;
	nas_sec_context.kamf.length = 32;
	nas_sec_context.kamf.value = malloc(32);
	memcpy(nas_sec_context.kamf.value, kamf, 32);

	nas_sec_context.ul_nascount.tag = 0x82;
	nas_sec_context.ul_nascount.length = 4;
	nas_sec_context.ul_nascount.value = malloc(4);
	memcpy(nas_sec_context.ul_nascount.value, ul_nas, 4);

	nas_sec_context.dl_nascount.tag = 0x83;
	nas_sec_context.dl_nascount.length = 4;
	nas_sec_context.dl_nascount.value = malloc(4);
	memcpy(nas_sec_context.dl_nascount.value, dl_nas, 4);

	nas_sec_context.int_enc_id.tag = 0x84;
	nas_sec_context.int_enc_id.length = 1;
	nas_sec_context.int_enc_id.value = malloc(1);
	*(nas_sec_context.int_enc_id.value) = *int_enc_id;
	nas_sec_context.eps_int_enc_id.tag = 0x85;
	nas_sec_context.eps_int_enc_id.length = 1;
	nas_sec_context.eps_int_enc_id.value = malloc(1);
	*(nas_sec_context.eps_int_enc_id.value) = *eps_int_enc_id;

	printf("Writing nas security context\n");

	ret = vtq_usim_write_nas_sec_ctx(&nas_sec_context,
				1024);
	if (ret == 0) {
		printf("ksi:%x\n", *(nas_sec_context.ngksi.value));
		printf("kamf:");
		hex_dump(nas_sec_context.kamf.value,
				nas_sec_context.kamf.length);
		printf("uplink nas count:");
		hex_dump(nas_sec_context.ul_nascount.value,
					nas_sec_context.ul_nascount.length);
		printf("downlink nas count:");
		hex_dump(nas_sec_context.dl_nascount.value,
						nas_sec_context.dl_nascount.length);
		printf("int enc algo id:%x\n",
				*(nas_sec_context.int_enc_id.value));
		printf("eps int algo id:%x\n",
				*(nas_sec_context.eps_int_enc_id.value));
		printf("Writing nas security context success\n");
	} else {
		printf("ksi:%x\n", *(nas_sec_context.ngksi.value));
	printf("kamf:");
	hex_dump(nas_sec_context.kamf.value, nas_sec_context.kamf.length);
	printf("uplink nas count:");
	hex_dump(nas_sec_context.ul_nascount.value,
		    nas_sec_context.ul_nascount.length);
	printf("downlink nas count:");
	hex_dump(nas_sec_context.dl_nascount.value,
			nas_sec_context.dl_nascount.length);

		printf("int enc algo id:%x\n",
				*(nas_sec_context.int_enc_id.value));
		printf("eps int algo id:%x\n",
				*(nas_sec_context.eps_int_enc_id.value));

		printf("Writing nas security context failed\n");
	}
	free(nas_sec_context.ngksi.value);
	free(nas_sec_context.kamf.value);
	free(nas_sec_context.ul_nascount.value);
	free(nas_sec_context.dl_nascount.value);
	free(nas_sec_context.int_enc_id.value);
	free(nas_sec_context.eps_int_enc_id.value);
}

void invalid_nassecctx(void)
{
	int ret =  -1;

	ret = vtq_usim_invalidate_nas_sec_ctx();
	if (ret < 0)
		printf("Invalid nas secctx failed\n");
	else
		printf("Invalid nas secctx success\n");

}


void read_admin(void)
{
	int ret = -1;
	vtq_usim_admin_data_t admin_data;

	printf("Reading admin data\n");
	ret = vtq_usim_read_admin_data(&admin_data);
	if (ret == 0) {
		printf("op mode:%x\n", admin_data.ue_op_mode);
		printf("addnl info:");
		hex_dump(admin_data.addnl_info, 2);
		printf("mnc len :%x\n", admin_data.mnc_len);
	} else {
		printf("Reading admin data failed\n");
	}

}

void read_iccid(void)
{
	uint8_t buffer[256];
	int ret;
	int len = 10;

	ret = usim_read_transparent(USIM_ICCID_FILEID, &len, buffer, 0x3F00);
	if (ret == 0) {
		printf("icicid successful\n");
		hex_dump(buffer, len);
	} else {
		printf("%s:: read transparent failed\n", __func__);
	}
}

void read_imeisv(void)
{
	unsigned char buf[16];
	vtq_usim_imeisv_t imeisv;
	int ret;

	memset(buf, 0, 16);
	imeisv.length = 16;
	imeisv.value =  &buf[0];

	ret = vqa_read_usim_imeisv(&imeisv);
	if (ret == 0) {
		printf("imeisv successful\n");
		usim_dbg_chardump(LOG_INFO, buf, 16);
	} else
		printf("%s:: read imeisv failed\n", __func__);
}

void read_apncl(void)
{
	int ret = -1;
	vtq_usim_acl_t acl;
	int acl_max_len = 1024;
	int i = 0;

	for (i = 0; i < ACL_MAX_LIST; i++) {
		acl.tlv_list[i].value = (uint8_t *) malloc(32);
	}

	ret = vtq_usim_read_acl(&acl, acl_max_len);
	if (ret == 0) {
		for (i = 0; i < acl.count; i++) {
			printf("ACL %d TAG:%x\n", i, acl.tlv_list[i].tag);
			if (acl.tlv_list[i].tag == ACL_TAG) {
				printf("ACL %d Length:%d\n",
						i, acl.tlv_list[i].length);
				printf("ACL %d value:", i);
				hex_dump(acl.tlv_list[i].value,
						acl.tlv_list[i].length);
			}
		}
	} else {
		printf("Reading Access point name control list failed\n");
	}

	for (i = 0; i < ACL_MAX_LIST; i++) {
		free(acl.tlv_list[i].value);
	}

}

void authenticate(char *random_value, char *autn_value)
{
	int ret = -1;
	vtq_usim_lv_t autn;
	vtq_usim_lv_t random;
	vtq_usim_lv_t res;
	vtq_usim_lv_t ck;
	vtq_usim_lv_t ik;
	vtq_usim_lv_t auts;

	unsigned char auts_value[16];
	unsigned char res_value[16];
	unsigned char ck_value[16];
	unsigned char ik_value[16];

	hex_dump((unsigned char *)random_value, 16);
	hex_dump((unsigned char *)autn_value, 16);

	autn.value = (unsigned char *)malloc(16);
	memcpy(autn.value, autn_value, 16);

	random.value = (unsigned char *)malloc(16);
	memcpy(random.value, random_value, 16);

	auts.value = (unsigned char *)malloc(16);

	ck.value = (unsigned char *)malloc(16);

	ik.value = (unsigned char *)malloc(16);

	res.value = (unsigned char *)malloc(16);

	autn.length = 16;
	random.length = 16;
	auts.length = 16;
	res.length = 16;
	ck.length = 16;
	ik.length = 16;

	ret = vtq_usim_authenticate(&random, &autn, &auts, &res, &ck, &ik);
	if (ret == 0) {
		printf("Authentication is success\n");
		printf("RES:");
		hex_dump(res_value, sizeof(res_value));
		printf("CK:");
		hex_dump(ck_value, sizeof(ck_value));
		printf("IK:");
		hex_dump(ik_value, sizeof(ik_value));
	} else if (ret == 1) {
		printf("Authentication Failure\n");
	} else if (ret == 2) {
		printf("Synschronization Failure:\n");
		hex_dump(auts_value, sizeof(auts_value));
	}

	free(random.value);
	free(auts.value);
	free(autn.value);
	free(ck.value);
	free(ik.value);
	free(res.value);
}

void offchip_authenticate(char *key, char *opc, char *random_value,
		char *autn_value)
{
	int ret = -1;
	vtq_usim_lv_t autn;
	vtq_usim_lv_t random;
	vtq_usim_lv_t res;
	vtq_usim_lv_t ck;
	vtq_usim_lv_t ik;
	vtq_usim_lv_t auts;


	autn.value = (unsigned char *)malloc(16);
	memcpy(autn.value, autn_value, 16);

	random.value = (unsigned char *)malloc(16);
	memcpy(random.value, random_value, 16);

	auts.value = (unsigned char *)malloc(16);

	ck.value = (unsigned char *)malloc(16);

	ik.value = (unsigned char *)malloc(16);

	res.value = (unsigned char *)malloc(8);

	autn.length = 16;
	random.length = 16;
	auts.length = 16;
	res.length = 8;
	ck.length = 16;
	ik.length = 16;


	ret = vtq_usim_offchip_authenticate(key, opc, &random, &autn, &auts,
			&res, &ck, &ik);

	if (ret == 0) {
		printf("Authentication is success\n");
		printf("RES:");
		hex_dump(res.value, res.length);
		printf("CK:");
		hex_dump(ck.value, ck.length);
		printf("IK:");
		hex_dump(ik.value, ik.length);
	} else if (ret == 1) {
		printf("Authentication Failure\n");
	} else if (ret == 2) {
		printf("Synschronization Failure:\n");
		hex_dump(auts.value, auts.length);
	}

	free(random.value);
	free(auts.value);
	free(autn.value);
	free(ck.value);
	free(ik.value);
	free(res.value);

}

void read_ust_table(void)
{
	int ret = -1;
	unsigned char buffer[20];
	int len = 0;

	printf("Reading UST table\n");
	ret = vtq_usim_read_ust(buffer, &len);
	if (ret < 0) {
		printf("Reading ust table failed\n");
	}

	printf("Size of ust table in bytes:%d\n", len);
	printf("dump of usim service table\n");
	hex_dump(buffer, len);

}

int main(int argc, char *argv[])
{
	int ret = -1;
	vtq_usim_state_t state = 0;
	char buffer[256];
	unsigned short dir_fileid = 0;
	int command_support = 0;
	unsigned char plmn_id[3];
	char plmn_id_char[6];
	char *pos;
	char buf[256];
	unsigned short rid;
	char data[128];
	char random_val[16];
	char autn_val[16];
	char uac_value[4];
	char guti_value[13], tac_value[3];
	char seaf_val[32], ausf_val[32];
	char ksi_val, id_val, eps_id_val;
	char kamf_val[32], dl_val[4], ul_val[4];
	int i = 0;
	char key_val[16], opc_val[16];

	if (argc != 3) {
		printf("Usage ./usim_test_app <sim type> <debug>\n");
		return 0;
	}
	sim.type = atoi(argv[1]);

	vtq_usim_debug_set(atoi(argv[2]));

	ret = vtq_usim_register(sim.type);
	if (ret < 0)
		printf("USIM device registeration failied\n");
	else
		printf("USIM device registration successful\n");


	printf("\n##");
	printf("\n List of commands Supported");
	printf("\n help");
	printf("\n Command to read/writ SIM configuration <file name> <directory fileid>");
	printf("\n exit");

	while (1) {
		printf("\n##");
		memset(buf, '\0', sizeof(buf));
		memset(buffer, '\0', sizeof(buffer));

		if (scanf(" %256[^\n]", buf) == 0)
			continue;

		if (strcmp(buf, "exit") == 0) {
			break;
		}
		if (strcmp(buf, "help") == 0) {
			printf("\nnasconfig");
			printf("\nsuci");
			printf("\nimsi");
			printf("\niccid");
			printf("\nrid");
			printf("\nwrite_rid");
			printf("\nhplmnact");
			printf("\nehplmn");
			printf("\nuser_plmnact");
			printf("\nop_plmnact");
			printf("\nplmn_nn");
			printf("\nop_plmn");
			printf("\nfplmn");
			printf("\nappend_fplmn");
			printf("\ndelete_fplmn");
			printf("\nuac");
			printf("\nwrite_uac");
			printf("\nacc");
			printf("\nadmin");
			printf("\n5gs_location");
			printf("\nwrite_5gs_location");
			printf("\nnassecctx");
			printf("\nwrite_nassecctx");
			printf("\ninvalid_nassecctx");
			printf("\nimeisv");
			printf("\nnas_auth_keys");
			printf("\nwrite_nas_auth_keys");
			printf("\napncl");
			printf("\nauthenticate");
			printf("\nust_table");
			printf("\nusim_state");
			printf("\nsim_status");
			printf("\nstatus_data_dump");
			printf("\nusim_invalid");
			continue;
		}

		sscanf(buf, "%s %hx", buffer, &dir_fileid);
		sim.dir_fileid = dir_fileid;
		command_support = 0;
		if (strcmp(buffer, "power_on") == 0) {
			usim_poweron();
			command_support = 1;
		}

		if (strcmp(buffer, "power_off") == 0) {
			usim_poweroff();
			command_support = 1;
		}

		if (strcmp(buffer, "nasconfig") == 0) {
			read_nasconfig();
			command_support = 1;
		}

		if (strcmp(buffer, "suci") == 0) {
			read_suci();
			command_support = 1;
		}

		if (strcmp(buffer, "imsi") == 0) {
			read_imsi();
			command_support = 1;
		}

		if (strcmp(buffer, "iccid") == 0) {
			read_iccid();
			command_support = 1;
		}

		if (strcmp(buffer, "rid") == 0) {
			read_rid();
			command_support = 1;
		}

		if (strcmp(buffer, "write_rid") == 0) {
			printf("rid:");

			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 4) {
				printf("Incorrect rid, rid size should be 2bytes\n");
				continue;
			}
			sscanf(data, "%hx", &rid);
			write_rid(rid);
			command_support = 1;
		}

		if (strcmp(buffer, "hplmnact") == 0) {
			read_hplmnact();
			command_support = 1;
		}

		if (strcmp(buffer, "ehplmn") == 0) {
			read_ehplmn();
			command_support = 1;
		}

		if (strcmp(buffer, "user_plmnact") == 0) {
			read_user_plmnact();
			command_support = 1;
		}

		if (strcmp(buffer, "op_plmnact") == 0) {
			read_op_plmnact();
			command_support = 1;
		}

		if (strcmp(buffer, "plmn_nn") == 0) {
			read_plmn_nn();
			command_support = 1;
		}

		if (strcmp(buffer, "op_plmn") == 0) {
			read_op_plmn();
			command_support = 1;
		}

		if (strcmp(buffer, "fplmn") == 0) {
			read_fplmn();
			command_support = 1;
		}

		if (strcmp(buffer, "append_fplmn") == 0) {
			printf("plmn id:");
			if (scanf("%6s", plmn_id_char) == 0)
				continue;
			if (strlen(plmn_id_char) != 6) {
				printf("Incorrect plmn, plmn id size should be 3bytes\n");
				continue;
			}
			pos = plmn_id_char;
			sscanf(pos, "%2hhx", &plmn_id[0]);
			pos = pos+2;
			sscanf(pos, "%2hhx", &plmn_id[1]);
			pos = pos+2;
			sscanf(pos, "%2hhx", &plmn_id[2]);

			append_fplmn(plmn_id);
			command_support = 1;
		}

		if (strcmp(buffer, "delete_fplmn") == 0) {
			printf("plmn id:");
			if (scanf("%6s", plmn_id_char) == 0)
				continue;
			if (strlen(plmn_id_char) != 6) {
				printf("Incorrect plmn, plmn id size should be 3bytes\n");
				continue;
			}
			pos = plmn_id_char;
			sscanf(pos, "%2hhx", &plmn_id[0]);
			pos = pos+2;
			sscanf(pos, "%2hhx", &plmn_id[1]);
			pos = pos+2;
			sscanf(pos, "%2hhx", &plmn_id[2]);
			delete_fplmn(plmn_id);
			command_support = 1;
		}

		if (strcmp(buffer, "uac") == 0) {
			read_uac();
			command_support = 1;
		}

		if (strcmp(buffer, "write_uac") == 0) {
			printf("uac value:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 8) {
				printf("Incorrect uac value,  size should be 4bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 4; i++) {
				sscanf(pos, "%2hhx", &uac_value[i]);
				pos = pos + 2;
			}
			write_uac((unsigned char *)uac_value);
			command_support = 1;
		}

		if (strcmp(buffer, "acc") == 0) {
			read_acc();
			command_support = 1;
		}

		if (strcmp(buffer, "admin") == 0) {
			read_admin();
			command_support = 1;
		}

		if (strcmp(buffer, "5gs_location") == 0) {
			read_5gs_loc_info();
			command_support = 1;
		}

		if (strcmp(buffer, "write_5gs_location") == 0) {
			printf("guti value:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 26) {
				printf("Incorrect guti value, size should be 13bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 13; i++) {
				sscanf(pos, "%2hhx", &guti_value[i]);
				pos = pos + 2;
			}
			printf("tac:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 6) {
				printf("Incorrect tac value, tac size should be 3bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 3; i++) {
				sscanf(pos, "%2hhx", &tac_value[i]);
				pos = pos + 2;
			}

			printf("plmn id:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 6) {
				printf("Incorrect plmn, plmn id size should be 3bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 3; i++) {
				sscanf(pos, "%2hhx", &plmn_id[i]);
				pos = pos + 2;
			}

			write_5gs_loc_info((unsigned char *)guti_value,
					(unsigned char *)tac_value,
					(unsigned char *)plmn_id);
			command_support = 1;
		}

		if (strcmp(buffer, "nassecctx") == 0) {
			read_nassecctx();
			command_support = 1;
		}

		if (strcmp(buffer, "write_nassecctx") == 0) {
			printf("ngKSI:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 2) {
				printf("Incorrect ksi value, size should be 1byte\n");
				continue;
			}
			sscanf(data, "%2hhx", &ksi_val);
			printf("kamf:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 64) {
				printf("Incorrect key value, size should be 32bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 32; i++) {
				sscanf(pos, "%2hhx", &kamf_val[i]);
				pos = pos + 2;
			}

			printf("uplink count:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 8) {
				printf("Incorrect ul nas value, size should be 4bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 4; i++) {
				sscanf(pos, "%2hhx", &ul_val[i]);
				pos = pos + 2;
			}

			printf("down link count:\n");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 8) {
				printf("Incorrect dl nas value, size should be 4bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 4; i++) {
				sscanf(pos, "%2hhx", &dl_val[i]);
				pos = pos + 2;
			}

			printf("nas integrity and encryption id:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 2) {
				printf("Incorrect nas integrity and encryption id value, size should be 1byte\n");
				continue;
			}
			sscanf(data, "%2hhx", &id_val);

			printf("eps nas integrity and encryption id:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 2) {
				printf("Incorrect eps nas integrity and encryption id value, size should be 1byte\n");
				continue;
			}
			sscanf(data, "%2hhx", &eps_id_val);

			write_nassecctx((unsigned char *)&ksi_val,
					(unsigned char *)kamf_val,
					(unsigned char *)ul_val,
					(unsigned char *)dl_val,
					(unsigned char *)&id_val,
					(unsigned char *)&eps_id_val);
			command_support = 1;
		}

		if (strcmp(buffer, "invalid_nassecctx") == 0) {
			invalid_nassecctx();
			command_support = 1;
		}

		if (strcmp(buffer, "imeisv") == 0) {
			read_imeisv();
			command_support = 1;
		}

		if (strcmp(buffer, "nas_auth_keys") == 0) {
			read_nas_auth_keys();
			command_support = 1;
		}

		if (strcmp(buffer, "write_nas_auth_keys") == 0) {
			printf("seaf key value:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 64) {
				printf("Incorrect key value,  size should be 32bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 32; i++) {
				sscanf(pos, "%2hhx", &seaf_val[i]);
				pos = pos + 2;
			}

			printf("ausf key value:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 64) {
				printf("Incorrect key value,  size should be 32bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 32; i++) {
				sscanf(pos, "%2hhx", &ausf_val[i]);
				pos = pos + 2;
			}

			write_nas_auth_keys((unsigned char *)seaf_val,
					(unsigned char *)ausf_val);
			command_support = 1;
		}

		if (strcmp(buffer, "apncl") == 0) {
			read_apncl();
			command_support = 1;
		}

		if (strcmp(buffer, "authenticate") == 0) {
			printf("random value:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 32) {
				printf("Incorrect random value, size should be 16bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 16; i++) {
				sscanf(pos, "%2hhx", &random_val[i]);
				pos = pos + 2;
			}
			printf("autn value:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 32) {
				printf("Incorrect autn value, size should be 16bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 16; i++) {
				sscanf(pos, "%2hhx", &autn_val[i]);
				pos = pos + 2;
			}
			authenticate(random_val, autn_val);
			command_support = 1;
		}

		if (strcmp(buffer, "ust_table") == 0) {
			read_ust_table();
			command_support = 1;
		}

		if (strcmp(buffer, "usim_state") == 0) {
			state = vtq_usim_check_status();
			printf("present sim state is %d\n", state);
			command_support = 1;
		}

		if (strcmp(buffer, "sim_status") == 0) {
			state = vtq_usim_get_cur_status();
			printf("current sim status is %d\n", state);
			command_support = 1;

		}

		if (strcmp(buffer, "status_data_dump") == 0) {
			vtq_usim_status_data_dump();
			command_support = 1;
		}

		if (strcmp(buffer, "usim_invalid") == 0) {
			vtq_usim_invalidate();
			command_support = 1;
		}

		if (strcmp(buffer, "offchip_auth") == 0) {

			printf("key value:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 32) {
				printf("Incorrect key value, size should be 16bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 16; i++) {
				sscanf(pos, "%2hhx", &key_val[i]);
				pos = pos + 2;
			}

			printf("opc value:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 32) {
				printf("Incorrect opc value, size should be 16bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 16; i++) {
				sscanf(pos, "%2hhx", &opc_val[i]);
				pos = pos + 2;
			}

			printf("random value:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 32) {
				printf("Incorrect random value, size should be 16bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 16; i++) {
				sscanf(pos, "%2hhx", &random_val[i]);
				pos = pos + 2;
			}
			printf("autn value:");
			if (scanf("%128s", data) == 0)
				continue;
			if (strlen(data) != 32) {
				printf("Incorrect autn value, size should be 16bytes\n");
				continue;
			}
			pos = data;
			for (i = 0; i < 16; i++) {
				sscanf(pos, "%2hhx", &autn_val[i]);
				pos = pos + 2;
			}

			offchip_authenticate(key_val, opc_val, random_val,
					autn_val);
			command_support = 1;
		}

		if (!command_support)
			printf("This command %s is not supported\n", buffer);
	}

	vtq_usim_deregister();
	return 0;
}
