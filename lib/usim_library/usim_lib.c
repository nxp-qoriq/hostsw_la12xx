/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2021-2022 NXP
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include "aka_functions.h"
#include "usim_library.h"
#include "usim_ioctl.h"
#include "usim_apdu.h"
#include "usim_event.h"

#define USIM_DEVICE "/dev/usimdev0"
#define MEM_DEVICE "/dev/mem"
#define FLASH_DEVICE "/dev/mtd0"

int usim_fd;
int mem_fd;
int flash_fd;

extern sim_t sim;
int debug;
uint32_t imeisv_flash_offset;
event_handle_t event_handle;

typedef struct {
	uint8_t tag;
	uint8_t length;
	uint8_t value;
} tlv_t;

typedef struct {
	uint8_t plmn_id[3];
	uint16_t act_id;
} vqa_plmnact_t;


typedef struct {
	uint8_t plmn_id[3];
} vqa_plmn_t;

vqa_plmnact_t hplmnact[USIM_PLMN_MAX_COUNT];
vqa_plmn_t eqhplmn[USIM_PLMN_MAX_COUNT];
vqa_plmnact_t uplmnact[USIM_PLMN_MAX_COUNT];
vqa_plmnact_t oplmnact[USIM_PLMN_MAX_COUNT];
vqa_plmn_t forbplmn[USIM_PLMN_MAX_COUNT];

typedef struct {
	vtq_usim_fplmn_t *fplmn;
	int count;
} vqa_fplmn_list_t;

typedef struct {
	vtq_usim_lv_t rand;
	vtq_usim_lv_t autn;
	vtq_usim_lv_t auts;
	vtq_usim_lv_t res;
	vtq_usim_lv_t ck;
	vtq_usim_lv_t ik;
	int auth_status;
} vqa_usim_auth_t;

#define VQA_USIM_API_K_SIZE 16
#define VQA_USIM_API_AK_SIZE 6
#define VQA_USIM_API_SQN_SIZE VQA_USIM_API_AK_SIZE
#define VQA_USIM_API_SQNMS_SIZE VQA_USIM_API_SQN_SIZE
#define VQA_USIM_API_AMF_SIZE 2
#define VQA_USIM_API_XMAC_SIZE 8
#define VQA_USIM_API_MACS_SIZE VQA_USIM_API_XMAC_SIZE
/* List of the last used sequence numbers   */
#define VQA_USIM_API_SQN_LIST_SIZE  32
#define ONCHIP_AUTH 1

typedef struct {
	uint8_t usim_api_k[VQA_USIM_API_K_SIZE];
	uint8_t opc[16];
} vqa_usim_auth_keys_t;

typedef struct {
	/* Highest sequence number the USIM has ever accepted */
	uint8_t sqn_ms[VQA_USIM_API_SQNMS_SIZE];
	uint8_t n_sqns;
	uint32_t sqn[VQA_USIM_API_SQN_LIST_SIZE];
} vqa_usim_sqn_t;

vqa_usim_sqn_t usim_sqn_data;
vqa_usim_auth_keys_t auth_keys;


char lv_output[1024];

void hex_dump(unsigned char *buffer, int len)
{
	int i = 0;

	for (i = 0; i < len; i++) {
		if (i%16 == 0)
			printf("\n");

		if (i%4 == 0)
			printf(" ");

		printf("%02x", buffer[i]);
	}
	printf("\n");
}

void char_dump(unsigned char *buffer, int len)
{
	int i = 0;

	for (i = 0; i < len; i++) {
		if (i%16 == 0)
			printf("\n");

		printf(" %c", buffer[i]);
	}
	printf("\n");
}

int read_from_flash(int fd, off_t offset, size_t len, uint8_t *buf)
{
	if (offset != lseek(fd, offset, SEEK_SET)) {
		usim_dbg(LOG_ERROR, "%s:%d lseek failed offset:%p\n"
					, __func__, __LINE__, (void *)offset);
		return -1;
	}

	if (read(fd, buf, len) < 0) {
		usim_dbg(LOG_ERROR, "%s:%d read flash failed fd:%d\n"
					, __func__, __LINE__, fd);
		return -1;
	}

	return 0;
}

void vtq_usim_debug_set(int enable)
{
	debug = enable;

}

char *dump_tlv(vtq_usim_lv_t *lv)
{
	int i;
	int len_remain = 1024;
	int len = 0;
	int len_print = 0;

	for (i = 0; i < lv->length; i++) {
		len_print = snprintf(&lv_output[len], len_remain, "0x%x ",
						lv->value[i]);
		len += len_print;
		len_remain -= len_print;
	}

	return lv_output;
}

int vtq_usim_register(int sim_type)
{
	int ret;

	usim_fd = open(USIM_DEVICE, O_RDONLY);
	if (usim_fd < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Cannot open device file %s\n",
					__func__, __LINE__, USIM_DEVICE);
		return -1;
	}

	sim.type = sim_type;
	if (sim_type == 0)
		sim.dir_fileid = GSM_DF_GSM;
	else
		sim.dir_fileid = 0;

	imeisv_flash_offset = USIM_SPI_IMEISV_ADD;

	/* initialize usim event handling str */
	mem_fd = open(MEM_DEVICE, O_RDWR | O_SYNC);
	if (mem_fd == -1) {
		usim_dbg(LOG_ERROR, "%s:%d /dev/mem open failed ret:%d\n"
					, __func__, __LINE__, mem_fd);
		return -1;
	}

	usim_dbg(LOG_INFO, "/dev/mem opened.%s\n", "");

	ret = ioctl(usim_fd, USIM_IOCTL_GET_PHY_ADD,
				&event_handle.usim_ccsr_phy);
	if (ret) {
		usim_dbg(LOG_ERROR, "%s:%d ioctl for USIM physical failed err value:%d\n",
				__func__, __LINE__, ret);
		return -1;
	}

	usim_dbg(LOG_INFO, "%s:%d event_handle.usim_ccsr_phy=%p\n"
			, __func__, __LINE__, (void *)event_handle.usim_ccsr_phy);

	event_handle.usim_ccsr_map = mmap(0, MAP_SIZE,
			PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd,
			event_handle.usim_ccsr_phy & ~MAP_MASK);

	if (event_handle.usim_ccsr_map == (void *) -1) {
		usim_dbg(LOG_ERROR, "%s:%d memory map failed\n",
							__func__, __LINE__);
		return -1;

	}

	usim_dbg(LOG_INFO, "Memory mapped at address %p.\n",
					(void *)event_handle.usim_ccsr_map);

	event_handle.usim_regs =
			(struct usim_regs_map *) event_handle.usim_ccsr_map;

	atr_us(&event_handle);

	if (sim.type == GSM_SIM_TYPE) {
		ret = usim_select_gsm();
		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d select gsm failed\n",
					__func__, __LINE__);
			close(usim_fd);
			return -1;
		}
	} else if (sim.type == USIM_TYPE) {
		ret = usim_select_aid();
		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d select aid failed\n",
					__func__, __LINE__);
			close(usim_fd);
			return -1;
		}
	}

	return 0;
}

int vtq_usim_deregister(void)
{

	close(usim_fd);
	close(mem_fd);

	if (munmap(event_handle.usim_ccsr_map, MAP_SIZE) == -1) {
		usim_dbg(LOG_ERROR, "%s:%d memory unmap failed\n",
							__func__, __LINE__);
		return -1;
	}

	return 0;
}

int vtq_usim_read_nasconfig(vtq_usim_nasconfig_t *nasconfig, int nas_len)
{
	uint8_t *nas_data;
	int ret = -1;
	int len = 0;
	uint8_t *nas_data_start;

	nas_data = (uint8_t *)malloc(nas_len);
	if (nas_data == NULL) {
		usim_dbg(LOG_ERROR, "%s::%d Memory allocation failed\n",
					__func__, __LINE__);
		return -1;
	}

	ret = usim_read_transparent(USIM_NASCONFIG_FILEID, &len, nas_data, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		free(nas_data);
		return ret;
	}

	if (len > nas_len) {
		usim_dbg(LOG_ERROR, "%s::%d Total received memory len %d is less than total file len %d\n",
				__func__,
				__LINE__, nas_len, len);
		free(nas_data);
		return ret;
	}

	nas_data_start = nas_data;

	nasconfig->signalling_priority.tag = *nas_data;
	if (nasconfig->signalling_priority.tag == NAS_SIG_PRIO_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->signalling_priority.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->signalling_priority.value, nas_data,
				nasconfig->signalling_priority.length);
		nas_data = nas_data + nasconfig->signalling_priority.length;
	}


	nasconfig->nmo_i_behaviour.tag = *nas_data;
	if (nasconfig->nmo_i_behaviour.tag == NAS_NMO_BEH_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->nmo_i_behaviour.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->nmo_i_behaviour.value, nas_data,
				nasconfig->nmo_i_behaviour.length);
		nas_data = nas_data + nasconfig->nmo_i_behaviour.length;
	}

	nasconfig->attach_with_imsi.tag = *nas_data;
	if (nasconfig->attach_with_imsi.tag == NAS_AT_IMSI_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->attach_with_imsi.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->attach_with_imsi.value, nas_data,
				nasconfig->attach_with_imsi.length);
		nas_data = nas_data + nasconfig->attach_with_imsi.length;
	}

	nasconfig->min_p_search_timer.tag = *nas_data;
	if (nasconfig->min_p_search_timer.tag == NAS_MPST_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->min_p_search_timer.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->min_p_search_timer.value, nas_data,
				nasconfig->min_p_search_timer.length);
		nas_data = nas_data + nasconfig->min_p_search_timer.length;
	}

	nasconfig->ext_access_barring.tag = *nas_data;
	if (nasconfig->ext_access_barring.tag == NAS_EAB_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->ext_access_barring.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->ext_access_barring.value, nas_data,
				nasconfig->ext_access_barring.length);
		nas_data = nas_data + nasconfig->ext_access_barring.length;
	}

	nasconfig->timer_t3245_behaviour.tag = *nas_data;
	if (nasconfig->timer_t3245_behaviour.tag == NAS_TIM_BEH_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->timer_t3245_behaviour.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->timer_t3245_behaviour.value, nas_data,
				nasconfig->timer_t3245_behaviour.length);
		nas_data = nas_data + nasconfig->timer_t3245_behaviour.length;
	}

	nasconfig->o_nas_signalling_low_priority.tag = *nas_data;
	if (nasconfig->o_nas_signalling_low_priority.tag ==
			NAS_OD_SIG_PRIO_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->o_nas_signalling_low_priority.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->o_nas_signalling_low_priority.value,
			nas_data,
			nasconfig->o_nas_signalling_low_priority.length);
		nas_data = nas_data +
				nasconfig->o_nas_signalling_low_priority.length;
	}

	nasconfig->o_extended_access_barring.tag = *nas_data;
	if (nasconfig->o_extended_access_barring.tag == NAS_OD_EAB_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->o_extended_access_barring.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->o_extended_access_barring.value, nas_data,
				nasconfig->o_extended_access_barring.length);
		nas_data = nas_data +
				nasconfig->o_extended_access_barring.length;
	}

	nasconfig->fast_first_higher_priority_plmn.tag = *nas_data;
	if (nasconfig->fast_first_higher_priority_plmn.tag == NAS_FFHPS_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->fast_first_higher_priority_plmn.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->fast_first_higher_priority_plmn.value,
			nas_data,
			nasconfig->fast_first_higher_priority_plmn.length);
		nas_data = nas_data +
			nasconfig->fast_first_higher_priority_plmn.length;
	}

	nasconfig->eutra_disabling_capable.tag = *nas_data;
	if (nasconfig->eutra_disabling_capable.tag == NAS_DA_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->eutra_disabling_capable.length = *nas_data;
		memcpy(nasconfig->eutra_disabling_capable.value, nas_data,
			nasconfig->eutra_disabling_capable.length);
		nas_data = nas_data +
				nasconfig->eutra_disabling_capable.length;
	}

	nasconfig->sm_retry_waittime.tag = *nas_data;
	if (nasconfig->sm_retry_waittime.tag == NAS_SM_RWT_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->sm_retry_waittime.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->sm_retry_waittime.value, nas_data,
				nasconfig->sm_retry_waittime.length);
		nas_data = nas_data + nasconfig->sm_retry_waittime.length;
	}

	nasconfig->sm_retry_atrat_change.tag = *nas_data;
	if (nasconfig->sm_retry_atrat_change.tag == NAS_SW_RRAT_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->sm_retry_atrat_change.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->sm_retry_atrat_change.value, nas_data,
				nasconfig->sm_retry_atrat_change.length);
		nas_data = nas_data + nasconfig->sm_retry_atrat_change.length;
	}

	nasconfig->default_dcn_id.tag = *nas_data;
	if (nasconfig->default_dcn_id.tag == NAS_DCN_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->default_dcn_id.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->default_dcn_id.value, nas_data,
				nasconfig->default_dcn_id.length);
		nas_data = nas_data + nasconfig->default_dcn_id.length;
	}

	nasconfig->e_data_reporting_allowed.tag = *nas_data;
	if (nasconfig->e_data_reporting_allowed.tag == NAS_EDRA_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nasconfig->e_data_reporting_allowed.length = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;
		memcpy(nasconfig->e_data_reporting_allowed.value, nas_data,
				nasconfig->e_data_reporting_allowed.length);
		nas_data = nas_data +
				nasconfig->e_data_reporting_allowed.length;
	}

	free(nas_data_start);
	return 0;
}

int vtq_usim_read_check_suci(void)
{
	int status;
	uint8_t suci_buf[4];
	int suci_len = sizeof(suci_buf);

	status = usim_get_suci(suci_len, suci_buf);
	if (status < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Check suci status failed err value:%d\n",
					__func__, __LINE__,
					status);
	}

	return status;
}

int vtq_usim_read_suci(vtq_usim_suci_t *suci, int suci_len)
{
	int length = 0;
	int len_octets_count = 0, i = 0;
	uint8_t *suci_buf = NULL;
	int status = -1;

	suci_buf = malloc(suci_len);
	if (suci_buf == NULL) {
		usim_dbg(LOG_ERROR, "%s::%d Memory allocation failed\n",
				__func__, __LINE__);
		return -1;
	}

	status = usim_get_suci(suci_len, suci_buf);
	if (status < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Get SUCI failed err value:%d\n",
				__func__, __LINE__,
				status);
		free(suci_buf);
		return -1;
	}

	/*Length to be read as per The length is coded according
	 *to ISO/IEC 8825-1 [35]
	 */
	length = suci_buf[1];
	if (length > 127) {
		len_octets_count = (suci_buf[1] & 0x7F);

		for (i = 0; i < len_octets_count; i++) {
			length = length + suci_buf[i+2];
		}

	}

	if (suci_len < length) {
		usim_dbg(LOG_ERROR, "%s::%d Receved Memory len %d is less thantotal suci len %d\n",
					__func__,
					__LINE__, suci_len, length);
		free(suci_buf);
		return -1;
	}
	suci->length = length;
	memcpy(suci->value, suci_buf+2+len_octets_count, length);

	free(suci_buf);
	return 0;
}

int vtq_usim_read_suci_calc_info(vtq_usim_suci_calc_info_t *suci_calc_info,
		int suci_calc_info_len)
{
	uint8_t *buffer;
	int len_octet_count = 1;
	uint8_t key_index[16];
	uint8_t *hmn_pub_key_list = NULL;
	int octet_length_count = 0;
	int hmn_key_octet_len_cnt = 0;
	int hmn_pub_key_len = 0;
	int len = 0;
	int ret;
	int i = 0, j = 0, k = 0;
	unsigned char prot_id_tag;
	unsigned int prot_id_len = 0;
	unsigned char hmn_pub_list_tag;
	unsigned int hmn_pub_list_len = 0;
	unsigned int hmn_pub_id_tag;
	unsigned int hmn_pub_key_tag;

	buffer = malloc(suci_calc_info_len);
	if (buffer == NULL) {
		usim_dbg(LOG_ERROR, "%s::%d Memory allocation failed\n",
						__func__, __LINE__);
		return -1;
	}

	ret = usim_read_transparent(USIM_SUCI_CALC_INFO_FILEID, &len, buffer,
					USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
					__func__, __LINE__, ret);
		free(buffer);
		return ret;
	}

	if (suci_calc_info_len < len) {
		usim_dbg(LOG_ERROR, "%s::%d Receied memory size %d is less then total file length %d\n",
			__func__, __LINE__,
			suci_calc_info_len, len);
		free(buffer);
		return -1;
	}

	prot_id_tag = buffer[0];
	if (prot_id_tag == 0xA0) {
		prot_id_len = buffer[1];


		if (prot_id_len > 127) {
			len_octet_count = (buffer[1] & 0x7F);

			for (i = 0; i < len_octet_count; i++) {
				prot_id_len = prot_id_len + buffer[i+2];
			}
		}

		suci_calc_info->num_protection_schemes = prot_id_len/2;


		for (i = 0; i < suci_calc_info->num_protection_schemes; i = i+1) {
			suci_calc_info->protection_scheme_ids[i] = *(buffer +
					TAG_BYTE_SIZE +
					len_octet_count + i);

			key_index[i] = *(buffer + 1 + len_octet_count + i + 1);
			if (key_index[i] == 0) {
				suci_calc_info->home_nw_public_key_id[i] = 0;
				suci_calc_info->home_nw_pub_key_len[i] = 0;
			}
		}

		/*buffer + TAG + nnumber of protection scheme identifiers*/
		hmn_pub_key_list = buffer + TAG_BYTE_SIZE + len_octet_count +
						prot_id_len;

		hmn_pub_list_tag = hmn_pub_key_list[0];

		if (hmn_pub_list_tag == 0xA1) {
			hmn_pub_list_len = hmn_pub_key_list[1];

			if (hmn_pub_list_len > 127) {
				octet_length_count = hmn_pub_key_list[1] & 0x7F;

				for (i = 0; i < octet_length_count; i++) {

					hmn_pub_list_len = hmn_pub_list_len +
						hmn_pub_key_list[i+2];
				}
			}

			hmn_pub_key_list = hmn_pub_key_list + TAG_BYTE_SIZE +
							1 + octet_length_count;

			hmn_pub_id_tag = hmn_pub_key_list[0];

			for (i = 0; i < suci_calc_info->num_protection_schemes; i = i+1) {

				k = key_index[i];
				if (k != 0) {
					if (hmn_pub_id_tag == 0x80) {
						suci_calc_info->home_nw_public_key_id[k-1] =
							*(hmn_pub_key_list + TAG_BYTE_SIZE + LENGTH_BYTE_SIZE);
					}

					hmn_pub_key_list = hmn_pub_key_list + TAG_BYTE_SIZE + LENGTH_BYTE_SIZE + 1;

					hmn_pub_key_tag = hmn_pub_key_list[0];
					if (hmn_pub_key_tag == 0x81) {
						printf("home network public key\n");
						hmn_pub_key_len = *(hmn_pub_key_list + TAG_BYTE_SIZE);

						if (hmn_pub_key_len > 127) {

							hmn_key_octet_len_cnt = hmn_pub_key_list[1] & 0x7F;

							hmn_pub_key_len = 0;
							for (j = 0; j < hmn_key_octet_len_cnt; j++) {
								hmn_pub_key_len = hmn_pub_key_len +
									*(hmn_pub_key_list +
											1 + 1 + 1 + 1 + 1 + j);
							}
						}
						suci_calc_info->home_nw_pub_key_len[k-1] = hmn_pub_key_len;
						memcpy(suci_calc_info->home_nw_public_key[k-1],
								(hmn_pub_key_list + TAG_BYTE_SIZE + LENGTH_BYTE_SIZE +
								 hmn_key_octet_len_cnt), hmn_pub_key_len);
						hmn_pub_key_list = hmn_pub_key_list +
								TAG_BYTE_SIZE + hmn_key_octet_len_cnt + hmn_pub_key_len;
					}
				}
			}

		}
	}
	free(buffer);

	return 0;
}


int vtq_usim_read_rid(uint16_t *rid)
{
	uint8_t buffer[USIM_RID_LENGTH];
	int len = USIM_RID_LENGTH;
	int ret;

	ret = usim_read_transparent(USIM_RID_FILEID, &len, buffer,
						USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
					__func__, __LINE__, ret);
		return ret;
	}

	*rid = *(uint16_t *)(buffer);

	return 0;
}

int vtq_usim_read_imsi(vtq_usim_imsi_t *imsi)
{
	uint8_t buffer[USIM_IMSI_LENGTH + 1];
	int ret;
	int len = USIM_IMSI_LENGTH + 1;

	ret = usim_read_transparent(USIM_IMSI_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	imsi->length = buffer[0];

	/*skip length which of 1byte and copy data*/
	memcpy(imsi->value, (buffer+1), imsi->length);

	return 0;
}

int vqa_read_usim_imeisv(vtq_usim_imeisv_t *imeisv)
{
	flash_fd = open(FLASH_DEVICE, O_RDWR | O_SYNC);
	if (flash_fd == -1) {
		usim_dbg(LOG_ERROR, "%s:%d /dev/mtd open failed ret:%d\n"
					, __func__, __LINE__, flash_fd);
		goto err1;
	}

	usim_dbg(LOG_INFO, "/dev/mtd opened.%s\n", "");

	if (read_from_flash(flash_fd, imeisv_flash_offset, imeisv->length,
						imeisv->value) == -1) {
		usim_dbg(LOG_ERROR, "%s:%d Read IMEISV failed ret:%d\n"
					, __func__, __LINE__, flash_fd);
		goto err;
	}
	close(flash_fd);
	return 0;

err:
	close(flash_fd);
err1:
	return -1;


	return 0;
}

int vtq_usim_read_hplmnact_list_count(int *count)
{
	int ret;
	int len = 0;
	uint8_t buffer[USIM_PLMN_MAX_LENGTH];
	uint8_t *pbuffer;
	int i = 0, tot_count = 0, j = 0;
	unsigned char invalid_plmn[3] = {0xFF, 0xFF, 0xFF};

	ret = usim_read_transparent(USIM_HPLMNACT_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	if (len > USIM_PLMN_MAX_LENGTH) {
		len = USIM_PLMN_MAX_LENGTH;
	}

	tot_count = len/(USIM_PLMNID_LENGTH + USIM_ACTID_LENGTH);
	pbuffer = buffer;
	for (i = 0; i < tot_count; i++) {
		if (memcmp(pbuffer, invalid_plmn, 3) != 0) {
			memcpy(hplmnact[j].plmn_id, pbuffer,
						USIM_PLMNID_LENGTH);
			memcpy(&(hplmnact[j].act_id),
					pbuffer+USIM_PLMNID_LENGTH,
					USIM_ACTID_LENGTH);
			*count = *count + 1;
			j++;
		}
		pbuffer = pbuffer + USIM_PLMNID_LENGTH + USIM_ACTID_LENGTH;
	}
	return 0;
}

int vtq_usim_read_hplmnact(vtq_usim_hplmnact_t *hplmn, int count)
{
	int i = 0;

	for (i = 0; i < count; i++) {
		memcpy(hplmn->plmn.plmn_id, hplmnact[i].plmn_id,
				USIM_PLMNID_LENGTH);
		hplmn->act_id = hplmnact[i].act_id;
		hplmn = hplmn + 1;
	}

	return 0;
}

int vtq_usim_read_ehplmn_list_count(int *count)
{
	int ret;
	int len = 0;
	uint8_t buffer[USIM_PLMN_MAX_LENGTH];
	uint8_t *pbuffer;
	int i = 0, tot_count = 0, j = 0;
	unsigned char invalid_plmn[3] = {0xFF, 0xFF, 0xFF};


	ret = usim_read_transparent(USIM_EHPLMN_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	if (len > USIM_PLMN_MAX_LENGTH) {
		len = USIM_PLMN_MAX_LENGTH;
	}

	tot_count = len/USIM_PLMNID_LENGTH;
	pbuffer = buffer;
	for (i = 0; i < tot_count; i++) {
		if (memcmp(pbuffer, invalid_plmn, 3) != 0) {
			memcpy(eqhplmn[j].plmn_id, pbuffer, USIM_PLMNID_LENGTH);
			*count = *count + 1;
			j++;
		}
		pbuffer = pbuffer + USIM_PLMNID_LENGTH;
	}

	return 0;
}

int vtq_usim_read_ehplmn(vtq_usim_ehplmn_t *ehplmn, int count)
{
	int i = 0;

	for (i = 0; i < count; i++) {
		memcpy(ehplmn->plmn.plmn_id, eqhplmn[i].plmn_id,
				USIM_PLMNID_LENGTH);
		ehplmn = ehplmn + 1;
	}

	return 0;
}

int vtq_usim_read_user_plmnact_list_count(int *count)
{
	int ret;
	int len = 0;
	uint8_t buffer[USIM_PLMN_MAX_LENGTH];
	uint8_t *pbuffer;
	int i = 0, tot_count = 0, j = 0;
	unsigned char invalid_plmn[3] = {0xFF, 0xFF, 0xFF};

	ret = usim_read_transparent(USIM_UPLMNACT_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value :%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	if (len > USIM_PLMN_MAX_LENGTH) {
		len = USIM_PLMN_MAX_LENGTH;
	}

	tot_count = len/(USIM_PLMNID_LENGTH + USIM_ACTID_LENGTH);
	pbuffer = buffer;
	for (i = 0; i < tot_count; i++) {
		if (memcmp(pbuffer, invalid_plmn, 3) != 0) {
			memcpy(uplmnact[j].plmn_id, pbuffer,
						USIM_PLMNID_LENGTH);
			memcpy(&(uplmnact[j].act_id),
					pbuffer+USIM_PLMNID_LENGTH,
					USIM_ACTID_LENGTH);
			*count = *count + 1;
			j++;
		}
		pbuffer = pbuffer + USIM_PLMNID_LENGTH + USIM_ACTID_LENGTH;
	}

	return 0;
}

int vtq_usim_read_uplmnact(vtq_usim_uplmnact_t *uplmn_act, int count)
{
	int i = 0;

	for (i = 0; i < count; i++) {
		memcpy(uplmn_act->plmn.plmn_id, uplmnact[i].plmn_id,
					USIM_PLMNID_LENGTH);
		uplmn_act->act_id = uplmnact[i].act_id;
		uplmn_act = uplmn_act + 1;
	}

	return 0;
}

int vtq_usim_read_opc_plmnact_list_count(int *count)
{
	int ret;
	uint8_t buffer[USIM_PLMN_MAX_LENGTH];
	uint8_t *pbuffer;
	int i = 0;
	int len = 0, tot_count = 0, j = 0;
	unsigned char invalid_plmn[3] = {0xFF, 0xFF, 0xFF};

	ret = usim_read_transparent(USIM_OPCONTRLACT_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err val:%d\n",
					__func__, __LINE__, ret);
		return ret;
	}

	if (len > USIM_PLMN_MAX_LENGTH) {
		len = USIM_PLMN_MAX_LENGTH;
	}

	tot_count = len/(USIM_PLMNID_LENGTH + USIM_ACTID_LENGTH);
	pbuffer = buffer;
	for (i = 0; i < tot_count; i++) {
		if (memcmp(pbuffer, invalid_plmn, 3) != 0) {
			memcpy(oplmnact[j].plmn_id, pbuffer,
						USIM_PLMNID_LENGTH);
			memcpy(&(oplmnact[j].act_id),
				pbuffer+USIM_PLMNID_LENGTH,
				USIM_ACTID_LENGTH);
			*count = *count + 1;
			j++;
		}
		pbuffer = pbuffer + USIM_PLMNID_LENGTH + USIM_ACTID_LENGTH;
	}

	return 0;
}

int vtq_usim_read_opc_plmnact(vtq_usim_oplmnact_t *oplmn_act, int count)
{
	int i = 0;

	for (i = 0; i < count; i++) {
		memcpy(oplmn_act->plmn.plmn_id, oplmnact[i].plmn_id,
					USIM_PLMNID_LENGTH);
		oplmn_act->act_id = oplmnact[i].act_id;
		oplmn_act = oplmn_act + 1;
	}

	return 0;
}

int vtq_usim_read_op_plmn(vtq_usim_op_plmn_list_t *op_plmn_list)
{

	uint8_t buffer[USIM_OPPLMN_LENGTH];
	int ret;
	int len = USIM_OPPLMN_LENGTH;

	ret = usim_read_linearfixed(USIM_OPPLMN_FILEID, &len, buffer,
					USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	memcpy(op_plmn_list->plmn.plmn_id, buffer, USIM_PLMNID_LENGTH);

	memcpy(op_plmn_list->start_tac, buffer+USIM_PLMNID_LENGTH,
				USIM_TAC_LENGTH);

	memcpy(op_plmn_list->end_tac, buffer+USIM_PLMNID_LENGTH+USIM_TAC_LENGTH,
				USIM_TAC_LENGTH);

	op_plmn_list->pnn_id = buffer[USIM_PLMNID_LENGTH + 2*USIM_TAC_LENGTH];

	return 0;
}

int vtq_usim_read_forbidden_plmn_list_count(int *count)
{
	int ret;
	int len = 0;
	uint8_t buffer[USIM_PLMN_MAX_LENGTH];
	uint8_t *pbuffer;
	int i = 0, tot_count = 0, j = 0;
	unsigned char invalid_plmn[3] = {0xFF, 0xFF, 0xFF};

	ret = usim_read_transparent(USIM_FPLMN_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	if (len > USIM_PLMN_MAX_LENGTH) {
		len = USIM_PLMN_MAX_LENGTH;
	}

	tot_count = len/USIM_PLMNID_LENGTH;
	pbuffer = buffer;

	for (i = 0; i < tot_count; i++) {
		if (memcmp(pbuffer, invalid_plmn, 3) != 0) {
			memcpy(forbplmn[j].plmn_id, pbuffer,
						USIM_PLMNID_LENGTH);
			*count = *count + 1;
			j++;
		}
		pbuffer = pbuffer + USIM_PLMNID_LENGTH;
	}

	return 0;
}

int vtq_usim_read_forbidden_plmns(vtq_usim_fplmn_t *fplmn, int count)
{
	int i = 0;

	for (i = 0; i < count; i++) {
		memcpy(fplmn->plmn.plmn_id, forbplmn[i].plmn_id,
				USIM_PLMNID_LENGTH);
		fplmn = fplmn + 1;
	}

	return 0;
}

int vtq_usim_append_forbidden_plmns(vtq_usim_fplmn_t *fplmn, int count)
{
	int ret;
	uint8_t buffer[256];
	int len = 0;
	int i = 0;
	int lower_offset, higher_offset;
	unsigned char resp[3];//update record response data buffer;
	size_t sr_len;//update record response data length;

	ret = usim_read_transparent(USIM_FPLMN_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	lower_offset = len - USIM_PLMNID_LENGTH*count;
	higher_offset = 0;

	len = 0;
	memset(buffer, 0, sizeof(buffer));

	for (i = 0; i < count; i++) {
		memcpy(buffer + len, fplmn[i].plmn.plmn_id, USIM_PLMNID_LENGTH);
		len = len + USIM_PLMNID_LENGTH;
	}

	sr_len = sizeof(resp);
	ret = usim_update_binary(len, buffer, higher_offset, lower_offset,
			sr_len, resp);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Update binary failed err value: %d\n",
				__func__, __LINE__, ret);
	}

	if ((resp[1] != 0x90) && (resp[2] != 0x00)) {
		usim_dbg(LOG_ERROR, "%s:%d Update binary Returned unexpected status Status Word:%x %x\n",
				__func__,
				__LINE__, resp[1], resp[2]);
		return -1;
	}

	return 0;
}

int vtq_usim_delete_forbidden_plmns(vtq_usim_fplmn_t *fplmn, int count)
{
	int ret;
	uint8_t buffer[256];
	int len = 0;
	int i = 0, j = 0;
	uint8_t del_buffer[] = {0xFF, 0xFF, 0xFF};
	unsigned char resp[3];//update binary response data buffer;
	size_t sr_len;//update binary response data length;

	ret = usim_read_transparent(USIM_FPLMN_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	while (i < count) {
		for (j = 0; j < len; j = j+USIM_PLMNID_LENGTH) {
			if (memcmp(fplmn[i].plmn.plmn_id, buffer+j,
						USIM_PLMNID_LENGTH) == 0) {
				memcpy(buffer+j, del_buffer,
						USIM_PLMNID_LENGTH);
			}
		}
		i++;
	}

	sr_len = sizeof(resp);
	ret = usim_update_binary(len, buffer, 0, 0, sr_len, resp);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Update binary failed err value: %d\n",
				__func__, __LINE__, ret);
	}

	if ((resp[1] != 0x90) && (resp[2] != 0x00)) {
		usim_dbg(LOG_ERROR, "%s:%d Update binary Returned unexpected status Status Word:%x %x\n",
				__func__,
				__LINE__, resp[1], resp[2]);
		return -1;
	}

	event_handle.state_cur = USIM_STATE_INVALIDATED;

	return 0;
}

int vtq_usim_read_plmn_nn(vtq_usim_plmn_nn_t *plmn_nn, int plmn_nn_len)
{
	int len = 0;
	uint8_t *buffer;
	int ret;
	uint8_t *buffer_start;

	buffer = (uint8_t *)malloc(plmn_nn_len);
	if (buffer == NULL) {
		usim_dbg(LOG_ERROR, "%s::%d Memory allocation failed\n",
				__func__, __LINE__);
		return -1;
	}
	ret = usim_read_linearfixed(USIM_PLMN_NN_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read linear fixed failed err value:%d\n",
				__func__, __LINE__, ret);
		free(buffer);
		return ret;
	}

	if (len > plmn_nn_len) {
		usim_dbg(LOG_ERROR, "%s::%d Total received memory %d is less than total file len %d\n",
				__func__, __LINE__,
							plmn_nn_len, len);
		free(buffer);
		return -1;
	}

	buffer_start = buffer;

	plmn_nn->full_name.tag = *buffer;
	if (plmn_nn->full_name.tag == PLMN_NN_FULLNAME_TAG) {
		buffer = buffer + TAG_BYTE_SIZE;
		plmn_nn->full_name.length = *buffer;
		buffer = buffer + LENGTH_BYTE_SIZE;
		memcpy(plmn_nn->full_name.value, buffer,
				plmn_nn->full_name.length);
		buffer = buffer + plmn_nn->full_name.length;
	}

	plmn_nn->short_name.tag = *buffer;
	if (plmn_nn->short_name.tag == PLMN_NN_SHORTNAME_TAG) {
		buffer = buffer + TAG_BYTE_SIZE;
		plmn_nn->short_name.length = *buffer;
		buffer = buffer + LENGTH_BYTE_SIZE;
		memcpy(plmn_nn->short_name.value, buffer,
				plmn_nn->short_name.length);
		buffer = buffer + plmn_nn->short_name.length;
    }

	free(buffer_start);

	return 0;
}

int vtq_usim_read_uac(vtq_usim_uac_t *uac)
{
	uint8_t buffer[USIM_UAC_LENGTH];
	int len = USIM_UAC_LENGTH;
	int ret;

	ret = usim_read_transparent(USIM_UAC_FILEID, &len, buffer,
					USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	memcpy(uac->uac_acc_id, buffer, USIM_UAC_LENGTH);

	return 0;
}

int vtq_usim_read_acc(vtq_usim_acc_t *acc_id)
{
	uint8_t buffer[USIM_ACC_LENGTH];
	int len = USIM_ACC_LENGTH;
	int ret;

	ret = usim_read_transparent(USIM_ACC_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	memcpy(acc_id->acc, buffer, USIM_ACC_LENGTH);

	return 0;
}

int vtq_usim_read_5gs_loc_info(vtq_usim_5gs_loc_info_t *loc_info)
{
	uint8_t buffer[USIM_5GS_LOCATION_INFO_LENGTH];
	int len = USIM_5GS_LOCATION_INFO_LENGTH;
	int ret;

	ret = usim_read_transparent(USIM_5GS_LOCATION_INFO_FILEID, &len,
			buffer, USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	memcpy(loc_info->guti, buffer, USIM_GUTI_LENGTH);
	memcpy(loc_info->tai.plmn.plmn_id, buffer + USIM_GUTI_LENGTH,
					USIM_PLMNID_LENGTH);
	memcpy(loc_info->tai.tac, buffer+USIM_GUTI_LENGTH+USIM_PLMNID_LENGTH,
						USIM_TAC_LENGTH);
	loc_info->status_update = buffer[USIM_5GS_LOCATION_INFO_LENGTH - 1];

	return 0;
}


int vtq_usim_read_nas_sec_ctx(vtq_usim_nas_security_ctx_t *nas_sec_ctx, int
	nas_sec_ctx_len)
{
	int len = 0;
	int ret;
	uint8_t *nas_data;
	uint8_t *nas_data_start;

	nas_data = (uint8_t *)malloc(nas_sec_ctx_len);
	if (nas_data == NULL) {
		usim_dbg(LOG_ERROR, "%s::%d Memory allocation failed\n",
				__func__, __LINE__);
		return -1;
	}

	ret = usim_read_linearfixed(USIM_NASSECCTX_FILEID, &len, nas_data,
					USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		free(nas_data);
		return ret;
	}

	if (len > nas_sec_ctx_len) {
		usim_dbg(LOG_ERROR, "%s::%d Total received memory is less than total file len\n",
				__func__, __LINE__);
		free(nas_data);
		return -1;
	}

	nas_data_start = nas_data;

	nas_sec_ctx->nas_sec_ctx_tag = *nas_data;
	if (nas_sec_ctx->nas_sec_ctx_tag == NAS_SEC_CTX_TAG) {
		nas_data = nas_data + TAG_BYTE_SIZE;
		nas_sec_ctx->nas_sec_ctx_len = *nas_data;
		nas_data = nas_data + LENGTH_BYTE_SIZE;

		nas_sec_ctx->ngksi.tag = *nas_data;
		if (nas_sec_ctx->ngksi.tag == NAS_SEC_NGKSI_TAG) {
			nas_data = nas_data + TAG_BYTE_SIZE;
			nas_sec_ctx->ngksi.length = *nas_data;
			nas_data = nas_data + LENGTH_BYTE_SIZE;
			memcpy(nas_sec_ctx->ngksi.value, nas_data,
						nas_sec_ctx->ngksi.length);
			nas_data = nas_data + nas_sec_ctx->ngksi.length;
		}

		nas_sec_ctx->kamf.tag = *nas_data;
		if (nas_sec_ctx->kamf.tag == NAS_SEC_KAMF_TAG) {
			nas_data = nas_data + TAG_BYTE_SIZE;
			nas_sec_ctx->kamf.length = *nas_data;
			nas_data = nas_data + LENGTH_BYTE_SIZE;
			memcpy(nas_sec_ctx->kamf.value, nas_data,
						nas_sec_ctx->kamf.length);
			nas_data = nas_data + nas_sec_ctx->kamf.length;
		}

		nas_sec_ctx->ul_nascount.tag = *nas_data;
		if (nas_sec_ctx->ul_nascount.tag == NAS_UL_NASCNT_TAG) {
			nas_data = nas_data + TAG_BYTE_SIZE;
			nas_sec_ctx->ul_nascount.length = *nas_data;
			nas_data = nas_data + LENGTH_BYTE_SIZE;
			memcpy(nas_sec_ctx->ul_nascount.value, nas_data,
					nas_sec_ctx->ul_nascount.length);
			nas_data = nas_data + nas_sec_ctx->ul_nascount.length;
		}

		nas_sec_ctx->dl_nascount.tag = *nas_data;
		if (nas_sec_ctx->dl_nascount.tag == NAS_DL_NASCNT_TAG) {
			nas_data = nas_data + TAG_BYTE_SIZE;
			nas_sec_ctx->dl_nascount.length = *nas_data;
			nas_data = nas_data + LENGTH_BYTE_SIZE;
			memcpy(nas_sec_ctx->dl_nascount.value, nas_data,
					nas_sec_ctx->dl_nascount.length);
			nas_data = nas_data + nas_sec_ctx->dl_nascount.length;
		}

		nas_sec_ctx->int_enc_id.tag = *nas_data;
		if (nas_sec_ctx->int_enc_id.tag == NAS_INT_ENC_TAG) {
			nas_data = nas_data + TAG_BYTE_SIZE;
			nas_sec_ctx->int_enc_id.length = *nas_data;
			nas_data = nas_data + LENGTH_BYTE_SIZE;
			memcpy(nas_sec_ctx->int_enc_id.value, nas_data,
						nas_sec_ctx->int_enc_id.length);
			nas_data = nas_data + nas_sec_ctx->int_enc_id.length;
		}

		nas_sec_ctx->eps_int_enc_id.tag = *nas_data;
		if (nas_sec_ctx->eps_int_enc_id.tag == NAS_EPS_INT_ENC_TAG) {
			nas_data = nas_data + TAG_BYTE_SIZE;
			nas_sec_ctx->eps_int_enc_id.length = *nas_data;
			nas_data = nas_data + LENGTH_BYTE_SIZE;
			memcpy(nas_sec_ctx->eps_int_enc_id.value, nas_data,
					nas_sec_ctx->eps_int_enc_id.length);
			nas_data = nas_data +
					nas_sec_ctx->eps_int_enc_id.length;
		}
	}

	free(nas_data_start);
	return 0;
}


int vtq_usim_invalidate_nas_sec_ctx(void)
{
	int ret = -1;
	uint8_t buffer[256];
	int len = 0;

	ret = usim_read_linearfixed(USIM_NASSECCTX_FILEID, &len, buffer,
			USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	buffer[4] = 0x07; //set ngKSI to 07 to invalidate

	ret = usim_write_linearfixed(USIM_NASSECCTX_FILEID, len, buffer,
			USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d write linear fixed failied err value:%d\n",
				__func__,	__LINE__, ret);
	}

	return ret;
}

int vtq_usim_read_admin_data(vtq_usim_admin_data_t *admin_data)
{
	uint8_t buffer[USIM_ADMIN_LENGTH];
	int len = USIM_ADMIN_LENGTH;
	int ret;

	ret = usim_read_transparent(USIM_ADMIN_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	admin_data->ue_op_mode = buffer[0];
	memcpy(admin_data->addnl_info, buffer+1, 2);
	admin_data->mnc_len = buffer[3];

	return 0;
}

int vtq_usim_read_nas_auth_keys(vtq_usim_nas_auth_keys_t *nas_auth_keys,
		int auth_key_len)
{
	uint8_t *buffer;
	int tot_len = 0;
	int next_tag_position = 0, length = 0;
	int len_octets_count = 0;
	int i = 0;
	int ret;
	uint8_t *buffer_start;

	buffer = (uint8_t *)malloc(auth_key_len);
	if (buffer == NULL) {
		usim_dbg(LOG_ERROR, "%s::%d Memory allocation failed\n",
					__func__, __LINE__);
		return -1;
	}

	ret = usim_read_transparent(USIM_NAS_AUTH_KEYS_FILEID, &tot_len,
			buffer, USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		free(buffer);
		return ret;
	}

	if (tot_len > auth_key_len) {
		usim_dbg(LOG_ERROR, "%s::%d Total received memory is less than total file len\n",
				__func__, __LINE__);
		free(buffer);
		return -1;
	}

	buffer_start = buffer;

	nas_auth_keys->kausf.tag = *buffer;
	if (nas_auth_keys->kausf.tag == USIM_NAS_AUTH_KEY_AUSF_TAG) {
		buffer = buffer + TAG_BYTE_SIZE;
		length = *buffer;
		if (length > 127) {
			len_octets_count = (*buffer & 0x7F);
			length = 0;
			for (i = 0; i < len_octets_count; i++) {
				length = length + buffer[i+LENGTH_BYTE_SIZE];
			}
		}

		nas_auth_keys->kausf.length = length;
		buffer = buffer + LENGTH_BYTE_SIZE + len_octets_count;

		memcpy(nas_auth_keys->kausf.value, buffer, length);
		buffer = buffer + length;
	}

	nas_auth_keys->kseaf.tag = *buffer;
	if (nas_auth_keys->kseaf.tag == USIM_NAS_AUTH_KEY_SEAF_TAG) {
		buffer = buffer + TAG_BYTE_SIZE;
		length = *buffer;
		if (length > 127) {
			len_octets_count = (*buffer & 0x7F);
			length = 0;
			for (i = 0; i < len_octets_count; i++) {
				length = length +
						buffer[i+2+next_tag_position];
			}
		}

		nas_auth_keys->kseaf.length = length;
		buffer = buffer + LENGTH_BYTE_SIZE + len_octets_count;
		memcpy(nas_auth_keys->kseaf.value, buffer, length);
	}

	free(buffer_start);
	return 0;
}

int vtq_usim_write_uac(vtq_usim_uac_t *uac)
{
	int ret;
	uint8_t buffer[USIM_UAC_LENGTH];
	int len = USIM_UAC_LENGTH;
	byte lower_addr = 0x00;
	byte high_addr = 0x00;

	memcpy(buffer, uac->uac_acc_id, USIM_UAC_LENGTH);

	ret = usim_write_transparent(USIM_UAC_FILEID, len, buffer, lower_addr,
			high_addr, USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d write transparent failied err value:%d\n",
					__func__, __LINE__, ret);
	}

	return ret;
}


int vtq_usim_write_5gs_loc_info(vtq_usim_5gs_loc_info_t *loc_info)
{
	int ret;
	uint8_t buffer[USIM_5GS_LOCATION_INFO_LENGTH];
	byte lower_addr = 0;
	byte higher_addr = 0;
	int len = USIM_5GS_LOCATION_INFO_LENGTH;

	memcpy(buffer, (uint8_t *)loc_info, USIM_5GS_LOCATION_INFO_LENGTH);

	ret = usim_write_transparent(USIM_5GS_LOCATION_INFO_FILEID, len,
				buffer, lower_addr, higher_addr,
				USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d write transparent failied err value:%d\n",
					__func__, __LINE__, ret);
	}

	return ret;
}

int vtq_usim_write_nas_sec_ctx(vtq_usim_nas_security_ctx_t *nas_sec_ctx,
	int nas_sec_ctx_len)
{
	int ret;
	uint8_t *buffer;
	int len = 0;

	buffer = malloc(nas_sec_ctx_len);
	if (buffer == NULL) {
		usim_dbg(LOG_ERROR, "%s::%d Memory allocation failed\n",
				__func__, __LINE__);
		return -1;
	}


	buffer[len] = nas_sec_ctx->nas_sec_ctx_tag;
	len = len + TAG_BYTE_SIZE + LENGTH_BYTE_SIZE;

	buffer[len] = nas_sec_ctx->ngksi.tag;
	len = len + TAG_BYTE_SIZE;
	buffer[len] = nas_sec_ctx->ngksi.length;
	len = len + LENGTH_BYTE_SIZE;

	memcpy(buffer+len, nas_sec_ctx->ngksi.value,
			nas_sec_ctx->ngksi.length);

	len = len + nas_sec_ctx->ngksi.length;

	buffer[len] = nas_sec_ctx->kamf.tag;
	len = len + TAG_BYTE_SIZE;
	buffer[len] = nas_sec_ctx->kamf.length;
	len = len + LENGTH_BYTE_SIZE;
	memcpy(buffer+len, nas_sec_ctx->kamf.value,
			nas_sec_ctx->kamf.length);
	len = len + nas_sec_ctx->kamf.length;


	buffer[len] = nas_sec_ctx->ul_nascount.tag;
	len = len + TAG_BYTE_SIZE;
	buffer[len] = nas_sec_ctx->ul_nascount.length;
	len = len + LENGTH_BYTE_SIZE;
	memcpy(buffer+len, nas_sec_ctx->ul_nascount.value,
			nas_sec_ctx->ul_nascount.length);
	len = len + nas_sec_ctx->ul_nascount.length;

	buffer[len] = nas_sec_ctx->dl_nascount.tag;
	len = len + TAG_BYTE_SIZE;
	buffer[len] = nas_sec_ctx->dl_nascount.length;
	len = len + LENGTH_BYTE_SIZE;
	memcpy(buffer+len, nas_sec_ctx->dl_nascount.value,
			nas_sec_ctx->dl_nascount.length);
	len = len + nas_sec_ctx->dl_nascount.length;

	buffer[len] = nas_sec_ctx->int_enc_id.tag;
	len = len + TAG_BYTE_SIZE;
	buffer[len] = nas_sec_ctx->int_enc_id.length;
	len = len + LENGTH_BYTE_SIZE;
	memcpy(buffer+len, nas_sec_ctx->int_enc_id.value,
			nas_sec_ctx->int_enc_id.length);
	len = len + nas_sec_ctx->int_enc_id.length;

	buffer[len] = nas_sec_ctx->eps_int_enc_id.tag;
	len = len + TAG_BYTE_SIZE;
	buffer[len] = nas_sec_ctx->eps_int_enc_id.length;
	len = len + LENGTH_BYTE_SIZE;
	memcpy(buffer+len, nas_sec_ctx->eps_int_enc_id.value,
			nas_sec_ctx->eps_int_enc_id.length);
	len = len + nas_sec_ctx->eps_int_enc_id.length;
	buffer[1] = len - TAG_BYTE_SIZE - LENGTH_BYTE_SIZE;

	ret = usim_write_linearfixed(USIM_NASSECCTX_FILEID, len,
			buffer, USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d write linear fixed failied err value:%d\n",
				__func__, __LINE__, ret);
	}
	free(buffer);
	return ret;

}

int vtq_usim_write_nas_auth_keys(vtq_usim_nas_auth_keys_t *nas_auth_keys)
{
	int ret = -1;
	uint8_t buffer[256];
	int len = 0;
	int lower_addr = 0, high_addr = 0;

	len = len + TAG_BYTE_SIZE;
	*buffer = nas_auth_keys->kausf.tag;
	len = len + LENGTH_BYTE_SIZE;
	*(buffer+TAG_BYTE_SIZE) = nas_auth_keys->kausf.length;

	memcpy(buffer+len, nas_auth_keys->kausf.value,
				nas_auth_keys->kausf.length);
	len = len + nas_auth_keys->kausf.length;
	buffer[len] = nas_auth_keys->kseaf.tag;
	len = len + TAG_BYTE_SIZE;
	buffer[len] = nas_auth_keys->kseaf.length;
	len = len + LENGTH_BYTE_SIZE;
	memcpy(buffer+len, nas_auth_keys->kseaf.value,
		nas_auth_keys->kseaf.length);
	len = len + nas_auth_keys->kseaf.length;

	ret = usim_write_transparent(USIM_NAS_AUTH_KEYS_FILEID, len, buffer,
			lower_addr, high_addr, USIM_5GS_DIRFILEID);
	if (ret < 0) {
	usim_dbg(LOG_ERROR, "%s::%d write transparent failied err value:%d\n",
					__func__, __LINE__, ret);
	}

	return 0;

}

int vtq_usim_read_acl(vtq_usim_acl_t *acl, int acl_max_len)
{
	uint8_t *buffer = NULL;
	int ret = -1;
	int len = 0, i = 0;
	uint8_t *buffer_start;

	buffer = (uint8_t *)malloc(acl_max_len);
	if (buffer == NULL) {
		usim_dbg(LOG_ERROR, "%s::%d Memory allocation failed\n",
				__func__, __LINE__);
		return -1;
	}

	ret = usim_read_transparent(USIM_ACL_FILEID, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		free(buffer);
		return ret;
	}

	if (len > acl_max_len) {
		usim_dbg(LOG_ERROR, "%s::%d Total received memory %d is less than total file len %d\n",
				__func__,
				__LINE__, acl_max_len, len);
		free(buffer);
		return -1;
	}

	buffer_start = buffer;

	acl->count = *buffer;

	if (acl->count >= ACL_MAX_LIST) {
		usim_dbg(LOG_INFO, "%s::%d ACL list count %d in usim is greater than %d",
				__func__, __LINE__,
				acl->count, ACL_MAX_LIST);
		acl->count = ACL_MAX_LIST;
	}

	buffer = buffer + ACL_COUNT_BYTE_SIZE;

	for (i = 0; i < acl->count; i++) {
		acl->tlv_list[i].tag = *buffer;
		if (acl->tlv_list[i].tag == ACL_TAG) {
			buffer = buffer + TAG_BYTE_SIZE;
			acl->tlv_list[i].length = *buffer;
			buffer = buffer + LENGTH_BYTE_SIZE;
			memcpy(acl->tlv_list[i].value, buffer,
					acl->tlv_list[i].length);
			buffer = buffer + acl->tlv_list[i].length;
		}
	}

	free(buffer_start);
	return 0;
}

int vtq_usim_write_rid(uint16_t rid)
{
	int ret;
	uint8_t buffer[USIM_RID_LENGTH];
	int len = USIM_RID_LENGTH;
	int lower_addr = 0x00;
	int high_addr = 0x00;

	memcpy(buffer, (uint8_t *)&rid, USIM_RID_LENGTH);
	ret = usim_write_transparent(USIM_RID_FILEID, len, buffer, lower_addr,
				high_addr, USIM_5GS_DIRFILEID);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d write transparent failied err value:%d\n",
					__func__, __LINE__, ret);
	}

	return ret;
}

int vtq_usim_read_ust(unsigned char *ust_data, int *length)
{
	uint8_t buffer[UST_TABLE_MAX_LEN];
	int len = UST_TABLE_MAX_LEN;
	int ret;

	ret = usim_read_transparent(0x6F38, &len, buffer, 0);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d read transparent failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	*length = len;
	memcpy(ust_data, buffer, len);

	return ret;
}

int vqa_usim_api_check_sqn(uint32_t seq, uint8_t ind)
{

	if (seq > usim_sqn_data.sqn[ind])
		return 0;
	else
		return -1;
}

int vtq_usim_offchip_authenticate(char *key, char *opc,
		vtq_usim_lv_t *random_chal, vtq_usim_lv_t *auth_token,
		vtq_usim_lv_t *auts, vtq_usim_lv_t *res, vtq_usim_lv_t *ck,
		vtq_usim_lv_t *ik)
{
	int ret = 0;
	u8 ak[VQA_USIM_API_AK_SIZE];
	u8 sqn[VQA_USIM_API_SQN_SIZE];
	u8 xmac[VQA_USIM_API_XMAC_SIZE];
	int i = 0;

	usim_dbg(LOG_INFO, "%s::%d random value:%s\n", __func__, __LINE__,
				dump_tlv(random_chal));
	usim_dbg(LOG_INFO, "%s::%d autn value:%s\n", __func__, __LINE__,
				dump_tlv(auth_token));

	memcpy(auth_keys.usim_api_k, key, 16);

	memcpy(auth_keys.opc, opc, 16);

	/** Takes key K and random challenge RAND, and returns response RES,
	 *confidentiality key CK, integrity key IK and anonymity key AK. */
	f2345(auth_keys.usim_api_k, random_chal->value,
			res->value, ck->value, ik->value, ak, auth_keys.opc);

	usim_dbg(LOG_INFO, "%s::%d res value:%s\n", __func__, __LINE__,
						dump_tlv(res));
	usim_dbg(LOG_INFO, "%s::%d ck value:%s\n", __func__, __LINE__,
							dump_tlv(ck));
	usim_dbg(LOG_INFO, "%s::%d ik value:%s\n", __func__, __LINE__,
						dump_tlv(ik));
	usim_dbg(LOG_INFO, "%s::%d ak value:%02X%02X%02X%02X%02X%02X\n",
			__func__, __LINE__, ak[0], ak[1], ak[2], ak[3], ak[4],
			ak[5]);

	/* Retrieve the sequence number SQN = (SQN ⊕ AK) ⊕ AK */

	for (i = 0; i < VQA_USIM_API_SQN_SIZE; i++) {
		sqn[i] = auth_token->value[i] ^ ak[i];
	}



	/* Compute XMAC = f1K (SQN || RAND || AMF) */
	f1(auth_keys.usim_api_k, random_chal->value, sqn,
			&auth_token->value[VQA_USIM_API_SQN_SIZE], xmac,
			auth_keys.opc);

	/* Compare the XMAC with the MAC included in AUTN */
	if (memcmp(xmac, &auth_token->value[VQA_USIM_API_SQN_SIZE +
				VQA_USIM_API_AMF_SIZE],
				VQA_USIM_API_XMAC_SIZE) != 0) {
		ret = 1;
		//LOG_FUNC_RETURN (RETURNerror);
	} else {
		usim_dbg(LOG_INFO, "%s::%d Comparing the XMAC with the MAC included in AUTN Succeeded\n",
			__func__, __LINE__);
		/* Verify that the received sequence number SQN is in the
		 * correct range
		 */
		ret = vqa_usim_api_check_sqn(*(uint32_t *)(sqn),
						sqn[VQA_USIM_API_SQN_SIZE - 1]);
	}

	if (ret != 0) {
		u8 sqn_ms[VQA_USIM_API_SQNMS_SIZE];
		u8 sqnms[VQA_USIM_API_SQNMS_SIZE];
		u8 macs[VQA_USIM_API_MACS_SIZE];
		/* Synchronisation failure; compute the AUTS parameter */

		/* Concealed value of the counter SQNms in the USIM:
		 * Conc(SQNMS) = SQNMS ⊕ f5*K(RAND) */
		f5star(auth_keys.usim_api_k, random_chal->value, ak,
				auth_keys.opc);


		memset(sqn_ms, 0, VQA_USIM_API_SQNMS_SIZE);


		usim_dbg(LOG_INFO, "%s::%d usim_sqn_data.sqn_ms %p\n", __func__,
						__LINE__, usim_sqn_data.sqn_ms);
		for (i = 1; i <= VQA_USIM_API_SQNMS_SIZE; i++) {
			//#warning "LG:BUG HERE TODO"
			usim_dbg(LOG_INFO, "%s::%d i %d: %d\n",	__func__,
					__LINE__, i,
			((uint8_t *)(usim_sqn_data.sqn_ms))[
				VQA_USIM_API_SQNMS_SIZE - i]);
			sqn_ms[VQA_USIM_API_SQNMS_SIZE - i] =
			((uint8_t *)(usim_sqn_data.sqn_ms))[
				VQA_USIM_API_SQNMS_SIZE - i];
		}


		for (i = 0; i < VQA_USIM_API_SQNMS_SIZE; i++) {
			sqnms[i] = sqn_ms[i] ^ ak[i];
		}

		/* Synchronisation message authentication code:
		 * MACS = f1*K(SQNMS || RAND || AMF) */
		f1star(auth_keys.usim_api_k, random_chal->value, sqn_ms,
				&random_chal->value[VQA_USIM_API_SQN_SIZE],
				macs, auth_keys.opc);
		usim_dbg(LOG_INFO, "%s::%d MACS %02X%02X%02X%02X%02X%02X%02X%02X",
				__func__, __LINE__, macs[0], macs[1], macs[2],
				macs[3], macs[4], macs[5], macs[6], macs[7]);

		/* Synchronisation authentication token:
		 * AUTS = Conc(SQNMS) || MACS */
		memcpy(&auts->value[0], sqnms, VQA_USIM_API_SQNMS_SIZE);
		memcpy(&auts->value[VQA_USIM_API_SQNMS_SIZE], macs,
				VQA_USIM_API_MACS_SIZE);
		auts->length = VQA_USIM_API_SQNMS_SIZE +
					VQA_USIM_API_MACS_SIZE;
		ret = 2;
	}

	return ret;

}
int vtq_usim_authenticate(vtq_usim_lv_t *random_chal,
	vtq_usim_lv_t *auth_token, vtq_usim_lv_t *auts, vtq_usim_lv_t *res,
	vtq_usim_lv_t *ck, vtq_usim_lv_t *ik)
{
	int ret = 0;

	u8 ak[VQA_USIM_API_AK_SIZE];

	ret = usim_onchip_auth(random_chal, auth_token, auts, res, ck, (vtq_usim_lv_t *) ak);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d ON chip authentication failed err value:%d\n",
					__func__, __LINE__, ret);
		return -1;
	}

	return ret;
}

vtq_usim_state_t vtq_usim_get_cur_status(void)
{
	int status;

	status = vtq_usim_get_status(&event_handle);
	if (status < 0) {
		usim_dbg(LOG_ERROR, "%s:%d unable to get updated status value:%d\n",
					__func__, __LINE__, status);
		return -1;
	}

	return status;
}

void vtq_usim_status_data_dump(void)
{
	usim_dbg(LOG_INFO, "\ndumping status data dump%s\n", "");
	usim_dbg(LOG_INFO, "=========================%s\n", "");
	usim_dbg(LOG_INFO, "usim ccsr phy address: %p.\n", (void *)event_handle.usim_ccsr_phy);
	usim_dbg(LOG_INFO, "usim ccsr mapped address: %p.\n", (void *)event_handle.usim_ccsr_map);
	usim_dbg(LOG_INFO, "usim previous state: %d.\n", event_handle.state);
	usim_dbg(LOG_INFO, "usim current state: %d.\n", event_handle.state_cur);

	usim_dbg(LOG_INFO, "usim iccid len old: %d.\n", event_handle.iccid_len_old);
	usim_dbg(LOG_INFO, "usim iccid old: %s", "");
	usim_dbg_hexdump(LOG_INFO, &event_handle.iccid_old[0], event_handle.iccid_len_old);

	usim_dbg(LOG_INFO, "usim iccid len new: %d.\n", event_handle.iccid_len_new);
	usim_dbg(LOG_INFO, "usim iccid new: %s", "");
	usim_dbg_hexdump(LOG_INFO, &event_handle.iccid_new[0], event_handle.iccid_len_new);

	return;
}

vtq_usim_state_t vtq_usim_check_status(void)
{
	int status, ret;

	ret = ioctl(usim_fd, USIM_IOCTL_STATUS, &status);
	if (ret) {
		usim_dbg(LOG_ERROR, "%s:%d ioctl to get the USIM status failed err value:%d\n",
			__func__, __LINE__, ret);
		return -1;
	}

	return status;
}

int vtq_usim_invalidate(void)
{
	int ret;

	ret = ioctl(usim_fd, USIM_IOCTL_INVALIDATE_USIM, NULL);
	if (ret) {
		usim_dbg(LOG_INFO, "%s::%d ioctl to  invalidate USIM failed err value:%d\n",
				__func__, __LINE__, ret);
		return -1;
	}

	return 0;
}
