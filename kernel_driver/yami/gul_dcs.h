/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2026 NXP
 */

#include "gul_base.h"

/* Macro to enable the Temp Compensation */
#define HSDCS_THERMAL_COMPENSATION	1

/* Macro to enable the Temp Compensation Read function */
#define HSDCS_READ_THERMAL_COMPENSATION 0

/* Macro to enable the Temp Monitor Support */
#define HSDCS_MONITOR_CHIP_TEMPERATURE	1

/* This counter is used to control the init time calibration attempts  */
#define HSDCS_INITIAL_CAL_RETRIES	5

/* Enables the Temperature Monitor Kernel Thread */
#define HSDCS_TMON_THREAD_ENABLED 1

/* Enable this to print the SCFG register values */
#define DEBUG_SHOW_SCFG_REG_CONFIG 0

/* To get all the Hs DCS Register dump */
#define DEBUG_HSDCS_DUMP_REG 0

/* Enables HSADC calibration debug events */
#define HSDCS_CAL_DEBUG      0

#ifdef HSDCS_CAL_DEBUG
#define HSDCS_CAL_TS_LEN 	100 /* event name string len in bytes */
#define HSDCS_CAL_MAX_EVENTS 	100

#define HSDCS_PING_FW_BIT      0x80
#define HSDCS_PING_ASSERT_DELAY 300 /* ms */
#define HSDCS_PING_DEASSERT_DELAY 50 /* ms */
#endif /* HSDCS_CAL_DEBUG */



/* for FW version < 2.9 */
#define DEM_ENABLE_CONFIG 0

#if defined (LA1224)
/* This is to disable LSDCS on the LA12xx stage2 reworked HSDCS */
#define BNRG_DCS 1
#else
#define BNRG_DCS 0
#endif


#define HSDCS_DO_INC_RECAL  1
#define HSDCS_DO_FULL_RECAL 2

#define DCS_HS_FIRMWARE_NAME	"adc_cal.bin"
#define DCS_HS_FIRMWARE_NAME_B0	"adc_cal_b0.bin"
#define DCS_HS_FW_NAME_MAX	0x50
#define DCS_HS_MAGIC	0xcafebabe

/* Min Required FW version 2.18.1 */
#define HSDCS_MIN_REQUIRED_MAJOR 2
#define HSDCS_MIN_REQUIRED_MINOR 18
#define HSDCS_MIN_REQURED_SUBVER 1

#define HSDCS_SET_BIT_POS(x, bitpos) (x |= (1 << bitpos))
#define HSDCS_CLEAR_BIT_POS(x, bitpos) (x &= ~(1 << bitpos))
#define HSDCS_CHECK_BIT_POS(x, bitpos) (x & (1 << bitpos))

#define HSADC_CAL_BIT_POS_0   0
#define HSADC_CAL_BIT_POS19  19
#define HSADC_CAL_BIT_POS20  20

#define HSDCS_BYTE_MASK  0xff
#define HSDCS_BIT0_MASK  0x01
#define HSDCS_BIT1_MASK  0x10
#define HSDCS_BIT1F_MASK  0x1f
#define HSDCS_BIT0F_MASK  0x0f
#define HSDCS_SHIFT_1BYTE 8
#define HSDCS_SHIFT_2BYTE 16

/* DCS Clock Gen Related Macros */
#define DCS_CLK_GEN_OFFSET	0x1380000
#define DCS_PLL_STATUS_CHK	((0x01 << 23) | (1 << 30))
#define DCS_PLL_WAIT_TIMEOUT	0x100
#define DCS_PLL_STP_REQ	 (0x01 << 26)
#define DCS_PLL_CLK_SET_DIV8  ((0x01 << 14) | (1 << 15))

/* Register Offsets */
#define DCS_RSTCTL_OFFSET	0x00
#define DCS_GR0_OFFSET		0x04
#define DCS_TCR0_OFFSET		0x08
#define DCS_LCAPCR0_OFFSET	0x20
#define DCS_LCAPCR2_OFFSET	0x28
#define DCS_LCAPCR3_OFFSET	0x2C
#define DCS_BCB0_OFFSET		0xF0
#define DCS_SPARE_CR0_OFFSET	0x100
#define DCS_SPARE_CR1_OFFSET	0x104
#define DCS_SPARE_CR2_OFFSET	0x108
#define DCS_SPARE_SR0_OFFSET	0x10C
#define DCS_SPARE_SR1_OFFSET	0x110
#define DCS_PLLRSTCTL_OFFSET	0x400
#define DCS_PLLCR0_OFFSET	0x404
#define DCS_PLLCR1_OFFSET	0x408
#define DCS_PLLCR2_OFFSET	0x40C
#define DCS_PLLCR5_OFFSET	0x418
#define DCS_PLLCR6_OFFSET	0x41C
#define DCS_PLLCR8_OFFSET	0x424
#define DCS_PLLCR9_OFFSET	0x428
#define DCS_PLLCR10_OFFSET	0x4d0
#define DCS_PLLCR11_OFFSET	0x4d4
#define DCS_PLLCB0_OFFSET	0x4F0
#define DCS_PLLCB1_OFFSET	0x4F4

/* PLL Reset and Reconfigure val */
#define DCS_PLL_RECONF_SEQ_VAL_400  0x44800030
#define DCS_PLL_RECONF_SEQ_VAL_408  0x8b300008
#define DCS_PLL_RECONF_SEQ_VAL_41C  0x01300000
#define DCS_PLL_RECONF_SEQ_VAL_4D0  0x38040000
#define DCS_PLL_RECONF_SEQ_VAL_4D4  0x00130000
#define DCS_PLL_RECONF_SEQ_VAL2_400 0x80000000

/* SCFG Register Map */
#define SCFG_REG_OFFSET		0x1E10000
#define DISABLE_DCS_HS_AXIQ_CLK	(0x01 << 2)
#define DISABLE_DCS_HS_PCLK	(0x01 << 14)
#define ENABLE_ALL_CONFIG_CTRL5	0x1fffff
#define ENABLE_DEFAULT_CONFIG_CTRL0 0x8107ff80

/* Registers offset */
#define SCFG_CONFIG_CTRL0_OFFSET		0x0
#define SCFG_CONFIG_CTRL1_OFFSET		0x4
#define SCFG_CONFIG_CTRL2_OFFSET		0x8
#define SCFG_CONFIG_CTRL3_OFFSET		0xC
#define SCFG_RSTOUT_PULSE_WIDTH_XCVR1_OFFSET	0x10
#define SCFG_RSTOUT_PULSE_WIDTH_XCVR2_OFFSET	0x14
#define SCFG_RSTOUT_PULSE_WIDTH_XCVR3_OFFSET	0x18
#define SCFG_RSTOUT_PULSE_WIDTH_XCVR4_OFFSET	0x1C
#define SCFG_CONFIG_CTRL4_OFFSET		0x20
#define SCFG_CONFIG_CTRL5_OFFSET		0x24
#define SCFG_CONFIG_CTRL6_OFFSET		0x28
#define SCFG_INTR0_OFFSET			0x3000
#define SCFG_INTR1_OFFSET			0x3004
#define SCFG_TBGEN_TSTAMP_TRIGGER_OFFSET	0x4000

#define CAL_REF_REG_VAL_RB  0xfc4
#define CAL_REF_REG_VAL_RA  0xfc0
#define CLK_CTRL_REG_VAL 0xeb

/* DCS HS Macros */
#define DCS_HS_CCSR_OFFSET	0x1290000

#define DCSCORE_PROG_START_VAL	0x00000000

#define DCSCORE_RESET		(0x01 << 0)
#define DCSCORE_FREEZE		(0x01 << 1)
#define DCSCORE_PROGRAM_MODE	(0x01 << 2)
#define DCSCORE_PROGRAM_RESET	(0x01 << 3)

#define HSDCS_FW_VERIFICATION	0
#define ENABLE_PCLK_AXIQ_CLK	1
#define DCS_PLL_CHK_ENABLE	1

#define HSDCS_WRITE_MIN_TIMEWAIT  10

#define HSDCS_TIMEOUT	100
#define HSDCS_RETRIES	20
#define HSDCS_TIMEOUT_MS 10
#define HSDCS_TIMEOUT_INITIAL_CAL (300) /* millisecs; timeout suggested by FW */
#define HSDCS_TIMEOUT_FULL_RECAL  (160) /* millisecs; timeout suggested by FW */

#define DAC_ENABLE_NONE     0x0
#define DAC_ENABLE_1        0x3
#define DAC_ENABLE_2	    0xC
#define DAC_ENABLE_BOTH	    (DAC_ENABLE_1 | DAC_ENABLE_2)
#define ADC_ENABLE_1         0x3
#define ADC_ENABLE_2         0xC
#define ADC_ENABLE_BOTH    (ADC_ENABLE_1 | ADC_ENABLE_2)
#define ADC_16G_DIS_MASK        0x0f
#define ADC_16G_EN_MASK    0x1f

#define HS_DCS_SRAM_CLK_EN (0x01 << 17)

#define HS_DCS_DAC_CURR_10MA  (1 << 29)
#define HS_DCS_DAC_CURR_12MA  (1 << 30)

#define HSADC0_LOW_PWR_CFG_LDO	(1)
#define HSADC1_LOW_PWR_CFG_LDO	(1 << 7)

#define HSADC0_LOW_PWR_TBGEN2_CTRL	0x011244F0
#define HSADC1_LOW_PWR_TBGEN2_CTRL	0x01124540
#define HSADC_LOW_PWR_TBGEN2_CFG_BIT_POS  8

#define DISABLE_LOWPWR 1
#define ENABLE_LOWPWR  2

#define TBGEN_STORE    1
#define TBGEN_RESTORE  2

/* DEM for all DACs */
#define HS_DCS_DAC_DEM_EN  (1 << 31)

#define HSADC_FAST_RECAL_CLEAR_MASK 0xFFFF07FF

#define HSADC_RECALIBRATION_SELECT 12
#define HSADC_START_RECALIBRATION  (1 << 11)

#define HSADC_TEMP_COMP_EN  (1 << 16) // Enable temperature compensation
#define HSADC_TEMP_COMP_READ_MASK  0xFE00FFFF
#define HSADC_TEMP_COMP_BIT_POS  16
#define HSADC_TEMP_COMP_ACTED_READ_MASK   0x03FE0000
#define HSADC_TEMP_COMP_ACTED_READ_BITPOS 17
#define HSADC_TEMP_COMP_ORG_CALC_MASK        0x0001FF00
#define HSADC_TEMP_COMP_ORG_CALC_READ_BITPOS 8

#define HSADC_TEMP_CONV_KELVIN 273

#define THRESHOLD_INCREMENTAL_RECAL 30  // in degree C
#define THRESHOLD_FULL_RECAL 70  // in degree C
#define DCS_MON_TIMER 10 // in secs


#define HSDCS_TMON_TEMP_POLL_TIMER_T1  DCS_MON_TIMER // in sec
#define HSDCS_ADC0_IQ_CHAN_MASK 0x03
#define HSDCS_ADC1_IQ_CHAN_MASK 0x0C

#define HSDCS_TEMP_THRESHOLD_LOW -30 // in degree C
#define HSDCS_TEMP_THRESHOLD_HIGH 105 // in degree C
#define HSDCS_TEMP_RETRY_WAIT_TIME  5 // secs

/* T3 timer wait for wait for .5 sec. (500*1msec) */
#define HSDCS_TMON_RETRY_COUNT 500
#define HSDCS_TMON_RETRY_WAIT  1

/* HSDCS MAILBOX_REG0_RESP bit fields */
#define HSDCS_REG0_RESP_ACK 1  /* Host protocol Ack bit */
#define HSDCS_REG0_RESP_INIT_DONE (1<<4)  /* Set to 1 when cal completes */

/* For DCS_LS */
#define DCS_LS_EN		1

#define LS_LLCP_PLAT_CLKDIV2   (1 << 1)

/*CONFIG_CTRL_6*/
#define LS0_AXIQ_TX_CLK_DIS    (1 << 8)
#define LS0_AXIQ_RX_CLK_DIS    (1 << 9)
#define LS1_AXIQ_TX_CLK_DIS    (1 << 10)
#define LS1_AXIQ_RX_CLK_DIS    (1 << 11)
#define LS0_DCS_PCLK_DIS       (1 << 12)
#define LS1_DCS_PCLK_DIS       (1 << 13)
/* CTRL 6 - Geul B0 */
#define HS_DCS_PCLK_DIS		(1 << 14)
#define HS_DCS_AXIQ_CLK_DIS	(1 << 6)
#define HS_DCS_CLK_DIS		(1 << 2)
#define HS_DCS_OUT_OF_RESET	(1 << 1)
/* CTRL 5 */

struct hsdcs_regs_map {
	/* High Speed AMB General Control Register */
	uint32_t ip_ver_info; /* 0x0 */
	uint32_t wr_rw_test_reg;
	uint32_t clk_ctrl;
	uint32_t adc_dac_reg_ctrl;
	uint32_t adc_dac_reg_gr_status; /* 0x10 */
	uint32_t adc_enable_ctrl;
	uint32_t adc_cal_comp_stat;
	uint32_t dac_enable_ctrl; /* 0x1C */
	uint32_t dac_ready_stat; /* 0x20 */
	uint32_t dcs_ctrl;
	uint32_t dcs_prog_wrt_data;
	uint32_t dcs_prog_rd_data;
	uint32_t dcs_prog_strt_addr; /* 0x30 */
	uint32_t low_pwr_cfg;
	uint32_t dcs_mailbox_reg0;
	uint32_t dcs_mailbox_reg1;
	uint32_t dcs_mailbox_reg2; /* 0x40 */
	uint32_t dcs_mailbox_reg3;
	uint32_t dcs_mailbox_reg0_resp;
	uint32_t dcs_mailbox_reg1_resp;
	uint32_t dcs_mailbox_reg2_resp; /* 0x50 */
	uint32_t fw_ver_info;
	uint32_t fifo_ctrl;
	uint32_t db_dcs_prog_waddr_ptr; /* 0x5c */
	uint32_t db_dcs_prog_raddr_ptr; /* 0x60 */
	uint32_t db_dcs_status; /* 0x64 */
	uint32_t db_dcs_current_pc; /* 0x68 */
	uint32_t resrv1[1]; /* 0x6c */
	uint32_t fail_safe_reg1; /* 0x70 */
	uint32_t fail_safe_reg2;
	uint32_t resrv2[2]; /* 0x78-0x7c */
	/* DAC REG */
	uint32_t dac1_rst_ctrl; /* 0x80 */
	uint32_t dac1_dem_ctrl_i;
	uint32_t dac1_dem_ctrl_q;
	uint32_t dac1_lfsr_crtl_i;
	uint32_t dac1_lfsr_crtl_q; /* 0x90 */
	uint32_t dac1_test_tri;
	uint32_t dac1_intrlve_patrn_sel;
	uint32_t dac1_resrv3; /* 0x9c */
	uint32_t dac1_atest_ctrl; /* 0xa0 */
	uint32_t dac1_pwr;   /* 0xa4 */
	uint32_t dac1_bias;  /* 0xa8 */
	uint32_t dac_ctrl_i; /* 0xac */
	uint32_t dac_ctrl_q; /* 0xb0 */
	uint32_t dac_mux;    /* 0xb4 */
	uint32_t dac_data_i; /* 0xb8 */
	uint32_t dac_data_q; /* 0xbc */
	uint32_t dac_ldo1;   /* 0xc0 */
	uint32_t dac_ldo2;   /* 0xc4 */
	uint32_t dac_ldo3;   /* 0xc8 */
	uint32_t resrv5[4]; /* 0xcc-0xd8 */
	uint32_t dac1_gp1_ctrl;
	uint32_t dac1_gp1_status; /* 0xe0 */
	uint32_t dac1_gp2_ctrl;
	uint32_t dac1_gp2_status;
	uint32_t dac1_resrv6[5]; /* 0xec-0xfc */
	uint32_t dac2_rst_ctrl; /* 0x100 */


} __packed;


struct hsdcs_status_info {
	uint32_t fullrecal_cnt;
	uint32_t fastrecal_cnt;
	uint32_t temp_comp_cnt;
} __packed;

struct dcs_dev {
	char name[DCS_HS_FW_NAME_MAX];
	struct hsdcs_regs_map *hsdcs_regs;
	struct gul_dev *gul_dev;
	struct hsdcs_status_info dcs_info;
	u8 hsdcs_inc_recal_enabled;
	u8 hsdcs_full_recal_enabled;
	int ihsdcs_lastime_inc_cal_temp;
	int ihsdcs_lastime_full_cal_temp;
	int ihsdcs_current_temp;
};

int hsdcs_check_clk_983(struct gul_dev *gul_dev);
int hsdcs_adc_fast_recal(struct gul_dev *gul_dev, u32 val);
int hsdcs_adc_full_recal(struct gul_dev *gul_dev);
void hsdcs_adc_set_tmon_tri(void);
void hsdcs_adc_set_tmon_tri_full(void);
ssize_t hsdcs_show_stats(struct gul_dev *gul_dev, char *buf);
void hsdcs_adc_en_dis_fullcal(struct dcs_dev *dcsdev, u32 val);
void hsdcs_adc_en_dis_inc_cal(struct dcs_dev *dcsdev, u32 val);
void hsdcs_read_temp_compensation(struct gul_dev *gul_dev, int *last_act, int *org_act);
void hsdcs_write_temp_compensation(struct gul_dev *gul_dev);
void hsdcs_adc_low_pow_conf(struct gul_dev *gul_dev, u8 low_enb_dis, u8 tbgen_rw);
