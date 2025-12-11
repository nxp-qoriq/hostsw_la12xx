/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2019-2022 NXP
 */
#include <linux/types.h>
#include <asm/io.h>
#include "gul_rf_dspi.h"

static void dspi_wait_fifo(struct dspi_regs *dspi_regs)
{
	uint32_t sr;

	while (1) {
		sr = ioread32(&(dspi_regs->sr));

		if (sr & DSPI_SR_TCF) {
			iowrite32(sr | DSPI_SR_TCF, &(dspi_regs->sr));
			break;
		}
	}
}

static int32_t dspi_push_read(struct dspi_regs *dspi_regs, uint16_t data)
{
	uint32_t cmd = DSPI_TFR_CS0;

	if (DSPI_SR_TXCTR(ioread32(&(dspi_regs->sr))) >= DSPI_TX_FIFO_SIZE) {
		pr_err("DSPI : TX FIFO is already full\n");
		return DSPI_TXFIFO_FULL;
	}

	iowrite32(cmd, &(dspi_regs->tfr));
	iowrite32(cmd | data, &(dspi_regs->tfr));
	dspi_wait_fifo(dspi_regs);

	return DSPI_WRITE_SUCCESS;
}

static int32_t dspi_push_write(struct dspi_regs *dspi_regs,
				uint16_t addr, uint8_t data)
{
	uint32_t cmd = DSPI_TFR_CS0;
	uint32_t mcr;

	if (DSPI_SR_TXCTR(ioread32(&(dspi_regs->sr))) >= DSPI_TX_FIFO_SIZE) {
		pr_err("DSPI : TX FIFO is already full\n");
		return DSPI_TXFIFO_FULL;
	}

	iowrite32(cmd | (((uint32_t)data)  << 8), &(dspi_regs->tfr));
	iowrite32(cmd | addr | DSPI_TFR_EOQ, &(dspi_regs->tfr));
	dspi_wait_fifo(dspi_regs);

	mcr = ioread32(&(dspi_regs->mcr));
	iowrite32(mcr | DSPI_MCR_CRXF, &(dspi_regs->mcr));

	return DSPI_WRITE_SUCCESS;
}

static int32_t dspi_pop(struct dspi_regs *dspi_regs, uint32_t *data)
{
	if ((ioread32(&(dspi_regs->sr)) & DSPI_SR_COUNT_RX) == 0) {
		pr_err("DSPI Read Error : Rx FIFO is empty\n");
		return DSPI_RXFIFO_EMPTY;
	}

	/*Shifting the data received to remove trailing zeros*/
	*data = (ioread32(&(dspi_regs->rfr)) >> 5);

	return DSPI_READ_SUCCESS;
}

int32_t rf_movandi_read_reg(enum dspi_interface dspi,
			    u8 __iomem *ccsr,
			    uint16_t addr,
			    uint8_t *data)
{
	int32_t ret, retry = 1;
	uint32_t sr, local_data;
	struct dspi_regs *dspi_regs =
		(struct dspi_regs *)(ccsr + DSPI_BASE_ADDR_OFFSET +
				     (dspi * DSPI_REG_OFFSET));

try_again:
	ret = dspi_push_read(dspi_regs, addr | DSPI_MOVANDI_READ);
	if (ret < 0)
		return ret;

	sr = ioread32(&(dspi_regs->sr));
	if (sr & (DSPI_SR_SPEF | DSPI_SR_RFOF)) {
		pr_err("DSPI Error: parity or overflow\n");
		iowrite32(sr, &(dspi_regs->sr));
		return -1;
	}

	ret = dspi_pop(dspi_regs, &local_data);
	if (ret < 0)
		return ret;

	if (local_data & DSPI_VALID_BIT_MASK) {
		*data = (uint8_t)(local_data);
		return DSPI_READ_SUCCESS;
	}

	if ((addr >= 0x400 || addr == 0x8) && retry--)
		goto try_again;

	return DSPI_READ_INVALID;
}

int32_t rf_movandi_write_reg(enum dspi_interface dspi,
			    u8 __iomem *ccsr,
			    uint16_t addr,
			    uint8_t data)
{
	struct dspi_regs *dspi_regs =
		(struct dspi_regs *)(ccsr + DSPI_BASE_ADDR_OFFSET +
				     (dspi * DSPI_REG_OFFSET));

	return dspi_push_write(dspi_regs, addr | DSPI_MOVANDI_WRITE, data);
}
