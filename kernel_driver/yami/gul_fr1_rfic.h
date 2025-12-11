/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2021-2022 NXP
 */

#ifndef __GUL_FR1_RFIC_H__
#define __GUL_FR1_RFIC_H__

#include "gul_base.h"
#include <rfdev_ioctl.h>

#define YUCCA_RFIC_1 1
#define YUCCA_RFIC_2 2

#define llcp_write	writew
#define llcp_read	readw

#define LLCP_REG_R	0
#define LLCP_REG_W	1

#define RFIC_INTF_PMEM	0x0015
#define RFIC_START_FW	0x0011

#define YUC_RFIC_FW_ADDR_REG	0x6000
#define YUC_RFIC_FW_CTRL_REG	0X6001
#define YUC_RFIC_FW_WRITE_REG	0X6002
#define YUC_RFIC_TS_RETRY	16

#define GETnBITS(val, pos, nbits) (((val) >> (pos)) & ((1 << nbits) - 1))

#define YUC_RFIC_WORD_1 1
#define YUC_RFIC_WORD_2 2
#define YUC_RFIC_WORD_3 3
#define YUC_RFIC_WORD_4 4

#define YUC_RFIC_RESETN_TIMEWAIT	3 /* 3ms */
#define YUC_RFIC_LVDS_OTP_TIMEWAIT	100 /*100micro sec = 0.1ms */
#define YUC_RFIC_START_TIMEWAIT	    200 /* 200 micro sec */

#define YUC_RFIC_FW_NAME_SIZE  FIRMWARE_NAME_SIZE
#define YUC_RFIC_FW_SIZE       (500 * 1024)
#define YUC_RFIC_PROG_FW_FILE  "YuccaES1_prog.mem"
#define YUC_RFIC_DATA_FW_FILE  "YuccaES1_init.mem"

#define YUC_RFIC_PMEM_CTRL_SRC_SELECT	0x0001

#define YUC_RFIC_CMD_ADDR		0x0000
#define YUC_RFIC_CMD_RESP_ADDR	0x000a

#define LLCP_IOCR_VAL           0x80092609
#define LLCP_AN_CTRL_VAL        0x08080808
#define LLCP_IOCR_ADDR        (0x1FF8000 + 0xB0)
#define LLCP_ANALOG_CTRL_ADDR (0x1FF8000 + 0xAC)

#define LLCPCSR1_OFFSET     0x1100000
#define LLCPGA1_OFFSET      0x1100030
#define LLCPCSR2_OFFSET     0x1104000
#define LLCPGA2_OFFSET      0x1104030
#define LLCPGA_VALUE        0xaff0

#define LLCP1_RFIC_OFFSET	0x1200000
#define LLCP2_RFIC_OFFSET   0x1210000

#define LLCP1_RFIC_RO_REG_CHIPID_OFFSET 0x1208200
#define LLCP2_RFIC_RO_REG_CHIPID_OFFSET 0x1218200
#define YUC_RFIC_CHIP_ID_RO_REG_VALUE   0x01a0

#define GPIO2_OFFSET 0x1134000

#define YUC_RFIC_CAL_RESISTOR_REFERENCE  0xff

typedef struct rfic_priv {
	u8 __iomem *llcp1_addr;
	u8 __iomem *llcp2_addr;
	u8 __iomem *llcp_addr;
	int yucca1_reset_gpio;
	int yucca2_reset_gpio;
	int yucca1_trx_gpio;
	int yucca2_trx_gpio;
	int yucca_trx_gpio;
} rfic_priv_t;

enum yuc_reset_type {
	YUC_PO_RESET = 1,
	YUC_COLD_RESET,
	YUC_WARM_RESET,
};

typedef struct yuc_rfic_cmd {
	u16 len : 4;
	u16 id : 10;
	u16 sd : 1;
	u16 rrq : 1;
} __attribute__((packed))yuc_cmd_t;

typedef struct yuc_rfic_cmd_resp {
	u16 len : 4;
	u16 id : 10;
	u16 tf : 1;
	u16 ew : 1;
} __attribute__((packed))yuc_cmd_resp_t;

int yuc_rfic_start_up(struct gul_dev *gul_dev, enum yuc_reset_type rst_type);
int gul_yucca_rfic_probe(struct gul_dev *gul_dev, int virq_count,
	struct virq_evt_map *virq_map);
int gul_yucca_rfic_remove(struct gul_dev *gul_dev);
#endif /* __GUL_FR1_RFIC_H__ */

