/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2021-2022 NXP
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "usim_ioctl.h"
#include "usim_library.h"
#include "usim_apdu.h"

#define USIM_FCP_TMPL_TAG 0x62

typedef unsigned short word;
typedef unsigned char byte;

sim_t sim;

extern int debug;

typedef struct {
	unsigned short len;
	uint8_t count;
} usim_rec_t;

typedef struct {
	byte rfu1[2];
	byte filesize[2];
	byte fileid[2];
	byte filetype;
	byte increase;
	byte accessconditions[3];
	byte filestatus;
	byte numbytesfollow;
	byte efstruct;
	byte recordsize;
} gsm_fci_ef_t;

gsm_fci_ef_t *fci_current;

unsigned char aid[32];
int aid_len;

int usim_read_adminpin(unsigned char *admin_pin)
{
	int flash_fd_adpin = 0;

	flash_fd_adpin = open("/dev/mtd0", O_RDWR | O_SYNC);
	if (flash_fd_adpin  == -1) {
		usim_dbg(LOG_ERROR, "%s:%d /dev/mtd open failed ret:%d\n"
					, __func__, __LINE__, flash_fd_adpin);
		goto err1;
	}

	usim_dbg(LOG_INFO, "/dev/mtd opened (usim_read_adminpin).%s\n", "");

	if (read_from_flash(flash_fd_adpin, USIM_SPI_ADMIN_PIN_ADD, 8,
						admin_pin) == -1) {
		usim_dbg(LOG_ERROR, "%s:%d Read Admin PIN Failed ret:%d\n"
					, __func__, __LINE__, flash_fd_adpin);
		goto err;
	}

	usim_dbg(LOG_INFO, "Admin PIN read success:\n");
	usim_dbg_chardump(LOG_INFO, admin_pin, 8);

	close(flash_fd_adpin);

	return 0;

err:
	close(flash_fd_adpin);
err1:
	return -1;


	return 0;
}

int usim_select_file_byaid(unsigned char *aid, int aid_len, byte *buf, int len)
{
	byte cmd[50] = {
		0x00, 0xa4, 0x04, 0x00, aid_len, 0x00, 0x00
	};

	memcpy(cmd+5, aid, aid_len);
	return SendReceiveAPDU(cmd, 5+aid_len, buf, len);

}


int usim_select_file_byid(word fileid, byte *buf, int len)
{
	byte cmd[] = {
		0xa0, 0xa4, 0x00, 0x00, 0x02, 0x00, 0x00
	};

	if (sim.type == USIM_TYPE) {
		cmd[0] = 0x00;
		cmd[3] = 0x04;
	}
	cmd[5] = (fileid >> 8) & 0xff;
	cmd[6] = (fileid >> 0) & 0xff;
	return SendReceiveAPDU(cmd, 7, buf, len);

}

int usim_get_response(int len, byte *buf)
{
	byte cmd[] = { 0xa0, 0xc0, 0x00, 0x00, len};

	if (sim.type == USIM_TYPE)
		cmd[0] = 0x00;

	return SendReceiveAPDU(cmd, 5, buf, len+3);
}

int usim_read_binary(int len, byte *buf)
{
	byte cmd[] = { 0xa0, 0xb0, 0, 0, len };

	if (sim.type == USIM_TYPE)
		cmd[0] = 0x00;

	return SendReceiveAPDU(cmd, 5, buf, len+3);
}

int usim_read_record(int id, int len, byte *buf)
{
	byte cmd[] = { 0xa0, 0xb2, id, 0x04, len };

	if (sim.type == USIM_TYPE)
		cmd[0] = 0x00;

	return SendReceiveAPDU(cmd, 5, buf, len+3);
}

int usim_update_record(int id, int len, byte *buf, int resp_len,
		byte *resp_buf)
{
	byte cmd[256] = { 0xa0, 0xdc, id, 0x04, len };

	if (sim.type == USIM_TYPE)
		cmd[0] = 0x00;

	memcpy(cmd+5, buf, len);

	return SendReceiveAPDU(cmd, 5+len, resp_buf, resp_len);
}

int usim_get_identity(int len, byte *buf)
{
	byte cmd[] = { 0xa0, 0x78, 0x00, 0x02, len };

	if (sim.type == USIM_TYPE)
		cmd[0] = 0x00;

	return SendReceiveAPDU(cmd, 5, buf, 4);
}

int usim_update_binary(int len, byte *buf, byte off_high, byte off_low,
		int resp_len, byte *resp_buf)
{
	byte cmd[256] = {0xa0, 0xD6, off_high, off_low, len};

	if (sim.type == USIM_TYPE)
		cmd[0] = 0x00;

	memcpy(cmd+5, buf, len);
	return SendReceiveAPDU(cmd, 5+len, resp_buf, resp_len);
}

int usim_authenticate(int len, byte *buf, byte *resp_buf, int res_len)
{
	byte cmd[39] = {0xa0, 0x88, 0x00, 0x81, len};

	if (sim.type == USIM_TYPE)
		cmd[0] = 0x00;

	if (strlen((const char *)buf) > 34) {
		usim_dbg(LOG_INFO, "%s::%d buffer data :%ld is greater\n",
			__func__, __LINE__, strlen((const char *)buf));
		return -1;
	}

	memcpy(cmd+5, buf, len);
	return SendReceiveAPDU(cmd, 5 + len, resp_buf, res_len);

}

int usim_admin_verify(int len, byte *resp_buff)
{
	unsigned char buf[8];
	byte cmd[13] = {0xa0, 0x20, 0x00, 0x0a, 0x08, '0', '0', '0',
		'0', '0', '0', '0', '0'};
	int ret = -1;

	if (sim.type == USIM_TYPE)
		cmd[0] = 0x00;

	ret = usim_read_adminpin(buf);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Reading admin pin failed\n",
				__func__, __LINE__);
		return -1;
	}

	memcpy(cmd+5, buf, 8);

	usim_dbg_hexdump(LOG_INFO, cmd, 13);

	return SendReceiveAPDU(cmd, 13, resp_buff, len);
}

int usim_verify(byte chv, char *buf)
{
	byte cmd[13] = {0xa0, 0x20, 0x00, chv, 0x08, '0', '0', '0',
					'0', '0', '0', '0', '0'};

	if (sim.type == USIM_TYPE)
		cmd[0] = 0x00;

	if (strlen(buf) > 8)
		return -1;

	memcpy(cmd+5, buf, strlen(buf));
	memset(cmd + 5 + strlen(buf), 0xff, 8 - strlen(buf));
	return SendReceiveAPDU(cmd, 13, 0, 0);

}

int usim_get_record_len(unsigned char recnum)
{
	unsigned char buf[127];
	int blen = sizeof(buf);

	byte cmd[] = { 0xa0, 0xb2, recnum, 0x04, blen };

	if (sim.type == USIM_TYPE)
		cmd[0] = 0x00;


	SendReceiveAPDU(cmd, 5, buf, blen);

	usim_dbg(LOG_INFO, "%s::%d Response buf %x %x\n", __func__, __LINE__,
				buf[0], buf[1]);

	if ((buf[0] != 0x6c) && (buf[0] != 0x67)) {
		usim_dbg(LOG_ERROR, "%s::%d Unexpected Response to get record len command %x %x\n",
				__func__, __LINE__,
				buf[0], buf[1]);
		return -1;
	}

	return buf[1];
}

int usim_get_aid(unsigned char *aid, int len)
{
	int ret =  -1, rlen, rec;
	unsigned char resp[3];//select response data buffer;
    size_t sr_len;//select response data length;
	unsigned char record_buf[127];
	struct efdir {
		unsigned char appl_template_tag; /* 0x61 */
		unsigned char appl_template_len;
		unsigned char appl_id_tag; /* 0x4f */
		unsigned char aid_len;
		unsigned char rid[5];
		unsigned char appl_code[2]; /* 0x1002 for 3G USIM */
		unsigned char aid_data_extra[9];
	} *efdir;

	size_t blen;

	memset(record_buf, 0, sizeof(record_buf));
	efdir = (struct efdir *) (record_buf+1);

	sr_len = sizeof(resp);
	ret = usim_select_file_byid(USIM_FILE_EF_DIR, resp, sr_len);

	usim_dbg(LOG_INFO, "%s::%d EF1 resp[0]:%02x,resp[1]:%02x,resp[2]:%02x sr_len:%ld\n",
				__func__, __LINE__, resp[0],
				resp[1], resp[2], (long)sr_len);

	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d select file ioctl failed err value:%d\n",
				__func__, __LINE__, ret);
		return ret;
	}

	if (((resp[1] != 0x90) || (resp[2] != 0x00)) && (resp[1] != 0x61) &&
			(resp[1] != 0x9F)) {
		usim_dbg(LOG_ERROR, "%s::%d Select file %x failed SW word:%02x %02x\n",
				__func__, __LINE__,
				USIM_FILE_EF_DIR, resp[1], resp[2]);
		return -1;
	}

	for (rec = 1; rec < 10; rec++) {

		rlen = usim_get_record_len(rec);
		if (rlen < 0) {
			usim_dbg(LOG_ERROR, "%s::%d Failed to get EF_DIR record length\n",
					__func__, __LINE__);
			return -1;
		}

		blen = sizeof(record_buf);
		if (rlen >= (int) (blen - 2)) {
			usim_dbg(LOG_ERROR, "%s::%d Too long EF_DIR record\n",
					__func__, __LINE__);
			return -1;
		}

		ret = usim_read_record(rec, rlen, record_buf);
		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d Read record failed err value:%d\n",
					__func__, __LINE__, ret);
			return -1;
		}

		if ((record_buf[rlen + 1] != 0x90) ||
				(record_buf[rlen + 2] != 0x00)) {
			usim_dbg(LOG_ERROR, "%s::%d Read record returned unexpected status rlen:%x SW:%x %x %x %x %x\n",
					__func__,
					__LINE__, rlen, record_buf[0],
					record_buf[1], record_buf[27],
					record_buf[rlen], record_buf[rlen+1]);
			return -1;
		}

		if (efdir->appl_template_tag != 0x61) {
			usim_dbg(LOG_INFO, "%s::%d Unexpected application template tag 0x%x\n",
					__func__,
					__LINE__, efdir->appl_template_tag);
			continue;
		}

		if (efdir->appl_template_len > rlen - 2) {
			usim_dbg(LOG_INFO, "%s::%d Too long application template (len=%d rlen=%d)\n",
					__func__, __LINE__,
					efdir->appl_template_len, rlen);
			continue;
		}

		if (efdir->appl_id_tag != 0x4f) {
			usim_dbg(LOG_INFO, "%s::%d Unexpected application identifier tag 0x%x\n",
					__func__,
					__LINE__, efdir->appl_id_tag);
			continue;
		}

		if ((efdir->aid_len < 1) || (efdir->aid_len > 16)) {
			usim_dbg(LOG_INFO, "%s::%d Invalid AID length %d\n",
					__func__, __LINE__, efdir->aid_len);
			continue;
		}

		usim_dbg_hexdump(LOG_INFO, efdir->rid, efdir->aid_len);

		if ((efdir->appl_code[0] == 0x10) &&
				(efdir->appl_code[1] == 0x02)) {

			usim_dbg(LOG_INFO, "%s::%d 3G USIM app found from EF_DIR record %d\n",
					__func__,
					__LINE__, rec);
			break;
		}

	}

	if (rec >= 10) {
		usim_dbg(LOG_ERROR, "%s::%d 3G USIM app not found from EF_DIR records\n",
				__func__, __LINE__);
		return -1;
	}

	if (efdir->aid_len > len) {
		usim_dbg(LOG_ERROR, "%s::%d Too long AID\n", __func__,
				__LINE__);
		return -1;
	}

	memcpy(aid, efdir->rid, efdir->aid_len);

	return efdir->aid_len;
}


int usim_get_suci(int len, byte *buf)
{
	usim_get_identity(len, buf);
	//0x6985 indicates suci need to be generated in ME
	if ((buf[0] != 0x90) || (buf[1] != 0x00)) {
		return 0;
	} else {
		usim_dbg(LOG_INFO, "%s::%d Status Word = %x %x\n",
				__func__, __LINE__, buf[0], buf[1]);
		return 1;
	}
}

int usim_verify_pin(int pin_no, char *pin)
{
	int ret;

	ret = usim_verify(pin_no, pin);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d usim verifycation failed due to ioctl err val:%d\n",
				__func__, __LINE__, ret);
	}
	return ret;
}

int usim_select_gsm(void)
{
	int ret = -1;
	unsigned char resp[3];//select response data buffer;
	size_t sr_len;//select response data length;


	sr_len = sizeof(resp);
	ret = usim_select_file_byid(sim.dir_fileid, resp, sr_len);

	usim_dbg(LOG_INFO, "%s::%d GSM select resp[0]:%02x,resp[1]:%02x,resp[2]:%02x sr_len:%ld\n",
			__func__,
			__LINE__, resp[0], resp[1], resp[2],
			(long)sr_len);

	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Select file ioctl failed err value:%d\n",
				__func__, __LINE__,
				ret);
		return ret;
	}

	if (((resp[1] != 0x90) || (resp[2] != 0x00)) && (resp[1] != 0x61) &&
			(resp[1] != 0x9F)) {
		usim_dbg(LOG_ERROR, "%s::%d Select file %x failed SW word:%02x %02x\n",
				__func__,
				__LINE__, sim.dir_fileid, resp[1],
				resp[2]);
		return -1;
	}
	return 0;
}

int usim_select_aid(void)
{
	int ret = -1;
	unsigned char resp[3];//select response data buffer;
	size_t sr_len;//select response data length;

	aid_len = usim_get_aid(aid, sizeof(aid));
	if (aid_len < 0) {
		usim_dbg(LOG_ERROR, "USIM:%s failed to find AID for USIM APP use standard 3G RID\n",
				__func__);
		memcpy(aid, "\xa0\x00\x00\x00\x87", 5);
		aid_len = 5;
	}

	usim_dbg_hexdump(LOG_INFO, aid, aid_len);

	sr_len = sizeof(resp);
	ret = usim_select_file_byaid(aid, aid_len, resp, sr_len);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d select file ioctl failed err value:%d\n",
				__func__, __LINE__,
				ret);
		return ret;
	}

	if (((resp[1] != 0x90) || (resp[2] != 0x00)) && (resp[1] != 0x61) &&
		(resp[1] != 0x9F)) {
		usim_dbg(LOG_ERROR, "%s::%d select aid returned Unexpected status %02x %02x\n",
				__func__, __LINE__, resp[1], resp[2]);
		return -1;
	}
	return 0;
}

int usim_write_transparent(word fileid, int len, byte *buffer,
		byte lower_offset_addr, byte high_offset_addr, int dir_fileid)
{
	int ret = -1;
	unsigned char resp[3];//select response data buffer;
	size_t sr_len;//select response data length;
	int select_back = 0;

	if (dir_fileid) {
		memset(resp, 0, sizeof(resp));
		sr_len = sizeof(resp);

		ret = usim_select_file_byid(dir_fileid, resp, sr_len);
		usim_dbg(LOG_INFO, "%s::%d DF resp[0]:%02x,resp[1]:%02x, resp[2]:%02x sr_len:%ld\n",
				__func__,
				__LINE__, resp[0], resp[1], resp[2],
				(long)sr_len);
		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d select file ioctl failed err value:%d\n",
					__func__, __LINE__,
					ret);
			return ret;
		}

		if ((resp[1] == 0x6a) && (resp[2] == 0x82)) {
			usim_dbg(LOG_ERROR, "%s::%d Select file %x not supported SW word:%02x %02x\n",
					__func__, __LINE__, dir_fileid,
					resp[1], resp[2]);
			return VTQ_USIM_FILE_NOT_SUPPORTED;
		}

		if (((resp[1] != 0x90) || (resp[2] != 0x00)) &&
			(resp[1] != 0x61) && (resp[1] != 0x9F)) {
			usim_dbg(LOG_ERROR, "%s::%d Select file %x failed SW word:%02x %02x\n",
					__func__,
					__LINE__, dir_fileid, resp[1], resp[2]);
			return -1;
		}

		select_back = 1;

	}

	memset(resp, 0, sizeof(resp));
	sr_len = sizeof(resp);
	ret = usim_select_file_byid(fileid, resp, sr_len);


	usim_dbg(LOG_INFO, "%s::%d EF resp[0]:%02x,resp[1]:%02x,resp[2]:%02x sr_len:%ld\n",
				__func__, __LINE__, resp[0],
				resp[1], resp[2], (long)sr_len);

	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d select file ioctl failed err value:%d\n",
					__func__, __LINE__, ret);
		goto error;
	}

	if ((resp[1] == 0x6a) && (resp[2] == 0x82)) {
		usim_dbg(LOG_ERROR, "%s::%d select file %x is not supported\n"
				, __func__, __LINE__, fileid);
		return VTQ_USIM_FILE_NOT_SUPPORTED;

	}

	if (((resp[1] != 0x90) || (resp[2] != 0x00)) && (resp[1] != 0x61) &&
		(resp[1] != 0x9F)) {
		usim_dbg(LOG_ERROR, "%s::%d Select file %x failed SW word:%02x %02x\n",
				__func__, __LINE__,
				fileid, resp[1], resp[2]);
		goto error;
	}

	ret = usim_update_binary(len, buffer, high_offset_addr,
			lower_offset_addr, sr_len, resp);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Update binary failed err value: %d\n",
				__func__, __LINE__, ret);
		goto error;
	}

	if ((resp[1] == 0x69) && (resp[2] == 0x82)) {
		sr_len = sizeof(resp);
		memset(resp, 0, sizeof(resp));
		ret = usim_admin_verify(sr_len, resp);

		if ((resp[1] !=  0x90) || (resp[2] !=  0x00)) {
			usim_dbg(LOG_ERROR, "%s::%d Admin Verify returned Unexpected status %02x %02x\n",
					__func__, __LINE__, resp[1], resp[2]);
			goto error;
		}

		sr_len = sizeof(resp);
		memset(resp, 0, sizeof(resp));

		ret = usim_update_binary(len, buffer, high_offset_addr,
				lower_offset_addr, sr_len, resp);
		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d Update binary failed err value: %d\n",
					__func__, __LINE__, ret);
			goto error;
		}

	}

	if (((resp[1] !=  0x90) || (resp[2] !=  0x00)) &&
			((resp[1] !=  0x60) || (resp[2] !=  0x90))) {

		usim_dbg(LOG_ERROR, "%s::%d Update Binary returned Unexpected status %02x %02x\n",
				__func__, __LINE__,
				resp[1], resp[2]);
		goto error;
	}

	if (select_back) {
		if (sim.type == 0)
			usim_select_file_byid(sim.dir_fileid, resp, sr_len);
		else if (sim.type == 1) {

			usim_dbg(LOG_INFO, "aid len:%d\n", aid_len);
			usim_dbg_hexdump(LOG_INFO, aid, aid_len);

			usim_select_file_byaid(aid, aid_len, resp, sr_len);
		}
	}

	return 0;

error:
	if (select_back) {
		if (sim.type == 0)
			usim_select_file_byid(sim.dir_fileid, resp, sr_len);
		else if (sim.type == 1) {

			usim_dbg(LOG_INFO, "aid len:%d\n", aid_len);
			usim_dbg_hexdump(LOG_INFO, aid, aid_len);

			usim_select_file_byaid(aid, aid_len, resp, sr_len);
		}
	}
	return -1;
}

int usim_parse_fcp_tmpl(byte *buffer, size_t buf_len, int *file_len,
			usim_rec_t *usim_rec)
{
	unsigned char *pos;
	unsigned char *end;

	pos = buffer+1;
	//end = pos + buf_len;

	if (*pos != USIM_FCP_TMPL_TAG) {
		usim_dbg(LOG_ERROR, " %s::%d FCP Template didnot start with right TAG\n",
					__func__, __LINE__);
		return -1;
	}
	//skip tag
	pos++;
	//pos[0] = length of fcp templ data
	end = pos + 1 + pos[0];
	//skip length
	pos++;

	while (pos + 1 < end) {

		switch (pos[0]) {

		case USIM_TLV_FILE_DESC:
			usim_dbg(LOG_INFO, "%s::%d File Descriptor TLV %04x %04x\n",
					__func__, __LINE__,
					pos[2], pos[1]);
			if (usim_rec) {
				usim_rec->len =
					(((unsigned short) pos[4] << 8) |
						((unsigned short) pos[5]));
				usim_rec->count = pos[6];

				usim_dbg(LOG_INFO, "%s::%d Record len:%d Record count:%d\n",
						__func__,
						__LINE__, usim_rec->len,
								usim_rec->count);
			}

			break;
		case USIM_TLV_FILE_ID:
			usim_dbg(LOG_INFO, "%s::%d File Identifier TLV %04x %04x\n",
					__func__, __LINE__,
					pos[2], pos[1]);
			break;
		case USIM_TLV_PROPR_INFO:
			usim_dbg(LOG_INFO, "%s::%d Proprietary information TLV %04x %04x\n",
					__func__, __LINE__, pos[2], pos[1]);
			break;
		case USIM_TLV_LIFE_CYCLE_STATUS:
			usim_dbg(LOG_INFO, "%s::%d Life Cycle Status Integer TLV %04x %04x\n",
					__func__, __LINE__, pos[2], pos[1]);
			break;

		case USIM_TLV_FILE_SIZE:
			usim_dbg(LOG_INFO, "%s::%d File size TLV %04x %04x\n",
					__func__, __LINE__, pos[2], pos[1]);

			if (file_len) {
				if (((pos[1] == 1) || (pos[1] == 2))
						&& file_len) {
					if (pos[1] == 1)
						*file_len = (int) pos[2];
					else
						*file_len =
							((int) pos[2] << 8) |
							(int) pos[3];

					usim_dbg(LOG_INFO, "%s::%d file_size=%d\n",
						__func__,
						__LINE__, *file_len);
				}
			}
			break;

		case USIM_TLV_PIN_STATUS_TEMPLATE:

			usim_dbg(LOG_INFO, "%s::%d PIN Status Template %04x %04x\n",
					__func__, __LINE__,
					pos[2], pos[1]);
			break;

		case USIM_TLV_SHORT_FILE_ID:

			usim_dbg(LOG_INFO, "%s::%d Short File Identifier (SFI) TLV %04x %04x\n",
					__func__, __LINE__, pos[2], pos[1]);
			break;

		case USIM_TLV_SECURITY_ATTR_8B:
		case USIM_TLV_SECURITY_ATTR_8C:
		case USIM_TLV_SECURITY_ATTR_AB:
			usim_dbg(LOG_INFO, "%s::%d USIM: Security attribute %04x %04x\n",
					__func__, __LINE__,
					pos[2], pos[1]);
			break;

		default:
			usim_dbg(LOG_INFO, "%s::%d Unrecongnized TLV %04x %04x\n",
					__func__, __LINE__,
					pos[0], 2 + pos[1]);
	}
		//2bytes = tag(1byte) + len(1byte)
		pos += 2 + pos[1];

		if (pos == end)
			return 0;
	}
	return -1;
}

int usim_read_transparent(word fileid, int *len, byte *buf, int dir_fileid)
{
	unsigned char buffer[256];
	int file_size;
	int blen = 0;
	int ret = -1;
	unsigned char resp[3];//select response data buffer;
	size_t sr_len;//select response data length;
	unsigned char *pData;
	int select_back = 0;

	if (dir_fileid) {
		memset(resp, 0, sizeof(resp));
		sr_len = sizeof(resp);

		ret = usim_select_file_byid(dir_fileid, resp, sr_len);
		usim_dbg(LOG_INFO, "%s::%d DF resp[0]:%02x,resp[1]:%02x, resp[2]:%02x sr_len:%ld\n",
				__func__,
				__LINE__, resp[0], resp[1], resp[2],
				(long)sr_len);
		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d select file ioctl failed err value:%d\n",
					__func__, __LINE__,
					ret);
			return ret;
		}

		if ((resp[1] == 0x6a) && (resp[2] == 0x82)) {
			usim_dbg(LOG_ERROR, "%s::%d Select file %x not supported SW word:%02x %02x\n",
					__func__, __LINE__, dir_fileid,
					resp[1], resp[2]);
			return VTQ_USIM_FILE_NOT_SUPPORTED;
		}

		if (((resp[1] != 0x90) || (resp[2] != 0x00)) &&
			(resp[1] != 0x61) && (resp[1] != 0x9F)) {
			usim_dbg(LOG_ERROR, "%s::%d Select file %x failed SW word:%02x %02x\n",
					__func__,
					__LINE__, dir_fileid, resp[1], resp[2]);
			return -1;
		}
		select_back = 1;

	}


	memset(resp, 0, sizeof(resp));
	sr_len = sizeof(resp);
	ret = usim_select_file_byid(fileid, resp, sr_len);


	usim_dbg(LOG_INFO, "%s::%d EF resp[0]:%02x,resp[1]:%02x,resp[2]:%02x sr_len:%ld\n",
			__func__, __LINE__, resp[0], resp[1],
			resp[2], (long)sr_len);

	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d select file ioctl failed err value:%d\n",
				__func__, __LINE__, ret);
		goto error;
	}

	if ((resp[1] == 0x6a) && (resp[2] == 0x82)) {
		usim_dbg(LOG_ERROR, "%s::%d select file %x is not supported\n",
				__func__, __LINE__, fileid);
		return VTQ_USIM_FILE_NOT_SUPPORTED;

	}

	if (((resp[1] != 0x90) || (resp[2] != 0x00)) && (resp[1] != 0x61) &&
		(resp[1] != 0x9F)) {
		usim_dbg(LOG_ERROR, "%s::%d Select file %x failed SW word:%02x %02x\n",
				__func__, __LINE__,
				fileid, resp[1], resp[2]);
		goto error;
	}

	blen = resp[2];
	memset(buffer, 0, sizeof(buffer));
	ret = usim_get_response(blen, buffer);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d get response ioctl failed err value:%d\n",
					__func__, __LINE__, ret);
		goto error;
	}

	if ((buffer[blen+1] != 0x90) || (buffer[blen+2] != 0x00)) {
		usim_dbg(LOG_ERROR, "%s::%d GET Response returned Unexpected status %02x %02x\n",
				__func__, __LINE__,
				buffer[blen + 1], buffer[blen + 2]);
		goto error;
	}


	usim_dbg(LOG_INFO, "%s::%d GET RESPONSE: %02x %02x\n", __func__,
				__LINE__, buffer[blen + 1], buffer[blen + 2]);

	if (sim.type == USIM_TYPE) {
		ret = usim_parse_fcp_tmpl(buffer, sizeof(buffer), &file_size,
						NULL);

		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d parsing fcp tmpl failed err value:%d\n",
					__func__, __LINE__,
					ret);
			goto error;
		}

		*len = file_size;
	} else {

		*len = (buffer[3] << 8) | buffer[4];
	}

	pData = (unsigned char *)malloc(*len + 3);
	if (pData == NULL) {
		usim_dbg(LOG_ERROR, "%s::%d Memory allocation failed\n",
				__func__, __LINE__);
		goto error;
	}

	ret = usim_read_binary(*len, pData);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d READ BINARY Transmit failed err value:%d\n",
					__func__, __LINE__, ret);
		free(pData);
		goto error;
	}

	if ((pData[*len + 1] != 0x90) || (pData[*len + 2] != 0x00)) {
		usim_dbg(LOG_ERROR, "%s::%d READ BINARY returned Unexpected status %02x %02x\n",
				__func__, __LINE__,
				pData[*len + 1], pData[*len + 2]);
		free(pData);
		goto error;
	}

	memcpy(buf, pData + 1, *len);

	free(pData);

	if (select_back) {
		if (sim.type == 0)
			usim_select_file_byid(sim.dir_fileid, resp, sr_len);
		else if (sim.type == 1) {

			usim_dbg(LOG_INFO, "aid len:%d\n", aid_len);
			usim_dbg_hexdump(LOG_INFO, aid, aid_len);

			usim_select_file_byaid(aid, aid_len, resp, sr_len);
		}
	}

	return 0;
error:
	if (select_back) {
		if (sim.type == 0)
			usim_select_file_byid(sim.dir_fileid, resp, sr_len);
		else if (sim.type == 1)
			usim_select_file_byaid(aid, aid_len, resp, sr_len);
	}
	return -1;
}

int usim_write_linearfixed(word fileid, int len, byte *buf, int dir_fileid)
{
	unsigned char buffer[256];
	int blen = 0, i = 0;
	usim_rec_t usim_record;
	int ret = -1;
	unsigned char resp[3];//select response data buffer;
	size_t sr_len;//select response data length;
	int select_back = 0;

	if (dir_fileid) {
		memset(resp, 0, sizeof(resp));
		sr_len = sizeof(resp);

		ret = usim_select_file_byid(dir_fileid, resp, sr_len);
		usim_dbg(LOG_INFO, "%s::%d DF resp[0]:%02x,resp[1]:%02x, resp[2]:%02x sr_len:%ld\n",
				__func__,
				__LINE__, resp[0], resp[1], resp[2],
				(long)sr_len);
		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d select file ioctl failed err value:%d\n",
					__func__, __LINE__,
					ret);
			return ret;
		}

		if ((resp[1] == 0x6a) && (resp[2] == 0x82)) {
			usim_dbg(LOG_ERROR, "%s::%d Select file %x not supported SW word:%02x %02x\n",
					__func__, __LINE__, dir_fileid,
					resp[1], resp[2]);
			return VTQ_USIM_FILE_NOT_SUPPORTED;
		}

		if (((resp[1] != 0x90) || (resp[2] != 0x00)) &&
			(resp[1] != 0x61) && (resp[1] != 0x9F)) {
			usim_dbg(LOG_ERROR, "%s::%d Select file %x failed SW word:%02x %02x\n",
					__func__,
					__LINE__, dir_fileid, resp[1], resp[2]);
			return -1;
		}
		select_back = 1;
	}

	sr_len = sizeof(resp);
	ret = usim_select_file_byid(fileid, resp, sr_len);


	usim_dbg(LOG_INFO, "%s::%d DF resp[0]:%02x,resp[1]:%02x,resp[2]:%02x sr_len:%ld\n",
				__func__, __LINE__, resp[0],
				resp[1], resp[2], (long)sr_len);

	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Select file ioctl failed err value:%d\n",
				__func__, __LINE__, ret);
		goto error;
	}

	if ((resp[1] == 0x6a) && (resp[2] == 0x82)) {
		usim_dbg(LOG_ERROR, "%s::%d select file %x is not supported\n",
				__func__, __LINE__, fileid);
		return VTQ_USIM_FILE_NOT_SUPPORTED;

	}

	if (((resp[1] != 0x90) || (resp[2] != 0x00)) && (resp[1] != 0x61) &&
		(resp[1] != 0x9F)) {
		usim_dbg(LOG_ERROR, "%s::%d Select file %x failed SW word:%02x %02x\n",
				__func__, __LINE__,
				fileid, resp[1], resp[2]);
		goto error;
	}

	blen = resp[2];
	memset(buffer, 0, sizeof(buffer));
	ret = usim_get_response(blen, buffer);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d get response ioctl failed err value:%d\n",
				__func__, __LINE__, ret);
		goto error;
	}
	if ((buffer[blen+1] != 0x90) || (buffer[blen+2] != 0x00)) {
		usim_dbg(LOG_ERROR, "%s::%d GET Response Unexpected status %02x %02x\n",
				__func__, __LINE__,
				buffer[blen + 1], buffer[blen + 2]);
		goto error;
	}


	usim_dbg(LOG_INFO, "%s::%d GET RESPONSE: %02x %02x %02x %02x %02x\n",
				__func__, __LINE__, buffer[3], buffer[4],
				buffer[blen], buffer[blen+1],
				buffer[blen + 2]);

	if (sim.type == GSM_SIM_TYPE) {
		fci_current = ((gsm_fci_ef_t *)buffer);
		usim_record.len = fci_current->recordsize;
		usim_record.count = ((fci_current->filesize[0] << 8) +
			fci_current->filesize[1]) / fci_current->recordsize;
	} else {

		ret = usim_parse_fcp_tmpl(buffer, sizeof(buffer), NULL,
			&usim_record);
		if (ret < 0)
			goto error;
	}

	for (i = 1; i <= usim_record.count; i++) {

		memset(resp, 0, sizeof(resp));
		sr_len = sizeof(resp);
		ret = usim_update_record(i, len, buf, sr_len, resp);
		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d update record failed err value:%d\n",
					__func__, __LINE__, ret);
			goto error;
		}

		if (((resp[1] != 0x90) || (resp[2] != 0x00)) &&
				((resp[1] != 0x60) || (resp[2] != 0x90))) {
			usim_dbg(LOG_ERROR, "%s::%d update record Unexpected status %02x %02x\n",
					__func__,
					__LINE__, resp[1], resp[2]);
			goto error;
		}

	}

	if (select_back) {
		if (sim.type == 0)
			usim_select_file_byid(sim.dir_fileid, resp, sr_len);
		else if (sim.type == 1) {

			usim_dbg(LOG_INFO, "aid len:%d\n", aid_len);
			usim_dbg_hexdump(LOG_INFO, aid, aid_len);

			usim_select_file_byaid(aid, aid_len, resp, sr_len);
		}
	}

	return 0;
error:
	if (select_back) {
		if (sim.type == 0)
			usim_select_file_byid(sim.dir_fileid, resp, sr_len);
		else if (sim.type == 1) {

			usim_dbg(LOG_INFO, "aid len:%d\n", aid_len);
			usim_dbg_hexdump(LOG_INFO, aid, aid_len);

			usim_select_file_byaid(aid, aid_len, resp, sr_len);
		}
	}

	return -1;
}

int usim_read_linearfixed(word fileid, int *len, byte *buf, int dir_fileid)
{
	unsigned char buffer[256];
	unsigned char buffer_rec[256];
	int blen = 0, i = 0;
	usim_rec_t usim_record;
	int ret;
	unsigned char resp[3];//select response data buffer;
	size_t sr_len;//select response data length;
	int select_back = 0;

	if (dir_fileid) {
		memset(resp, 0, sizeof(resp));
		sr_len = sizeof(resp);

		ret = usim_select_file_byid(dir_fileid, resp, sr_len);
		usim_dbg(LOG_INFO, "%s::%d DF resp[0]:%02x,resp[1]:%02x, resp[2]:%02x sr_len:%ld\n",
				__func__,
				__LINE__, resp[0], resp[1], resp[2],
				(long)sr_len);
		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d select file ioctl failed err value:%d\n",
					__func__, __LINE__,
					ret);
			return ret;
		}

		if ((resp[1] == 0x6a) && (resp[2] == 0x82)) {
			usim_dbg(LOG_ERROR, "%s::%d Select file %x not supported SW word:%02x %02x\n",
					__func__, __LINE__, dir_fileid,
					resp[1], resp[2]);
			return VTQ_USIM_FILE_NOT_SUPPORTED;
		}

		if (((resp[1] != 0x90) || (resp[2] != 0x00)) &&
			(resp[1] != 0x61) && (resp[1] != 0x9F)) {
			usim_dbg(LOG_ERROR, "%s::%d Select file %x failed SW word:%02x %02x\n",
					__func__,
					__LINE__, dir_fileid, resp[1], resp[2]);
			return -1;
		}
		select_back = 1;
	}

	memset(resp, 0, sizeof(resp));
	sr_len = sizeof(resp);
	ret = usim_select_file_byid(fileid, resp, sr_len);

	usim_dbg(LOG_INFO, "%s::%d DF resp[0]:%02x,resp[1]:%02x,resp[2]:%02x sr_len:%ld\n",
				__func__, __LINE__,
				resp[0], resp[1], resp[2], (long)sr_len);

	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Select file ioctl failed err value:%d\n",
				__func__, __LINE__, ret);
		goto error;
	}

	if ((resp[1] == 0x6a) && (resp[2] == 0x82)) {
		usim_dbg(LOG_ERROR, "%s::%d select file %x is not supported\n",
				__func__, __LINE__, fileid);
		return VTQ_USIM_FILE_NOT_SUPPORTED;

	}

	if (((resp[1] != 0x90) || (resp[2] != 0x00)) && (resp[1] != 0x61) &&
		(resp[1] != 0x9F)) {
		usim_dbg(LOG_ERROR, "%s::%d Select file %x failed SW word:%02x %02x\n",
				__func__, __LINE__,
				fileid, resp[1], resp[2]);
		goto error;
	}


	blen = resp[2];
	memset(buffer, 0, sizeof(buffer));
	ret = usim_get_response(blen, buffer);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d get response ioctl failed err value:%d\n",
					__func__, __LINE__, ret);
		goto error;
	}
	if ((buffer[blen+1] != 0x90) || (buffer[blen+2] != 0x00)) {
		usim_dbg(LOG_ERROR, "%s::%d GET Response Unexpected status %02x %02x\n",
				__func__, __LINE__,
					buffer[blen + 1], buffer[blen + 2]);
		goto error;
	}

	usim_dbg(LOG_INFO, "%s::%d GET RESPONSE: %02x %02x %02x %02x %02x\n",
			__func__, __LINE__, buffer[3], buffer[4], buffer[blen],
			buffer[blen+1], buffer[blen + 2]);

	if (sim.type == GSM_SIM_TYPE) {
		fci_current = ((gsm_fci_ef_t *)buffer);
		usim_record.len = fci_current->recordsize;
		usim_record.count = ((fci_current->filesize[0] << 8) +
			fci_current->filesize[1])/fci_current->recordsize;
	} else {
		ret = usim_parse_fcp_tmpl(buffer, sizeof(buffer), NULL,
				&usim_record);
		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d parse fcp template failed\n",
					__func__, __LINE__);
			goto error;
		}
	}


	for (i = 1; i <= usim_record.count; i++) {

		ret = usim_read_record(i, usim_record.len, buffer_rec);

		if (ret < 0) {
			usim_dbg(LOG_ERROR, "%s::%d read record failed err value:%d\n",
					__func__, __LINE__, ret);
			goto error;
		}
		if ((buffer_rec[usim_record.len + 1] !=  0x90) ||
				(buffer_rec[usim_record.len + 2] !=  0x00)) {
			usim_dbg(LOG_ERROR, "%s::%d read record Unexpected status %02x %02x\n",
					__func__,
					__LINE__, buffer_rec[*len + 1],
					buffer_rec[*len + 2]);
			goto error;
		}
		*len = usim_record.len;
		memcpy(buf, buffer_rec+1, usim_record.len);

	}

	if (select_back) {
		if (sim.type == 0)
			usim_select_file_byid(sim.dir_fileid, resp, sr_len);
		else if (sim.type == 1) {

			usim_dbg(LOG_INFO, "aid len:%d\n", aid_len);
			usim_dbg_hexdump(LOG_INFO, aid, aid_len);

			usim_select_file_byaid(aid, aid_len, resp, sr_len);
		}
	}

	return 0;
error:
	if (select_back) {
		if (sim.type == 0)
			usim_select_file_byid(sim.dir_fileid, resp, sr_len);
		else if (sim.type == 1) {

			usim_dbg(LOG_INFO, "aid len:%d\n", aid_len);
			usim_dbg_hexdump(LOG_INFO, aid, aid_len);

			usim_select_file_byaid(aid, aid_len, resp, sr_len);
		}
	}

	return -1;
}

int usim_onchip_auth(vtq_usim_lv_t *rand_chal, vtq_usim_lv_t *autn,
		vtq_usim_lv_t *auts, vtq_usim_lv_t *res, vtq_usim_lv_t *ck,
		vtq_usim_lv_t *ak)
{
	uint8_t buffer[256];
	int len = 0;
	int ret;
	uint8_t resp[3];//select response data buffer;
	size_t sr_len;//select response data length;

	memset(buffer, 0, sizeof(buffer));
	len = rand_chal->length + autn->length + 2;
	buffer[0] = rand_chal->length;
	memcpy(buffer + 1, rand_chal->value, rand_chal->length);
	buffer[rand_chal->length + 1] = autn->length;
	memcpy(buffer + rand_chal->length + 2, autn->value, autn->length);

	memset(resp, 0, sizeof(resp));
	sr_len = sizeof(resp);
	ret = usim_authenticate(len, buffer, resp, sr_len);
	usim_dbg(LOG_INFO, "%s::%d auth command response resp[0]:%x resp[1]:%x resp[2]:%x\n",
			__func__, __LINE__,
			resp[0], resp[1], resp[2]);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Ioctl related to authentication failed err value:%d\n",
				__func__, __LINE__,
				ret);
		return ret;
	}

	memset(buffer, 0, sizeof(buffer));
	ret = usim_get_response(sizeof(buffer), buffer);
	if (ret < 0) {
		usim_dbg(LOG_ERROR, "%s::%d Ioctl related to get response failed err value:%d\n",
				__func__, __LINE__,
				ret);
		return ret;
	}

	usim_dbg(LOG_INFO, "%s::%d response command buffer %x %x %x\n",
			__func__, __LINE__, buffer[0], buffer[1], buffer[2]);
	if (buffer[0] == 0xDB) {
		res->length = buffer[1];
		memcpy(res->value, buffer+2, res->length);
		ck->length = buffer[2+res->length+1];
		memcpy(ck->value, buffer+2+res->length+1, ck->length);
		ak->length = buffer[2+res->length+1+ck->length+1];
		memcpy(ak->value, buffer+2+res->length+1+ck->length+1,
				ak->length);
		return 0;
	} else if (buffer[0] == 0xDC) {
		auts->length = buffer[1];
		memcpy(auts->value, buffer+2, auts->length);
		return 2;
	} else
		return 1;
}
