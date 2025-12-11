/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2022 NXP
 */
#ifndef __GUL_HAWK_H__
#define __GUL_HAWK_H__

#include "gul_base.h"

#define HAWKDEV_NAME_LEN			(20)

struct hawk_host_data {
	struct hawk_sw_cmd_desc cmd_local;
	struct hawk_swcmd_stat cmd_stat;
	u32 core_mask;
};

struct hawkdev {
	char name[HAWKDEV_NAME_LEN];
	struct gul_dev *gul_dev;
	struct hawk_host_if *p_hif;
	struct hawk_host_data host_data;
};

int __hawk_list_events_show(struct hawkdev *hawkdev, char *buf);
int __hawk_events_show(struct hawkdev *hawkdev, char *buf);
int __hawk_event_set(struct hawkdev *hawkdev, u32 event_output, u32 event);
int __hawk_event_reset(struct hawkdev *hawkdev);
int __hawk_core_mask_show(struct hawkdev *hawkdev, char *buf);
int __hawk_core_mask_set(struct hawkdev *hawkdev, u32 core_mask);
int __hawk_tid_show(struct hawkdev *hawkdev, char *buf);
int __hawk_tid_set(struct hawkdev *hawkdev, u32 tid);
int __hawk_mark_show(struct hawkdev *hawkdev, char *buf);
int __hawk_mark_set(struct hawkdev *hawkdev, bool enable);
int __hawk_report_show(struct hawkdev *hawkdev, char *buf);
int __hawk_record_set(struct hawkdev *hawkdev, bool enable);

int gul_hawk_probe(struct gul_dev *gul_dev, int virq_count,
		  struct virq_evt_map *virq_map);
int gul_hawk_remove(struct gul_dev *gul_dev);
#endif

