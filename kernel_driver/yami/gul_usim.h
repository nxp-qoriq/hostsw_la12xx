/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2021-2022 NXP
 */
#ifndef __GUL_USIM_H__
#define __GUL_USIM_H__

#include "gul_base.h"
#include <linux/poll.h>
#include <linux/timer.h>

#define SIM_ATR_LENGTH_MAX 32

/* Raw ATR SIM_IOCTL_GET_ATR */
typedef struct {
	uint32_t size;          /* length of ATR received */
	uint8_t t[SIM_ATR_LENGTH_MAX];  /* raw ATR string received */
} sim_atr_t;

/* Communication parameters for SIM_IOCTL_[GET|SET]_PARAM */
typedef struct {
	uint8_t convention;     /* direct = 0, indirect = 1 */
	uint8_t FI, DI;         /* frequency multiplier and devider indices */
	uint8_t PI1, II;        /* programming voltage and current indices */
	uint8_t N;              /* extra guard time */
	uint8_t T;              /* protocol type: T0 = 0, T1 = 1 */
	uint8_t PI2;            /* programming voltage 2 value */
	uint8_t WWT;            /* working wait time */
} sim_param_t;

/* ISO7816-3 protocols */
#define SIM_PROTOCOL_T0  0
#define SIM_PROTOCOL_T1  1

/* Transfer data for SIM_IOCTL_XFER */
typedef struct {
	uint8_t *xmt_buffer;    /* transmit buffer pointer */
	int32_t xmt_length;     /* transmit buffer length */
	uint8_t *rcv_buffer;    /* receive buffer pointer */
	int32_t rcv_length;     /* receive buffer length */
	int type;               /* transfer type: TPDU = 0, PTS = 1 */
	int timeout;            /* transfer timeout in milliseconds */
	uint8_t sw1;            /* status word 1 */
	uint8_t sw2;            /* status word 2 */
	uint8_t xmt_position;   /* xmt position */
} sim_xfer_t;

/* Transfer types for SIM_IOCTL_XFER */
#define SIM_XFER_TYPE_TPDU 0
#define SIM_XFER_TYPE_PTS  1

/* Interface power states */
#define SIM_POWER_OFF 0
#define SIM_POWER_ON  1

/* Return values for SIM_IOCTL_GET_PRESENSE */
#define SIM_PRESENT_REMOVED     0
#define SIM_PRESENT_DETECTED    1
#define SIM_PRESENT_OPERATIONAL 2

/* Return values for SIM_IOCTL_GET_ERROR */
#define SIM_OK                         0
#define SIM_E_ACCESS                   1
#define SIM_E_TPDUSHORT                2
#define SIM_E_PTSEMPTY                 3
#define SIM_E_INVALIDXFERTYPE          4
#define SIM_E_INVALIDXMTLENGTH         5
#define SIM_E_INVALIDRCVLENGTH         6
#define SIM_E_NACK                     7
#define SIM_E_TIMEOUT                  8
#define SIM_E_NOCARD                   9
#define SIM_E_PARAM_FI_INVALID         10
#define SIM_E_PARAM_DI_INVALID         11
#define SIM_E_PARAM_FBYD_WITHFRACTION  12
#define SIM_E_PARAM_FBYD_NOTDIVBY8OR12 13
#define SIM_E_PARAM_DIVISOR_RANGE      14
#define SIM_E_MALLOC                   15
#define SIM_E_IRQ                      16
#define SIM_E_POWERED_ON               17
#define SIM_E_POWERED_OFF              18

/* ioctl encodings */
#define SIM_IOCTL_BASE 0xc0
#define SIM_IOCTL_GET_PRESENSE   _IOR(SIM_IOCTL_BASE, 1, int)
#define SIM_IOCTL_GET_ATR        _IOR(SIM_IOCTL_BASE, 2, sim_atr_t)
#define SIM_IOCTL_GET_PARAM_ATR  _IOR(SIM_IOCTL_BASE, 3, sim_param_t)
#define SIM_IOCTL_GET_PARAM      _IOR(SIM_IOCTL_BASE, 4, sim_param_t)
#define SIM_IOCTL_SET_PARAM      _IOW(SIM_IOCTL_BASE, 5, sim_param_t)
#define SIM_IOCTL_XFER           _IOR(SIM_IOCTL_BASE, 6, sim_xfer_t)
#define SIM_IOCTL_POWER_ON       _IO(SIM_IOCTL_BASE, 7)
#define SIM_IOCTL_POWER_OFF      _IO(SIM_IOCTL_BASE, 8)
#define SIM_IOCTL_WARM_RESET     _IO(SIM_IOCTL_BASE, 9)
#define SIM_IOCTL_COLD_RESET     _IO(SIM_IOCTL_BASE, 10)
#define SIM_IOCTL_CARD_LOCK      _IO(SIM_IOCTL_BASE, 11)
#define SIM_IOCTL_CARD_EJECT     _IO(SIM_IOCTL_BASE, 12)
#define USIM_IOCTL_STATUS	_IO(SIM_IOCTL_BASE, 13)
#define USIM_IOCTL_INVALIDATE	_IO(SIM_IOCTL_BASE, 14)
#define USIM_IOCTL_GET_PHY_ADD	_IO(SIM_IOCTL_BASE, 15)

/* Transmit and receive buffer sizes */
#define SIM_XMT_BUFFER_SIZE 256
#define SIM_RCV_BUFFER_SIZE 256

/* ATR parser data (the parser state is stored in the main device structure) */
typedef struct {
	uint8_t T0;             /* ATR T0 */
	uint8_t TS;             /* ATR TS */
	/* ATR TA1, TB1, TC1, TD1, TB1, ... , TD4 */
	uint8_t TXI[16];
	uint8_t THB[15];        /* ATR historical bytes */
	uint8_t TCK;            /* ATR checksum */
	uint16_t ifc_valid;     /* valid interface characters */
	uint8_t ifc_current_valid;      /* calid ifcs in the current batch */
	uint8_t cnt;            /* number of current batch */
	uint8_t num_hb;         /* number of historical bytes */
} sim_atrparser_t;

#ifdef CONFIG_OF
struct clk {
	u32 rate;
	u8 prescaler;
};
#endif

struct sim_t;

/* SIM status for NAS */
typedef enum {
    USIM_STATE_CONNECTED,
    USIM_STATE_DISCONNECTED,
    USIM_STATE_INVALIDATED,
    USIM_STATE_CHANGED
} usim_state_t;

/* Main SIM driver structure */
struct sim_t {
	/* card inserted = 1, ATR received = 2, card removed = 0 */
	int present;
	/* current ATR or OPS state */
	int state;
	/* last status of sim card */
	int last_status;
	/* status of sim card */
	int status;
	/* current power state */
	int power;
	/* error code occured during transfer */
	int errval;
	struct clk clk; /* Clock id */
	uint8_t clk_flag;
	uint32_t clk_prescaler;
	struct resource *res;   /* IO map memory */
	void __iomem *ioaddr;   /* Mapped address */
	int ipb_irq;            /* sim ipb IRQ num */
	int dat_irq;            /* sim dat IRQ num */
	/* parser for incoming ATR stream */
	sim_atrparser_t atrparser;
	/* raw ATR stream received */
	sim_atr_t atr;
	/* communication parameters according to ATR */
	sim_param_t param_atr;
	/* communication parameters after pps/pts*/
	sim_param_t param_pts;
	/* current communication parameters */
	sim_param_t param;
	/* current TPDU or PTS transfer */
	sim_xfer_t xfer;
	/* transfer is on the way = 1, idle = 2 */
	int xfer_ongoing;
	/* remaining bytes to transmit for the current transfer */
	int xmt_remaining;
	/* transmit position */
	int xmt_pos;
	/* receive position / number of bytes received */
	int rcv_count;
	uint8_t rcv_buffer[SIM_RCV_BUFFER_SIZE];
	uint8_t xmt_buffer[SIM_XMT_BUFFER_SIZE];
	/* transfer completion notifier */
	struct completion xfer_done;
	/* async notifier for card and ATR detection */
	struct fasync_struct *fasync;
	/* Platform specific data */
	struct fsl_sim_platform_data *plat_data;
	/* Don't know what to write */
	uint8_t sim_pts_pps2_flag;
	uint8_t sim_pts_pps3_flag;
	uint8_t sim_pts_valid_flag;
	uint8_t ins_complimentary_flag;
	uint8_t sim_xmt_last_byte;
};

int gul_usim_probe(struct gul_dev *gul_dev, int virq_count,
		  struct virq_evt_map *virq_map);
int gul_usim_remove(struct gul_dev *gul_dev);

/* USIM Macros */
#define USIM_CCSR_OFFSET	0x22e0000

/* SCFG Register Map */
#define SCFG_CCSR_OFFSET	0x1E10000

/* SCFG Registers offset */
#define SCFG_CCSR_CONFIG_CTRL0_OFFSET                0x0
#define SCFG_CCSR_CONFIG_CTRL1_OFFSET                0x4
#define SCFG_CCSR_CONFIG_CTRL1_SIM_PER_CLK_RAT	     0x00800000
#define SCFG_CCSR_CONFIG_CTRL2_OFFSET                0x8
#define SCFG_CCSR_CONFIG_CTRL3_OFFSET                0xC
#define SCFG_CCSR_CONFIG_CTRL4_OFFSET                0x20
#define SCFG_CCSR_CONFIG_CTRL5_OFFSET                0x24
#define SCFG_CCSR_CONFIG_CTRL6_OFFSET                0x28
#define SCFG_CCSR_INTR0_OFFSET                       0x3000
#define SCFG_CCSR_INTR1_OFFSET                       0x3004
#define SCFG_CCSR_TBGEN_TSTAMP_TRIGGER_OFFSET        0x4000

/* Default communication parameters: FI=372, DI=1, PI1=5V, II=50mA, WWT=10 */
#define SIM_PARAM_DEFAULT { 0, 1, 1, 5, 1, 0, 0, 0, 10 }

/* ATR and OPS states */
#define SIM_STATE_REMOVED              0
#define SIM_STATE_OPERATIONAL_IDLE     1
#define SIM_STATE_OPERATIONAL_COMMAND  2
#define SIM_STATE_OPERATIONAL_RESPONSE 3
#define SIM_STATE_OPERATIONAL_STATUS1  4
#define SIM_STATE_OPERATIONAL_STATUS2  5
#define SIM_STATE_DETECTED_ATR_T0       7
#define SIM_STATE_DETECTED_ATR_TS       8
#define SIM_STATE_DETECTED_ATR_TXI      9
#define SIM_STATE_DETECTED_ATR_THB      10
#define SIM_STATE_DETECTED_ATR_TCK      11
#define SIM_STATE_OPERATIONAL_PPSS     6
#define SIM_STATE_OPERATIONAL_PPS0     12
#define SIM_STATE_OPERATIONAL_PPS1     13
#define SIM_STATE_OPERATIONAL_PPS2     14
#define SIM_STATE_OPERATIONAL_PPS3     15
#define SIM_STATE_OPERATIONAL_PCK      16

/* SIM port[0|1]_cntl register bits */
#define SIM_PORT_CNTL_SFPD   (1<<7)
#define SIM_PORT_CNTL_3VOLT  (1<<6)
#define SIM_PORT_CNTL_SCSP   (1<<5)
#define SIM_PORT_CNTL_SCEN   (1<<4)
#define SIM_PORT_CNTL_SRST   (1<<3)
#define SIM_PORT_CNTL_STEN   (1<<2)
#define SIM_PORT_CNTL_SVEN   (1<<1)
#define SIM_PORT_CNTL_SAPD   (1<<0)

/* SIM od_config register bits */
#define SIM_OD_CONFIG_OD_P1  (1<<1)
#define SIM_OD_CONFIG_OD_P0  (1<<0)

/* SIM enable register bits */
#define SIM_ENABLE_XMTEN     (1<<1)
#define SIM_ENABLE_RCVEN     (1<<0)

/* SIM int_mask register bits */
#define SIM_INT_MASK_RFEM    (1<<13)
#define SIM_INT_MASK_BGTM    (1<<12)
#define SIM_INT_MASK_BWTM    (1<<11)
#define SIM_INT_MASK_RTM     (1<<10)
#define SIM_INT_MASK_CWTM    (1<<9)
#define SIM_INT_MASK_GPCM    (1<<8)
#define SIM_INT_MASK_TDTFM   (1<<7)
#define SIM_INT_MASK_TFOM    (1<<6)
#define SIM_INT_MASK_XTM     (1<<5)
#define SIM_INT_MASK_TFEIM   (1<<4)
#define SIM_INT_MASK_ETCIM   (1<<3)
#define SIM_INT_MASK_OIM     (1<<2)
#define SIM_INT_MASK_TCIM    (1<<1)
#define SIM_INT_MASK_RIM     (1<<0)

/* SIM xmt_status register bits */
#define SIM_XMT_STATUS_GPCNT (1<<8)
#define SIM_XMT_STATUS_TDTF  (1<<7)
#define SIM_XMT_STATUS_TFO   (1<<6)
#define SIM_XMT_STATUS_TC    (1<<5)
#define SIM_XMT_STATUS_ETC   (1<<4)
#define SIM_XMT_STATUS_TFE   (1<<3)
#define SIM_XMT_STATUS_XTE   (1<<0)

/* SIM rcv_status register bits */
#define SIM_RCV_STATUS_BGT   (1<<11)
#define SIM_RCV_STATUS_BWT   (1<<10)
#define SIM_RCV_STATUS_RTE   (1<<9)
#define SIM_RCV_STATUS_CWT   (1<<8)
#define SIM_RCV_STATUS_CRCOK (1<<7)
#define SIM_RCV_STATUS_LRCOK (1<<6)
#define SIM_RCV_STATUS_RDRF  (1<<5)
#define SIM_RCV_STATUS_RFD   (1<<4)
#define SIM_RCV_STATUS_RFE   (1<<1)
#define SIM_RCV_STATUS_OEF   (1<<0)

/* SIM cntl register bits */
#define SIM_CNTL_BWTEN       (1<<15)
#define SIM_CNTL_XMT_CRC_LRC (1<<14)
#define SIM_CNTL_CRCEN       (1<<13)
#define SIM_CNTL_LRCEN       (1<<12)
#define SIM_CNTL_CWTEN       (1<<11)
#define SIM_CNTL_SAMPLE12    (1<<4)
#define SIM_CNTL_ONACK       (1<<3)
#define SIM_CNTL_ANACK       (1<<2)
#define SIM_CNTL_ICM         (1<<1)
#define SIM_CNTL_GPCNT_CLK_SEL(x)   ((x&0x03)<<9)
#define SIM_CNTL_GPCNT_CLK_SEL_MASK     (0x03<<9)
#define SIM_CNTL_BAUD_SEL(x)        ((x&0x07)<<6)
#define SIM_CNTL_BAUD_SEL_MASK          (0x07<<6)

/* SIM rcv_threshold register bits */
#define SIM_RCV_THRESHOLD_RTH(x)    ((x&0x0f)<<9)
#define SIM_RCV_THRESHOLD_RTH_MASK      (0x0f<<9)
#define SIM_RCV_THRESHOLD_RDT(x)   ((x&0x1ff)<<0)
#define SIM_RCV_THRESHOLD_RDT_MASK     (0x1ff<<0)

/* SIM xmt_threshold register bits */
#define SIM_XMT_THRESHOLD_XTH(x)    ((x&0x0f)<<4)
#define SIM_XMT_THRESHOLD_XTH_MASK      (0x0f<<4)
#define SIM_XMT_THRESHOLD_TDT(x)    ((x&0x0f)<<0)
#define SIM_XMT_THRESHOLD_TDT_MASK      (0x0f<<0)

/* SIM guard_cntl register bits */
#define SIM_GUARD_CNTL_RCVR11              (1<<8)
#define SIM_GIARD_CNTL_GETU(x)           (x&0xff)
#define SIM_GIARD_CNTL_GETU_MASK           (0xff)

/* SIM port[0|]_detect register bits */
#define SIM_PORT_DETECT_SPDS  (1<<3)
#define SIM_PORT_DETECT_SPDP  (1<<2)
#define SIM_PORT_DETECT_SDI   (1<<1)
#define SIM_PORT_DETECT_SDIM  (1<<0)

/* USIM Registers */
struct usim_regs_map {
	uint32_t usim_port1_cntl;	//0x0
	uint32_t usim_setup;		//0x04
	uint32_t usim_port1_detect;	//0x08
	uint32_t usim_xmt_buff;		//0x0c
	uint32_t usim_rcv_buff;		//0x10
	uint32_t usim_port0_cntl;	//0x14
	uint32_t usim_cntl;		//0x18
	uint32_t usim_clk_prescaler;	//0x1c
	uint32_t usim_rcv_threshold;	//0x20
	uint32_t usim_enable;		//0x24
	uint32_t usim_xmt_status;	//0x28
	uint32_t usim_rcv_status;	//0x2c
	uint32_t usim_int_mask;		//0x30
	uint32_t usim_reserved1;	//0x34
	uint32_t usim_reserved2;	//0x38
	uint32_t usim_port0_detect;	//0x3c
	uint32_t usim_data_format;	//0x40
	uint32_t usim_xmt_threshold;	//0x44
	uint32_t usim_guard_cntl;	//0x48
	uint32_t usim_od_config;	//0x4c
	uint32_t usim_reset_cntl;	//0x50
	uint32_t usim_char_wait;	//0x54
	uint32_t usim_gpcnt;		//0x58
	uint32_t usim_divisor;		//0x5c
	uint32_t usim_bwt;		//0x60
	uint32_t usim_bgt;		//0x64
	uint32_t usim_bwt_h;		//0x68
	uint32_t usim_xmt_fifo_stat;	//0x6c
	uint32_t usim_rcv_fifo_cnt;	//0x70
	uint32_t usim_rcv_fifo_wptr;	//0x74
	uint32_t usim_rcv_fifo_rptr;	//0x78

} __attribute__((packed));

/* END of REGS definitions */

/* Number of USIM Instances */
#define MAX_USIM_DEV		1

/* USIM device */
struct usim_dev {
	struct usim_regs_map *usim_regs;
	phys_addr_t usim_phy_add;
	struct gul_dev *gul_dev;
	struct sim_t *sim;
	wait_queue_head_t queue;
	struct timer_list poll_timer;
	struct mutex mutex;
};

/* Function Declaration */
void usim_init_clk(struct usim_dev *usim_dev);
void bit_set_polling(uint32_t *addr, uint32_t req_value, int i);
void delay_clk(uint32_t cycles, struct usim_dev *usim_dev);
void clear_gpcnt_counter(struct usim_dev *usim_dev);
void usim_card_detect(struct usim_dev *usim_dev);
void sim_init(struct usim_dev *usim_dev);
int usim_xfer(struct usim_dev *usim_dev, uint8_t *cmd_buff,
			uint32_t cmd_len, uint8_t *rsp_buff, uint32_t rcv_len);

#endif

