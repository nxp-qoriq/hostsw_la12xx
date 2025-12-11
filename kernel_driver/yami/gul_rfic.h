/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2022 NXP
 */
#ifndef __GUL_RFIC_H__
#define __GUL_RFIC_H__

#include "gul_base.h"
#include <rfdev_ioctl.h>

#define RFDEV_NAME_LEN 20
struct rf_host_stats {
	int sw_cmds_tx;
	int sw_cmds_failed;
	int sw_cmds_timed_out;
	int sw_cmds_desc_busy;
};

struct rfdev {
	char name[RFDEV_NAME_LEN];
	struct gul_dev *gul_dev;
	struct rf_host_if *r_hif;
	struct semaphore mdata_lock;
	int swcmd_common_size;
	struct rf_sw_cmd_desc cmd_local;
	struct cdev cdev;
	dev_t rfdevnr;
	uint8_t minor;
	struct rf_host_stats host_stats;
	sys_map_t sys_map;
};

void __rf_dump_hif(struct rfdev *rfdev);
#ifdef RF_FR2_DRVR_ENABLED
int __rf_control_stress_test(struct rfdev *rfdev, u32 start);
int __rf_enable_disable_bg_task(struct rfdev *rfdev, u32 start);
int __rf_read_regs(struct rfdev *rfdev, u16 addr, int count,
	u32 *buf);
int __rf_write_regs(struct rfdev *rfdev, int count,
	struct rif_write_reg_buf *reg_buf);
int __rf_set_vcxo_dac(struct rfdev *rfdev, u16 index);
int __rf_set_tx_gain(struct rfdev *rfdev, u8 lut_index);
int __rf_set_rx_gain(struct rfdev *rfdev, u8 beam_mask, u8 lut_index);
int __rf_switch_tx(struct rfdev *rfdev);
int __rf_switch_rx(struct rfdev *rfdev);
int __rf_set_freq_tx(struct rfdev *rfdev,
		     u32 rf_freq_khz, u32 ppm,
		     u32 ref_freq_khz, u32 init);
int __rf_set_auto_tune_pll(struct rfdev *rfdev, u8 iter);
int __rf_set_rx_agc_gain_index(struct rfdev *rfdev,
			       u8 beam_mask, u8 lut_index);
int __rfic_set_beambook_index_tx(struct rfdev *rfdev, u8 bbk_index);
int __rfic_set_beambook_index_rx(struct rfdev *rfdev, u8 bbk_index);
int __rfic_set_target_broadcast_tx_fe(struct rfdev *rfdev);
int __rfic_set_target_broadcast_rx_fe(struct rfdev *rfdev);
int __rfic_set_pbk_mode_tx(struct rfdev *rfdev, rfic_pbk_modes_t mode);
int __rfic_set_pbk_mode_rx(struct rfdev *rfdev, rfic_pbk_modes_t mode);
int __rfic_increment_pbk_index(struct rfdev *rfdev);
int __rfic_config_dynamic_controller(struct rfdev *rfdev,
				     rfic_cfg_dynamic_ctrl_t dynamic_op,
				     u8 enable);
int __rfic_write_list_to_pbk_memory_tx(struct rfdev *rfdev, u8 pbk_length);
int __rfic_write_list_to_pbk_memory_rx(struct rfdev *rfdev, u8 pbk_length);
int __rfic_set_beam_config(struct rfdev *rfdev, u8 beam);

int rf_read_chip_regs(struct rfdev *rfdev, u16 addr, int count,
	u32 *buf, int chip);
int rf_write_chip_regs(struct rfdev *rfdev, int count,
	struct rif_write_reg_buf *reg_buf, int chip);
#endif /* RF_FR2_DRVR_ENABLED */
int gul_rfic_probe(struct gul_dev *gul_dev, int virq_count,
		  struct virq_evt_map *virq_map);
int gul_rfic_remove(struct gul_dev *gul_dev);
int gul_rfic_init(void);
int gul_rfic_exit(void);
#endif

