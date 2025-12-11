/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2021-2022 NXP
 *
 * @usim_apdu.h
 * @brief Function prototypes for usim apdu commands/response.
 *
 * This contains the prototypes for usim apdu commands/response
 * functions and eventually any macros, constants,
 * or global variables you will need.
 */

#ifndef _USIM_APDU_H
#define _USIM_APDU_H

/**
 * @brief Value of file descriptor TAG.
 */
#define USIM_TLV_FILE_DESC		0x82

/**
 * @brief Value of file identifier TAG.
 */
#define USIM_TLV_FILE_ID		0x83

/**
 * @brief Value of directory file name TAG.
 */
#define USIM_TLV_DF_NAME		0x84

/**
 * @brief Value of Proprietary information TAG.
 */
#define USIM_TLV_PROPR_INFO		0xA5

/**
 * @brief Value of life cycle status integer TAG.
 */
#define USIM_TLV_LIFE_CYCLE_STATUS	0x8A

/**
 * @brief Value of file size TAG.
 */
#define USIM_TLV_FILE_SIZE		0x80

/**
 * @brief Value of total file size TAG.
 */
#define USIM_TLV_TOTAL_FILE_SIZE	0x81

/**
 * @brief Value of pin status template TAG.
 */
#define USIM_TLV_PIN_STATUS_TEMPLATE	0xC6

/**
 * @brief Value of short file identifier TAG.
 */
#define USIM_TLV_SHORT_FILE_ID		0x88

/**
 * @brief Value of security attribute1 TAG.
 */
#define USIM_TLV_SECURITY_ATTR_8B	0x8B

/**
 * @brief Value of security attribute2 TAG.
 */
#define USIM_TLV_SECURITY_ATTR_8C	0x8C

/**
 * @brief Value of security attribute3 TAG.
 */
#define USIM_TLV_SECURITY_ATTR_AB	0xAB

/**
 * @brief File identifier of master file.
 */
#define USIM_FILE_EF_DIR   0x2F00

/**
 * @brief typedef for unsigned short.
 */
typedef unsigned short word;

/**
 * @brief typedef for unsigned char.
 */
typedef unsigned char byte;

/**
 * @brief Value of GSM type.
 */
#define GSM_SIM_TYPE 0

/**
 * @brief Value of 5G SIM type.
 */
#define USIM_TYPE 1

/** @brief Reads data from USIM file
 *
 *  This function reads the data from the file of type
 *  transparent. This contains data as sequence of bytes.
 *
 *  @param[in] fileid id of the file to read from USIM.
 *  @param[out] *len size of the file.
 *  @param[out] buffer which holds data of the file after reading from USIM.
 *  @param[in] directory fileid to select before selecting
 *              elementary file.
 *  @return 0 if success or -1 if failure.
 */
int usim_read_transparent(word fileid, int *len, byte *buffer, int dir_fileid);

/** @brief Gets the SUCI from USIM.
 *
 *  This function uses the GET IDENTITY command to read
 *  suci from USIM.
 *
 *  @param[in] len   length of the suci file to read from USIM.
 *  @param[out] suci_buf buffer which holds suci data after reading from USIM.
 *  @return 0 if success or -1 if failure.
 */
int usim_get_suci(int len, byte *suci_buf);

/** @brief Writes data to USIM file
 *
 *  This function writes the data tothe file of type
 *  transparent. This contains data as sequence of bytes.
 *
 *  @param[in] fileid id of the file to to write to USIM.
 *  @param[in] offset_high higher offset address.
 *  @param[in] offset_low lower offset address.
 *  @param[out] len size of the data to write to file.
 *  @param[out] buffer which holds data to write to USIM.
 *  @param[in] directory fileid to select before selecting
 *		elementary file.
 *
 *  @return 0 if success or -1 if failure.
 */
int usim_write_transparent(word fileid, int len, byte *buffer,
		byte offset_high, byte offset_low, int dir_fileid);


/** @brief Reads data from USIM file
 *
 *  This function reads the data from the file of type
 *  linear fixed. This contains data as records.
 *
 *  @param[in] fileid id of the file to read from USIM.
 *  @param[out] *len size of the file.
 *  @param[out] buffer which holds data of the file after reading from USIM.
 *  @param[in] directory fileid to select before selecting
 *              elementary file.
 *  @return 0 if success or -1 if failure.
 */
int usim_read_linearfixed(word fileid, int *len, byte *buf, int dir_fileid);

/** @brief writes data to USIM file
 *
 *  This function writes the data to the file of type
 *  linear fixed. This contains data as records.
 *
 *  @param[in] fileid id of the file to write to USIM.
 *  @param[out] len of the data to write.
 *  @param[out] buffer which holds data to write to USIM.
 *  @param[in]  directory fileid to select before selecting
 *		elementary file.
 *  @return 0 if success or -1 if failure.
 */
int usim_write_linearfixed(word fileid, int len, byte *buf, int dir_filid);


/** @brief Writes data to USIM file
 *
 *  This function writes the data to the file of type
 *  transparent. This contains data as sequence of bytes.
 *
 *  @param[in] len length of the data to write to USIM.
 *  @param[in] buf buffer which holds the data to write.
 *  @param[in] off_high higher offset address.
 *  @param[in] off_low lower offset address.
 *  @param[out] resp_len size of the response data.
 *  @param[out] resp_buffer buffer which holds response data.
 *
 *  @return 0 if success or -1 if failure.
 */
int usim_update_binary(int len, byte *buf, byte off_high, byte off_low,
		int resp_len, byte *resp_buf);

/** @brief Select gsm file
 *
 *  This function selects the gsm directory file.
 *
 *  @return 0 if success or -1 if failure.
 */
int usim_select_gsm(void);

/** @brief Select AID file
 *
 *  This function selects the aid file.
 *
 *  @return 0 if success or -1 if failure.
 */
int usim_select_aid(void);

/** @brief USIM online auth
 *
 * This function performs the onchip authentication.
 *
 * @return 0 if success or -1 if failure
 */
int usim_onchip_auth(vtq_usim_lv_t *rand_chal, vtq_usim_lv_t *autn,
		vtq_usim_lv_t *auts, vtq_usim_lv_t *res, vtq_usim_lv_t *ck,
		vtq_usim_lv_t *ak);

/** @brief Reads data from flash
 *
 * This function reads data from flash with given offset and length.
 * @return 0 if success or -1 if failure
 */

int read_from_flash(int fd, off_t offset, size_t len, uint8_t *buf);
#endif /* _USIM_APDU_H */
