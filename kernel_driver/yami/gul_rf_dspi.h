/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2019-2022 NXP
 */

#ifndef __GUL_RF_DSPI_H__
#define __GUL_RF_DSPI_H__

#include <linux/types.h>

#define DSPI_BASE_ADDR_OFFSET	0x1170000
#define DSPI_REG_OFFSET		0x4000

#define DSPI_TX_FIFO_SIZE           4

#define DSPI_WRITE_SUCCESS          0
#define DSPI_READ_SUCCESS           0
#define DSPI_TXFIFO_FULL            -1
#define DSPI_RXFIFO_EMPTY           -2
#define DSPI_ERR_COUNT              -3
#define DSPI_ERROR_PARITY           -4
#define DSPI_ERR_RX_OVERFLOW        -5
#define DSPI_CLKSET_ERROR           -6
#define DSPI_READ_INVALID           -7

#define DSPI_VALID_BIT_MASK         0x00000100
#define DSPI_DATA_MASK              0x000000FF

/* Module configuration */
#define DSPI_MCR_MSTR           0x80000000
#define DSPI_MCR_CSCK           0x40000000
#define DSPI_MCR_DCONF(x)    (((x)&0x03)<<28)
#define DSPI_MCR_FRZ            0x08000000
#define DSPI_MCR_MTFE           0x04000000
#define DSPI_MCR_PCSSE          0x02000000
#define DSPI_MCR_ROOE           0x01000000
#define DSPI_MCR_PCSIS(x)    (1<<(16+(x)))
#define DSPI_MCR_PCSIS_MASK     (0xf << 16)
#define DSPI_MCR_CSIS7          0x00800000
#define DSPI_MCR_CSIS6          0x00400000
#define DSPI_MCR_CSIS5          0x00200000
#define DSPI_MCR_CSIS4          0x00100000
#define DSPI_MCR_CSIS3          0x00080000
#define DSPI_MCR_CSIS2          0x00040000
#define DSPI_MCR_CSIS1          0x00020000
#define DSPI_MCR_CSIS0          0x00010000
#define DSPI_MCR_DOZE           0x00008000
#define DSPI_MCR_MDIS           0x00004000
#define DSPI_MCR_DTXF           0x00002000
#define DSPI_MCR_DRXF           0x00001000
#define DSPI_MCR_CTXF           0x00000800
#define DSPI_MCR_CRXF           0x00000400
#define DSPI_MCR_SMPL_PT(x)    (((x)&0x03)<<8)
#define DSPI_MCR_FCPCS          0x00000001
#define DSPI_MCR_PES            0x00000001
#define DSPI_MCR_HALT           0x00000001
#define DSPI_MCR_TXRX_ENABLE    0xffffcfff
#define DSPI_MCR_XSPI           0x00000008

/* Transfer count */
#define DSPI_TCR_SPI_TCNT(x)    (((x)&0x0000FFFF)<<16)

/* Clock and transfer attributes */
#define DSPI_CTAR(x)            (0x0c+(x*4))
#define DSPI_CTAR_DBR            0x80000000
#define DSPI_CTAR_TRSZ(x)       (((x)&0x0F)<<27)
#define DSPI_CTAR_CPOL           0x04000000
#define DSPI_CTAR_CPHA           0x02000000
#define DSPI_CTAR_LSBFE          0x01000000
#define DSPI_CTAR_PCSSCK(x)    (((x)&0x03)<<22)
#define DSPI_CTAR_PCSSCK_7CLK    0x00A00000
#define DSPI_CTAR_PCSSCK_5CLK    0x00800000
#define DSPI_CTAR_PCSSCK_3CLK    0x00400000
#define DSPI_CTAR_PCSSCK_1CLK    0x00000000
#define DSPI_CTAR_PASC(x)    (((x)&0x03)<<20)
#define DSPI_CTAR_PASC_7CLK      0x00300000
#define DSPI_CTAR_PASC_5CLK      0x00200000
#define DSPI_CTAR_PASC_3CLK      0x00100000
#define DSPI_CTAR_PASC_1CLK      0x00000000
#define DSPI_CTAR_PDT(x)    (((x)&0x03)<<18)
#define DSPI_CTAR_PDT_7CLK       0x000A0000
#define DSPI_CTAR_PDT_5CLK       0x00080000
#define DSPI_CTAR_PDT_3CLK       0x00040000
#define DSPI_CTAR_PDT_1CLK       0x00000000
#define DSPI_CTAR_PBR(x)    (((x)&0x03)<<16)
#define DSPI_CTAR_PBR_7CLK       0x00030000
#define DSPI_CTAR_PBR_5CLK       0x00020000
#define DSPI_CTAR_PBR_3CLK       0x00010000
#define DSPI_CTAR_PBR_1CLK       0x00000000
#define DSPI_CTAR_CSSCK(x)    (((x)&0x0F)<<12)
#define DSPI_CTAR_ASC(x)      (((x)&0x0F)<<8)
#define DSPI_CTAR_DT(x)       (((x)&0x0F)<<4)
#define DSPI_CTAR_BR(x)       ((x)&0x0F)
#define DSPI_CTAR_X_TRSZ        (1 << 16)

/* Status */
#define DSPI_SR_TCF         0x80000000
#define DSPI_SR_TXRXS       0x40000000
#define DSPI_SR_EOQF        0x10000000
#define DSPI_SR_TFUF        0x08000000
#define DSPI_SR_TFFF        0x02000000
#define DSPI_SR_SPEF        0x00200000
#define DSPI_SR_CTCF        0x00800000
#define DSPI_SR_RFOF        0x00080000
#define DSPI_SR_RFDF        0x00020000
#define DSPI_SR_TFIWF       0x00040000
#define DSPI_SR_COUNT_RX    0x000000F0
#define DSPI_SR_TXCTR(x)    (((x)&0x0000F000)>>12)
#define DSPI_SR_TXPTR(x)    (((x)&0x00000F00)>>8)
#define DSPI_SR_RXCTR(x)    (((x)&0x000000F0)>>4)
#define DSPI_SR_RXPTR(x)    ((x)&0x0000000F)

/* DMA/interrupt request selct and enable */
#define DSPI_IRSR_DISABLE    0x00000000
#define DSPI_IRSR_TCFE       0x80000000
#define DSPI_IRSR_EOQFE      0x10000000
#define DSPI_IRSR_TFUFE      0x08000000
#define DSPI_IRSR_TFFFE      0x02000000
#define DSPI_IRSR_TFFFS      0x01000000
#define DSPI_IRSR_RFOFE      0x00080000
#define DSPI_IRSR_RFDFE      0x00020000
#define DSPI_IRSR_RFDFS      0x00010000

/* Transfer control - 32-bit access */
#define DSPI_TFR_PCS(x)     (((1<<x)&0x0000003f)<<16)
#define DSPI_TFR_CONT     0x80000000
#define DSPI_TFR_CTAS(x)    (((x)&0x07)<<28)
#define DSPI_TFR_EOQ      0x08000000
#define DSPI_TFR_CTCNT    0x04000000
#define DSPI_TFR_CS7      0x00800000
#define DSPI_TFR_CS6      0x00400000
#define DSPI_TFR_CS5      0x00200000
#define DSPI_TFR_CS4      0x00100000
#define DSPI_TFR_CS3      0x00080000
#define DSPI_TFR_CS2      0x00040000
#define DSPI_TFR_CS1      0x00020000
#define DSPI_TFR_CS0      0x00010000

/* Transfer Fifo */
#define DSPI_TFR_TXDATA(x)     ((x)&0x0000FFFF)

/* Bit definitions and macros for DRFR */
#define DSPI_RFR_RXDATA(x)     ((x)&0x000000FF)

/* Bit definitions and macros for DTFDR group */
#define DSPI_TFDR_TXDATA(x)    ((x)&0x0000FFFF)
#define DSPI_TFDR_TXCMD(x)     (((x)&0x0000FFFF)<<16)

/* Bit definitions and macros for DRFDR group */
#define DSPI_RFDR_RXDATA(x)    ((x)&0x0000FFFF)

#define DSPI_MOVANDI_READ      0x0000
#define DSPI_MOVANDI_WRITE     0x8000

enum dspi_interface {
	DSPI1 = 0,
	DSPI2,
	DSPI3,
	DSPI4,
	DSPI5,
	DSPI6,
};

struct dspi_regs {
	uint32_t mcr;			/* 0x00 */
	uint32_t resv0;			/* 0x04 */
	uint32_t tcr;			/* 0x08 */
	uint32_t ctar[8];		/* 0x0C - 0x28 */
	uint32_t sr;			/* 0x2C */
	uint32_t irsr;			/* 0x30 */
	uint32_t tfr;			/* 0x34 - PUSHR */
	uint32_t rfr;			/* 0x38 - POPR */
	uint32_t tfdr[16];		/* 0x3C */
	uint32_t rfdr[40];		/* 0x7C */
	uint32_t ctarx[2];		/* 0x11c */
};

int32_t rf_movandi_read_reg(enum dspi_interface dspi,
				u8 __iomem *ccsr,
				uint16_t addr,
				uint8_t *data);
int32_t rf_movandi_write_reg(enum dspi_interface dspi,
				u8 __iomem *ccsr,
				uint16_t addr,
				uint8_t data);
#endif /* __GUL_RF_DSPI_H__ */
