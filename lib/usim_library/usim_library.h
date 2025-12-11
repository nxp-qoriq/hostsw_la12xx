/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2021-2022 NXP
 *
 * @usim_library.h
 * @brief Function prototypes for usim library.
 *
 * This contains the prototypes for usim library
 * and eventually any macros, constants,
 * or global variables you will need.
 *
 * @author Nagesh Koneti
 */

#ifndef _USIM_LIBRARY_H
#define _USIM_LIBRARY_H

#include <stdint.h>
#include <stdlib.h>

/**
 * @brief debug macro for logs.
 *
 */
#define usim_dbg(log_level, fmt, ...) \
			do { \
			if (debug & log_level) { \
			printf(fmt, ##__VA_ARGS__); } \
			} while (0)

/**
 * @brief hexdump macro to dump data in hexadecimal format.
 */
#define usim_dbg_hexdump(log_level, b, l) \
			do { \
			if (debug & log_level) { \
			hex_dump(b, l); } \
			} while (0)

/**
 * @brief chardump macro to dump data in charecter format.
 */
#define usim_dbg_chardump(log_level, b, l) \
			do { \
			if (debug & log_level) { \
			char_dump(b, l); } \
			} while (0)

/**
 * @brief macro for error logs.
 */
#define LOG_ERROR 1

/**
 * @brief macro for info logs.
 */
#define LOG_INFO 2

/**
 * @brief macro for all infor/error logs.
 */
#define LOG_ALL 3

/**
 * @brief macro for file not supported error.
 */
#define VTQ_USIM_FILE_NOT_SUPPORTED -2

/**
 * @brief Maximum length of UST Table in bytes.
 */
#define UST_TABLE_MAX_LEN 20

/**
 * @brief IMEISV addresses in SPI Flash */
#define USIM_SPI_IMEISV_ADD 0x3E00004

/**
 * @brief Admin PIN addresses in SPI Flash */
#define USIM_SPI_ADMIN_PIN_ADD 0x3E000F8

/**
 * @brief File id of Master file.
 */
#define USIM_MF_FILEID 0x3F00

/**
 * @brief File id of ICCID.
 */
#define USIM_ICCID_FILEID 0x2FE2

/**
 * @brief GSM Directory file.
 */
#define GSM_DF_GSM 0x7f20

/**
 * @brief Number of bytes for TAG.
 */
#define TAG_BYTE_SIZE 1

/**
 * @brief Number of bytes for length.
 */
#define LENGTH_BYTE_SIZE 1

/**
 * @brief NAS config signalling priority TAG.
 */
#define NAS_SIG_PRIO_TAG 0x80

/**
 * @brief NAS NMO I BEHAVIOUR TAG.
 */
#define NAS_NMO_BEH_TAG 0x81

/**
 * @brief NAS Attach with IMSI TAG.
 */
#define NAS_AT_IMSI_TAG 0x82

/**
 * @brief NAS Minimum Periodic Search Timer TAG.
 */
#define NAS_MPST_TAG 0x83

/**
 * @brief NAS Extended Access barring TAG.
 */
#define NAS_EAB_TAG 0x84

/**
 * @brief NAS Timer T3245 Behaviour TAG.
 */
#define NAS_TIM_BEH_TAG 0x85

/**
 * @brief NAS Override NAS signalling low priority TAG.
 */
#define NAS_OD_SIG_PRIO_TAG 0x86

/**
 * @brief NAS Override Extended access barring TAG.
 */
#define NAS_OD_EAB_TAG 0x87

/**
 * @brief NAS Fast First Higher Priority PLMN Search TAG.
 */
#define NAS_FFHPS_TAG 0x88

/**
 * @brief NAS E-UTRA Disabling Allowed for EMM cause #15 TAG.
 */
#define NAS_DA_TAG 0x89

/**
 * @brief NAS SM_RetryWaitTime TAG.
 */
#define NAS_SM_RWT_TAG 0x8A

/**
 * @brief SM_RetryAtRATChange TAG.
 */
#define NAS_SW_RRAT_TAG 0x8B

/**
 * @brief NAS Default_DCN_ID TAG.
 */
#define NAS_DCN_TAG 0x8C

/**
 * @brief NAS Exception Data Reporting Allowed TAG.
 */
#define NAS_EDRA_TAG 0x8D

/**
 * @brief PLMN Network Name fullname TAG.
 */
#define PLMN_NN_FULLNAME_TAG 0x43

/**
 * @brief PLMN Network Name shortname TAG.
 */
#define PLMN_NN_SHORTNAME_TAG 0x45

/**
 * @brief 5G Authentication key AUSF TAG.
 */
#define AUTH_AUSF_TAG 0x80

/**
 * @brief 5G Authentication key SEAF TAG.
 */
#define AUTH_SEAF_TAG 0x81

/**
 * @brief 5GS NAS security context TAG.
 */
#define NAS_SEC_CTX_TAG 0xA0

/**
 * @brief 5GS NAS secuirty context ngKSI TAG.
 */
#define NAS_SEC_NGKSI_TAG 0x80

/**
 * @brief 5GS NAS security context KAMF TAG.
 */
#define NAS_SEC_KAMF_TAG 0x81

/**
 * @brief 5GS NAS secuirty context Uplink NAS count TAG.
 */
#define NAS_UL_NASCNT_TAG 0x82

/**
 * @brief 5GS NAS security context Down Link NAS count TAG.
 */
#define NAS_DL_NASCNT_TAG 0x83

/**
 * @brief 5GS NAS security context Identifiers of selected NAS integrity and
 * encryption algorithms Tag.
 */
#define NAS_INT_ENC_TAG 0x84

/**
 * @brief 5GS NAS secuiryt context Identifiers of selected EPS NAS
 * integrity and encryption algorithms for use after mobility to
 * EPS Tag.
 */
#define NAS_EPS_INT_ENC_TAG 0x85

#define USIM_5GS_DIRFILEID 0x5FC0

/**
 * @brief File id of NASCONFIG.
 */
#define USIM_NASCONFIG_FILEID 0x6FE8

/**
 * @brief File id of SUCI Calculation Info.
 */
#define USIM_SUCI_CALC_INFO_FILEID 0x4F07

/**
 * @brief File id of Routing Indicator.
 */
#define USIM_RID_FILEID 0x4F0A

/**
 * @brief Length of Routing Indicator.
 */
#define USIM_RID_LENGTH 2

/**
 * @brief File id of IMSI.
 */
#define USIM_IMSI_FILEID 0x6F07

/**
 * @brief Length of IMSI data.
 */
#define USIM_IMSI_LENGTH 8

/**
 * @brief File id of HPLMN.
 */
#define USIM_HPLMNACT_FILEID 0x6F62

/**
 * @brief File id of Equivalent HPLMN.
 */
#define USIM_EHPLMN_FILEID 0x6FD9

/**
 * @brief File id of User Controlled PLMN.
 */
#define USIM_UPLMNACT_FILEID 0x6F60

/**
 * @brief File id of Operator Controlled PLMN.
 */
#define USIM_OPCONTRLACT_FILEID 0x6F61

/**
 * @brief length  of 5GS operator plmn list.
 */
#define USIM_OPPLMN_LENGTH 10

/**
 * @brief File id of 5GS operator plmn list.
 */
#define USIM_OPPLMN_FILEID 0x4F08

/**
 * @brief File id of Forbidden PLMN.
 */
#define USIM_FPLMN_FILEID 0x6F7B

/**
 * @brief length of Unified Access Control.
 */
#define USIM_UAC_LENGTH 4

/**
 * @brief File id of Unified Access Control.
 */
#define USIM_UAC_FILEID 0x4F06

/**
 * @brief Length of Access Control Class.
 */
#define USIM_ACC_LENGTH 2

/**
 * @brief File id of Access Control Class.
 */
#define USIM_ACC_FILEID 0x6F78

/**
 * @brief Length of 5GS 3GPP Location info.
 */
#define USIM_5GS_LOCATION_INFO_LENGTH 20

/**
 * @brief File id of 5GS 3GPP Location info.
 */
#define USIM_5GS_LOCATION_INFO_FILEID 0x4F01

/**
 * @brief Length of Administrative Data.
 */
#define USIM_ADMIN_LENGTH 4

/**
 * @brief File id of Administrative Data.
 */
#define USIM_ADMIN_FILEID 0x6FAD

/**
 * @brief File id of PLMN network name.
 */
#define USIM_PLMN_NN_FILEID 0x6FC5

/**
 * @brief File id of NAS Security context.
 */
#define USIM_NASSECCTX_FILEID 0x4F03

/**
 * @brief Length of NAS Security context.
 */
#define USIM_NASSECCTX_LENGTH 43

/**
 * @brief File id of IMEISV.
 */
#define USIM_IMEISV_FILEID 0x0000

/**
 * @brief File id of NAS authentication key's.
 */
#define USIM_NAS_AUTH_KEYS_FILEID 0x4F05

/**
 * @brief TAG of AUSF NAS authentication key.
 */
#define USIM_NAS_AUTH_KEY_AUSF_TAG 0x80

/**
 * @brief TAG of SEAF NAS authentication key.
 */
#define USIM_NAS_AUTH_KEY_SEAF_TAG 0x81

/**
 * @brief Length of PLMN identifier.
 */
#define USIM_PLMNID_LENGTH 3

/**
 * @brief Length of Access Technology identifier.
 */
#define USIM_ACTID_LENGTH 2

/**
 * @brief Length of Global Unique Temporary identifier.
 */
#define USIM_GUTI_LENGTH 13

/**
 * @brief Length of Tracking Area Code.
 */
#define USIM_TAC_LENGTH 3

/**
 * @brief Maximum Number of PLMN's.
 */
#define USIM_PLMN_MAX_COUNT 100

/**
 * @brief Maximum number bytes of PLMN data.
 */
#define USIM_PLMN_MAX_LENGTH 300

/**
 * @brief Maximum number bytes of Access point control list.
 */
#define ACL_MAX_LIST 8

/**
 * @brief File id of Access point name control list.
 */
#define USIM_ACL_FILEID 0x6F57

/**
 * @brief byte size of ACL count field.
 */
#define ACL_COUNT_BYTE_SIZE 1

/**
 * @brief Access Point name control list TAG.
 */
#define ACL_TAG 0xDD

/** @brief Enable/disable debug logs
 *
 *
 *	If enable value is 0 it will disable debug logging.if the
 *	enable value is 1, error logs will be enabled, if the
 *	enable value is 2, info logs will be enabled, if the
 *	enable value is 3, both error and info logs will be
 *	enabled.
 *
 *  @param[in] s flag to enable/disable debug logs.
 *  @return Void.
 */
void vtq_usim_debug_set(int enable);

/** @brief prints the data present in the buffer
 *         in hexadecimal format.
 *
 *  It prints the data in the buffer upto the length.
 *  space is printed after every 4bytes and new line
 *  after every 16bytes.
 *
 *  @param[in] s flag to enable/disable debug logs.
 *  @return Void.
 */
void hex_dump(unsigned char *buffer, int len);

/** @brief prints the data present in the buffer
 *         in character format.
 *
 *  It prints the data in the buffer upto the length.
 *  new line after every 16bytes.
 *
 *  @param[in] s flag to enable/disable debug logs.
 *  @return Void.
 */
void char_dump(unsigned char *buffer, int len);

#pragma pack(push, 1)

/**
 * @brief A structure to represent data in tlv format.
 */
typedef struct {
	uint8_t tag; /**< Type of data to be processed. */
	uint32_t  length; /**< Length of data to be processed. */
	uint8_t  *value; /**< Value of the data to processed. */
} vtq_usim_tlv_t;

/**
 * @brief A structure to represent sim type.
 */
typedef struct {
	int type; /**< Type of SIM , 1 for 5G or 0 for 4G. */
	unsigned short dir_fileid; /**< Directory file id, for 5G it is 0. */
} sim_t;

/**
 * @brief A structure to represent NAS configuration.
 */
typedef struct {
	/**
     * Determines the NAS signalling priority included in NAS
	 * messages.
	 */
	vtq_usim_tlv_t signalling_priority;
	/**
     * Indicates whether the "NMO I, Network Mode of Operation I"
	 * indication is applied by the UE.
	 */
	vtq_usim_tlv_t nmo_i_behaviour;
	/**
	 * Indicates whether attach with IMSI is performed when moving
	 * to a non-equivalent PLMN.
	 */
	vtq_usim_tlv_t attach_with_imsi;
	/**
	 * Minimum value in minutes for the timer T controlling the
	 * periodic search for higher prioritized PLMNs.
	 */
	vtq_usim_tlv_t min_p_search_timer;
	/**
	 * Indicates whether the extended access barring is applicable
	 * for the UE.
	 */
	vtq_usim_tlv_t ext_access_barring;
	/**
     * Indicates whether the timer T3245 and the related
	 * functionality is used by the UE.
	 */
	vtq_usim_tlv_t timer_t3245_behaviour;
	/**
	 * used to determine whether the NAS signalling
	 * priority included in NAS messages can be overridden.
	 */
	vtq_usim_tlv_t o_nas_signalling_low_priority;
	/**
     * used to determine whether the Extended access
	 * barring included in NAS messages can be overridden.
	 */
	vtq_usim_tlv_t o_extended_access_barring;
	/**
     * Determine whether the UE can perform Fast First
	 * Higher Priority PLMN Search upon selecting a VPLMN
	 * as specified in 3GPP TS 23.122 [31].
	 */
	vtq_usim_tlv_t fast_first_higher_priority_plmn;
	/**
     * Determine whether the UE is allowed
	 * to disable the E-UTRA capability.
	 */
	vtq_usim_tlv_t eutra_disabling_capable;
	/** provides a configured UE retry wait time value. */
	vtq_usim_tlv_t sm_retry_waittime;
	/** indicates the UE's retry behaviour. */
	vtq_usim_tlv_t sm_retry_atrat_change;
	/**
     * Indicates the default DCN-ID which is provided
	 *by NAS to the lower layers at establishment of
	 * the NAS signalling connection.
	 */
	vtq_usim_tlv_t default_dcn_id;
	/**
     * For the UE in NB-S1 mode indicates whether
	 * the UE is allowed to use the RRC establishment
	 * cause mo-ExceptionData;
	 */
	vtq_usim_tlv_t e_data_reporting_allowed;
} vtq_usim_nasconfig_t;

/**
 * @brief A structure to represent SUCI.
 */
typedef struct {
	unsigned int length; /**< Length of suci data to be processed. */
	uint8_t *value;		/**< Value of the suci  data to be processed. */
} vtq_usim_suci_t;

/**
 * A structure to represent SUCI calc info.
 */
typedef struct {
	/**
	 * number of protection shcemes present in the list.
	 */
	uint8_t num_protection_schemes;
	/**
	 * value of protection scheme identifier.
	 */
	uint8_t protection_scheme_ids[16];
	/**
     * identifier of home network public key.
	 */
	uint8_t home_nw_public_key_id[16];
	/**
	 * length of home networki public key.
	 */
	uint8_t home_nw_pub_key_len[16];
	/**
	 * value of home network public key.
	 */
	uint8_t home_nw_public_key[16][128];
} vtq_usim_suci_calc_info_t;

/**
 * @brief A structure to represent IMSI data.
 */
typedef struct {
	uint8_t length; /**< Length of imsi data. */
	uint8_t value[USIM_IMSI_LENGTH]; /** Value of imsi data. */
} vtq_usim_imsi_t;

/**
 * @brief A structure to represent imeisv data.
 */
typedef struct {
	uint8_t length; /**< Length of imeisv data. */
	uint8_t *value; /**< value of imeisv data. */
} vtq_usim_imeisv_t;

/**
 * A structure to present plmn data.
 */
typedef struct {
	/**
	 * public limited mobile network code.
	 */
	uint8_t plmn_id[USIM_PLMNID_LENGTH];
} vqa_usim_plmn_t;

/**
 * A structure to represent hplmn data.
 */
typedef struct {
	/**
	 * home public limited mobile network code.
	 */
	vqa_usim_plmn_t plmn;
	/**
	 * hplmn access technology identifier.
	 */
	uint16_t act_id;
} vtq_usim_hplmnact_t;

/**
 * A structure to represent ehplmn data.
 */
typedef struct {
	/**
	 * equivalent public limited mobile network code.
	 */
	vqa_usim_plmn_t plmn;
} vtq_usim_ehplmn_t;

/**
 * A structure to represent user controlled plmn data.
 */
typedef struct {
	/**
	 * User controlled public limited network code.
	 */
	vqa_usim_plmn_t plmn;
	/**
	 * User controlled plmn access technology identifier.
	 */
	uint16_t act_id;
} vtq_usim_uplmnact_t;

/**
 * A structure to represent operator controlled plmn data.
 */
typedef struct {
	/**
	 * Operator controlled public limited network code.
	 */
	vqa_usim_plmn_t plmn;
	/**
	 * Operator controlled plmn access technology identifier.
	 */
	uint16_t act_id;
} vtq_usim_oplmnact_t;

/**
 * A structure to represent opertor plmn list.
 */
typedef struct {
	/**
     * plmn code.
	 */
    vqa_usim_plmn_t plmn;
	/**
	 * start tracking area code in a range.
	 */
    uint8_t start_tac[USIM_TAC_LENGTH];
	/**
	 * end tracking area code in a range.
	 */
    uint8_t end_tac[USIM_TAC_LENGTH];
	/**
	 * plmn network name record identifier.
	 */
    uint8_t pnn_id;
} vtq_usim_op_plmn_list_t;

/**
 * A structure to represent forbidden plmn data.
 */
typedef struct {
	/**
	 * forbidden plmn code.
	 */
    vqa_usim_plmn_t plmn;
} vtq_usim_fplmn_t;

/**
 * A structure to represent plmn network name.
 */
typedef struct {
	/**
     * Full name tag,length and value for network contents.
	 */
	vtq_usim_tlv_t full_name;
	/**
	 * Short name tag,length and value for network contents.
	 */
	vtq_usim_tlv_t short_name;
} vtq_usim_plmn_nn_t;

/**
 * A structure to represent UAC identities configuration.
 */
typedef struct {
	/**
	 * Unified Access Control identity.
	 */
	uint8_t uac_acc_id[USIM_UAC_LENGTH];
} vtq_usim_uac_t;

/**
 * A structure to represent access control class configuration.
 */
typedef struct {
	/**
	 * Access control class.
	 */
	uint8_t acc[USIM_ACC_LENGTH];
} vtq_usim_acc_t;

/**
 * A structure to represent tracking area idenity data.
 */
typedef struct {
	/**
	 * plmn code.
	 */
	vqa_usim_plmn_t plmn;
	/**
     * Tracking Area code.
	 */
	uint8_t tac[USIM_TAC_LENGTH];
} vqa_usim_tai_t;

/**
 * A structure to represent 5gs 3gpp location data.
 */
typedef struct {
	/**
	 * 5G-Globally unique tempory identifier.
	 */
	uint8_t guti[USIM_GUTI_LENGTH];
	/**
	 * Last visited tracking area identity in 5GS.
	 */
	vqa_usim_tai_t tai;
	/**
	 * 5GS update status.
	 */
	uint8_t status_update;
} vtq_usim_5gs_loc_info_t;

/**
 * A structure to represent nas security context data.
 */
typedef struct {
	/**
	 * 5GS nas security context tag.
	 */
	uint8_t nas_sec_ctx_tag;
	/**
	 * Length of 5GS nas security context.
	 */
	unsigned int nas_sec_ctx_len;
	/**
	 * ngksi data in tlv format.
	 */
	vtq_usim_tlv_t ngksi;
	/**
	 * kamf data in tlv format.
	 */
	vtq_usim_tlv_t kamf;
	/**
	 * Uplink nascount data in tlv format.
	 */
	vtq_usim_tlv_t ul_nascount;
	/**
	 * Downlink nascount data in tlv format.
	 */
	vtq_usim_tlv_t dl_nascount;
	/**
	 * NAS integrity and encryption identifier.
	 */
	vtq_usim_tlv_t int_enc_id;
	/**
	 * EPS NAS integrity and encryption identifier.
	 */
	vtq_usim_tlv_t eps_int_enc_id;
} vtq_usim_nas_security_ctx_t;

/**
 * A structure to represent administrative data.
 */
typedef struct {
	/**
	 * UE operation mode.
	 */
	uint8_t ue_op_mode;
	/**
	 * Additional information.
	 */
	uint8_t addnl_info[2];
	/**
	 * Length of mobile network code in IMSI.
	 */
	uint8_t mnc_len;
} vtq_usim_admin_data_t;

/**
 * A structure to represent USIM states.
 */
typedef enum {
	/**
	 * This state indicates USIM in connection state.
     */
	USIM_CONNECTED,
	/**
	 * This state indicates USIM in disconnected state.
	 */
	USIM_DISCONNECTED,
	/**
	 * This state indicates USIM in invalidated state.
	 */
	USIM_INVALIDATED,
	/**
	 * This state indicates when newly inserted SIM is different from
	 * previous.
	 */
	USIM_CHANGED
} vtq_usim_state_t;

/**
 * A structure to represent length and value format data.
 */
typedef struct {
	uint32_t  length; /**< Length of the data to be processed. */
	uint8_t  *value; /**< Value of the data to be processed. */
} vtq_usim_lv_t;

/**
 * A structure to represent 5G nas authentication keys data.
 */
typedef struct {
	/**
	 * kausf auth key tag, length and value.
	 */
	vtq_usim_tlv_t kausf;
	/**
	 * kseaf auth key tag, length and value.
	 */
	vtq_usim_tlv_t kseaf;
} vtq_usim_nas_auth_keys_t;

/**
 * A structure to represent access point control list data.
 */
typedef struct {
	/**
	 * Number of APNs/DNNs
	 */
	uint8_t count;
	/**
	 * APN/DNN TLVs.
	 */
	vtq_usim_tlv_t tlv_list[ACL_MAX_LIST];
} vtq_usim_acl_t;

#pragma pack(pop)

/** @brief Reads USIM service table from USIM
 *
 *  UST configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.8
 *
 *  @param[out] ust_data pointer which holds service table configuration.
 *  @param[out] length length of ust data in USIM.
 *  @return 0 if success or -1 if failure.
 */
int vtq_usim_read_ust(unsigned char *ust_data, int *length);

/** @brief Reads NAS configuration from USIM
 *
 *  NAS configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.94
 *
 *  @param[in] nas_length size of memory allocated for nas configuration.
 *  @param[out] nasconfig nasconfig Structure which holds nas configuration.
 *  @return 0 if success or -1 if failure.
 */
int vtq_usim_read_nasconfig(vtq_usim_nasconfig_t *nasconfig, int nas_length);

/** @brief Checks whether suci to be calculated by USIM or in ME
 *
 *  SUCI configuration is specified in
 *  TS 131 102 - V15.10.0 section 7.5.2.1
 *
 *  @return 0 if suci needs to be generated in ME or
 *			1 if suci needs to be generated in USIM.
 */
int vtq_usim_read_check_suci(void);

/** @brief Reads SUCI from USIM
 *
 *  SUCI configuration is specified in
 *  TS 131 102 - V15.10.0 section 7.5.2.1
 *
 *  @param[in] suci_len size of memory allocated for suci configuration.
 *  @param[out] suci suci structure which holds suci length and value.
 *  @return 0 if success or -1 if failure.
 */
int vtq_usim_read_suci(vtq_usim_suci_t *suci, int suci_len);

/** @brief Reads SUCI calculation info from USIM
 *
 *  SUCI CALC INFO configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.8
 *
 *  @param[in] suci_calc_info_len size of memory allocated for suci
 *					configuration.
 *  @param[out] suci_calc_info structure which holds suci length and value.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_suci_calc_info(vtq_usim_suci_calc_info_t *suci_calc_info,
	int suci_calc_info_len);

/** @brief Reads Routing indicator configuration from USIM
 *
 *  RID configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.11
 *
 *  @param[out] rid value.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_rid(uint16_t *rid);

/** @brief Reads IMSI configuration from USIM
 *
 *  IMSI configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.2
 *
 *  @param[out] imsi length and value.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_imsi(vtq_usim_imsi_t *imsi);

/** @brief Reads IMEISV configuration from flash
 *
 *
 *  @param[out] imeisv structure which holds its value and length.
 *  @return 0 if success or -1 if failure
 */
int vqa_read_usim_imeisv(vtq_usim_imeisv_t *imeisv);

/** @brief Reads hplmn count from USIM
 *
 *  It reads total file size of hplmnid file from USIM
 *  and calcualtes the number of plmnid's present in the file.
 *
 *  HPLMN configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.54
 *
 *  @param[out] count Number of hplmn id's present in USIM.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_hplmnact_list_count(int *count);

/** @brief Reads HPLMN configuration from USIM
 *
 *
 *  HPLMN configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.54
 *
 *  @param[in] count number of hplmn id's in USIM.
 *  @param[out] structure of hplmn which holds hplmn id and actid.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_hplmnact(vtq_usim_hplmnact_t *hplmn, int count);

/** @brief Reads ehplmn count from USIM
 *
 *  It reads total file size of ehplmn file from USIM
 *  and calcualtes the number of plmnid's present in the file.
 *
 *  EHPLMN configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.84
 *
 *  @param[out] count Number of ehplmn id's present in USIM.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_ehplmn_list_count(int *count);

/** @brief Reads EHPLMN configuration from USIM
 *
 *
 *  EHPLMN configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.84
 *
 *  @param[in] count number of ehplmn id's in USIM.
 *  @param[out] structure of ehplmn which holds ehplmnid.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_ehplmn(vtq_usim_ehplmn_t *ehplmn, int count);

/** @brief Reads user controlled plmn count from USIM
 *
 *  It reads total file size of user plmnact file from USIM
 *  and calcualtes the number of plmnid's present in the file.
 *
 *  USER controlled plmn configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.5
 *
 *  @param[out] count Number of user controlled plmn id's present in USIM.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_user_plmnact_list_count(int *count);

/** @brief Reads user controlled configuration from USIM
 *
 *
 *  USER controlled PLMN configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.5
 *
 *  @param[in] count number of user controlled plmn id's in USIM.
 *  @param[out] uplmn_act structure of user controlled which holds plmnid
 *		and actid.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_uplmnact(vtq_usim_uplmnact_t *uplmn_act, int count);

/** @brief Reads operator controlled plmn count from USIM
 *
 *  It reads total file size of operator controlled plmn file from USIM
 *  and calcualtes the number of plmnid's present in the file.
 *
 *  Operator controlled PLMN configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.53
 *
 *  @param[out] count Number of operator controlled plmn id's present in USIM.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_opc_plmnact_list_count(int *count);

/** @brief Reads operator controlled configuration from USIM
 *
 *
 *  Operator controlled PLMN configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.53
 *
 *  @param[in] count number of operator controlled plmn id's in USIM.
 *  @param[out] oplmn_act structure of operator  controlled plmn which
 *				holds plmnid and actid.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_opc_plmnact(vtq_usim_oplmnact_t *oplmn_act, int count);

/** @brief Reads operator plmn info from USIM
 *
 *
 *  operator plmn info is specified in
 *  TS 131 102 - V15.10.0 section 4.2.59
 *
 *  @param[out] op_plmn_list Structure which holds operator plmn info..
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_op_plmn(vtq_usim_op_plmn_list_t *op_plmn_list);

/** @brief Reads forbidden plmn count from USIM
 *
 *  It reads total file size of forbidden plmn file from USIM
 *  and calcualtes the number of plmnid's present in the file.
 *
 *  fplmn configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.16
 *
 *  @param[out] count forbidden plmn id's present in USIM.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_forbidden_plmn_list_count(int *count);

/** @brief Reads forbidden plmn configuration from USIM
 *
 *
 *  fplmn configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.16
 *
 *  @param[in] count number of fplmn plmn id's in USIM.
 *  @param[out] fplmn structure of fplmn which holds plmnid.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_forbidden_plmns(vtq_usim_fplmn_t *fplmn, int count);

/** @brief append forbidden plmn to USIM
 *
 *  It appends forbiddend plmn id's to the forbidden plmn list
 *	present in USIM.
 *
 *  fplmn configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.16
 *
 *  @param[in] count number of fplmn plmn id's to append.
 *  @param[out] fplmn structure of fplmn which holds plmnid.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_append_forbidden_plmns(vtq_usim_fplmn_t *fplmn, int count);

/** @brief delete forbidden plmn id from USIM
 *
 *  It deletes forbiddend plmn id's from the forbidden plmn list
 *  present in USIM.
 *
 *  fplmn configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.16
 *
 *  @param[in] count number of fplmn plmn id's to delete.
 *  @param[out] fplmn structure of fplmn which holds plmnid.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_delete_forbidden_plmns(vtq_usim_fplmn_t *fplmn, int count);

/** @brief Reads plmn network name from USIM
 *
 *
 *  plmn network name configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.58
 *
 *  @param[in] plmn_nn_len max size of memory.
 *  @param[out] plmn_nn structure of plmn network name.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_plmn_nn(vtq_usim_plmn_nn_t *plmn_nn, int plmn_nn_len);

/** @brief Reads UAC configuration from USIM
 *
 *
 *  UAC configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.7
 *
 *
 *  @param[out] uac structure of UAC which holds uac info.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_uac(vtq_usim_uac_t *uac);

/** @brief Reads ACC configuration from USIM
 *
 *
 *  ACC configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.2.15
 *
 *
 *  @param[out] uac structure of ACC which holds ACC info.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_acc(vtq_usim_acc_t *acc);

/** @brief Reads 5GS 3gpp location configuration from USIM
 *
 *
 *  UAC configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.2
 *
 *
 *  @param[out] loc_info structure of 5gs location info.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_5gs_loc_info(vtq_usim_5gs_loc_info_t *loc_info);

/** @brief Reads nas security configuration from USIM
 *
 *
 *  nas security configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.4
 *
 *
 *  @param[out] nas_sec_ctx structure of nas security context.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_nas_sec_ctx(vtq_usim_nas_security_ctx_t *nas_sec_ctx,
	int len);

/** @brief Reads 5G NAS authentication keys configuration from USIM
 *
 *
 *  UAC configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.6
 *
 *  @param[in] len size of memory allocated for nas auth keys configuration.
 *  @param[out] nas_auth_kes structure of nas authentication keys.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_read_nas_auth_keys(vtq_usim_nas_auth_keys_t *nas_auth_keys,
		int len);

/** @brief Reads Access point control list from  USIM
 *
 *	Access point control list  is specified in
 *      TS 131 102 - V15.10.0 section  4.2.48
 *
 *  @param[in] acl_max_len maximum memory size for acl list.
 *  @param[out] acl – structure which holds access point configuration.
 *  @return: 0 if success or -1 if failure
 *
 */
int vtq_usim_read_acl(vtq_usim_acl_t *acl, int acl_max_len);

/** @brief Writes 5G NAS authentication keys configuration to USIM
 *
 *
 *  5G NAS authentication configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.6
 *
 *
 *  @param[in] nas_auth_kes structure of nas authentication keys.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_write_nas_auth_keys(vtq_usim_nas_auth_keys_t *nas_auth_keys);

/** @brief Reads Administrative Data from  USIM
 *
 *	           Administrative data  is specified in
 *                      TS 131 102 - V15.5.0 section  4.2.18
 *
 *
 *  @param[out] admin_data – structure which holds admin data configuration.
 *  @return: 0 if success or -1 if failure
 *
 */
int vtq_usim_read_admin_data(vtq_usim_admin_data_t *admin_data);

/** @brief writes UAC configuration to USIM
 *
 *
 *  UAC configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.7
 *
 *
 *  @param[in] uac structure of UAC which holds uac info.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_write_uac(vtq_usim_uac_t *uac);

/** @brief Writes 5GS 3gpp location configuration to USIM
 *
 *
 *  5gs 3gpp location configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.2
 *
 *
 *  @param[out] loc_info structure of 5gs location info.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_write_5gs_loc_info(vtq_usim_5gs_loc_info_t *loc_info);

/** @brief writes nas security configuration to USIM
 *
 *
 *  nas security configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.4
 *
 *
 *  @param[in] nas_sec_ctx structure of nas security context.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_write_nas_sec_ctx(vtq_usim_nas_security_ctx_t *nas_sec_ctx,
		int nas_sec_len);

/** @brief Invalidates NAS security context data in USIM
 *
 *  @return 0 if success or -1 if failure
 */

int vtq_usim_invalidate_nas_sec_ctx(void);

/** @brief writes Routing indicator configuration to USIM
 *
 *  RID configuration is specified in
 *  TS 131 102 - V15.10.0 section 4.4.11.11
 *
 *  @param[in] rid value.
 *  @return 0 if success or -1 if failure
 */
int vtq_usim_write_rid(uint16_t rid);


/** @brief Performs mutual authentication of the USIM to the network.
 *
 *              checking whether authentication token AUTN can be accep-
 *              ted. If so, returns an authentication response RES and
 *              the ciphering and integrity keys.
 *              In case of synch failure, returns a re-synchronization
 *              token AUTS.
 *
 *	@param[in]	random_chal Random challenge number – 16bytes length
 *  @param[in]  autn_token  Authentication token - 16 bytes length
 *
 *  @param[out] auts  Re-synchronization token	- 14bytes length
 *  @param[out] res   Authentication response   -  16bytes length
 *  @param[out] ck    Ciphering key             - 16bytes length
 *  @param[out] ik    Integrity key             - 16bytes length
 *
 *  @return: 0 if authentication success or
 *			    1 if authentication failure or
 *			    2 if synchronization failure happens
 *
 *
 */
int vtq_usim_authenticate(vtq_usim_lv_t *random_chal,
	vtq_usim_lv_t *auth_token, vtq_usim_lv_t *auts, vtq_usim_lv_t *res,
	vtq_usim_lv_t *ck, vtq_usim_lv_t *ik);

/** @brief This function opens the device (/dev/cud) and return
 *		the file descriptor.
 *
 *
 *  @param[out] fd  file descriptor of /dev/cud device.
 *  @return 0 if success or -1 if failure
 *
 */
int vtq_usim_register(int sim_type);

/** @brief This function closes the device /dev/cud
 *
 *
 *  @param[out] fd is closed.
 *  @return 0 if success or -1 if failure
 *
 */
int vtq_usim_deregister(void);

/** @brief This function returns status of sim using IOCTL.
 *
 *   @return USIM_CONNECTED if usim is present or
 *		         USIM_DISCONNECTED if usim is not present or
 *		         USIM_INVALIDATED if usim is invalidated or
 *		         USIM_CHANGED if different usim is inserted.
 *
 *
 */
vtq_usim_state_t vtq_usim_check_status(void);

/*
 * @brief This function checks for current usim status.
 *
 *  @return USIM_CONNECTED if usim is present.
 *		         USIM_DISCONNECTED if usim is not present.
 *		         USIM_INVALIDATED if usim is invalidated.
 *		         USIM_CHANGED if different usim is inserted.
 *
 */
vtq_usim_state_t vtq_usim_get_cur_status(void);

/*
 * @brief Dump usim state data for debug purpose.
 *
 *  @return dump debug info on console
 *
 */
void vtq_usim_status_data_dump(void);

/** @brief This function invalidates usim.
 *
 *  @return 0 if usim is invalidated successfully or
 *			1 if usim is not invalidated.
 *
 */
int vtq_usim_invalidate(void);

/** @brief Performs mutual offchip authentication of the USIM to the network.
 *
 *              checking whether authentication token AUTN can be accep-
 *              ted. If so, returns an authentication response RES and
 *              the ciphering and integrity keys.
 *              In case of synch failure, returns a re-synchronization
 *              token AUTS.
 *
 *  @param[in]      key key value – 16bytes length
 *  @param[in]      opc opc value – 16bytes length
 *  @param[in]      random_chal Random challenge number – 16bytes length
 *  @param[in]  autn_token  Authentication token - 16 bytes length
 *
 *  @param[out] auts  Re-synchronization token  - 14bytes length
 *  @param[out] res   Authentication response   -  16bytes length
 *  @param[out] ck    Ciphering key             - 16bytes length
 *  @param[out] ik    Integrity key             - 16bytes length
 *
 *  @return: 0 if authentication success or
 *                          1 if authentication failure or
 *                          2 if synchronization failure happens
 *
 *
 */
int vtq_usim_offchip_authenticate(char *key, char *opc,
		vtq_usim_lv_t *random_chal, vtq_usim_lv_t *auth_token,
		vtq_usim_lv_t *auts, vtq_usim_lv_t *res, vtq_usim_lv_t *ck,
		vtq_usim_lv_t *ik);
#endif /* _USIM_LIBRARY_H */
