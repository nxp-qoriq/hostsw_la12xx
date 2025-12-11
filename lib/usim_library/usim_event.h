/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2021-2022 NXP
 *
 */
#ifndef _USIM_EVENT_H
#define _USIM_EVENT_H

#include <endian.h>

#define MAP_SIZE		4096UL
#define MAP_MASK		(MAP_SIZE - 1)
#define USIM_CCSR_OFFSET        0x22e0000
#define CCSR_START		0x5048000000
#define USIM_CCSR_START		0x504A2E0000
#define CCSR_SIZE		0x8000000

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


/* SIM status for NAS */
typedef enum {
    USIM_STATE_CONNECTED,
    USIM_STATE_DISCONNECTED,
    USIM_STATE_INVALIDATED,
    USIM_STATE_CHANGED
} usim_state_t;


typedef struct {
	void *usim_ccsr_map;
	uint64_t usim_ccsr_phy;
	struct usim_regs_map *usim_regs;
	uint8_t state;
	uint8_t state_cur;
	uint8_t iccid_old[22];
	uint8_t iccid_len_old;
	uint8_t iccid_new[22];
	uint8_t iccid_len_new;
} event_handle_t;


typedef enum modem_endianness {
    MOD_BE,
    MOD_LE
} mod_endian_t;


#define iowrite32(v, p) ((*((uint32_t *)(p)) = htobe32((v))))
#define iowrite32be(v, p) ((*((uint32_t *)(p)) = htobe32((v))))
#define ioread32be(p) ((be32toh(*(p))))
#define iowrite32le(v, p) ((*((uint32_t *)(p)) = htole32((v))))
#define ioread32le(p) ((le32toh(*(p))))

#define iowr32(m, v, p) (m == MOD_BE ? iowrite32be((v), (p)) : iowrite32le((v), (p)))
#define iord32(m, p) (m == MOD_BE ? ioread32be((p)) : ioread32le((p)))

void atr_us(event_handle_t *event_handle_p);
vtq_usim_state_t vtq_usim_get_status(event_handle_t *event_handle_p);
#endif /* _USIM_EVENT_H */
