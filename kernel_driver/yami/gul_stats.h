/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2019-2022 NXP
 */

#ifndef __GUL_STATS_H__
#define __GUL_STATS_H__


#include <linux/fs.h>
#include <linux/cdev.h>

struct gul_dev;

#define GUL_STATS_DEV_NAME_PREFIX	"stats"
#define GUL_STATS_NAME_LEN		30
#define GUL_LOG_BUF_SIZE		(32 * 1024)

struct gul_stats_ops {
	ssize_t (*gul_show_stats)(void *stats_args, char *buf,
				  void *dev);
	void (*gul_reset_stats)(void *stats_args);
	void *stats_args;
};

struct gul_submodule_stats {
	struct gul_stats_ops stats_ops;
	struct list_head list;
};

struct gul_stats_desc {
	char name[GUL_STATS_NAME_LEN];
	uint32_t stats_control;
	dev_t devnr;
	struct device *dev;
	struct cdev cdev;
	struct list_head list;
	char *log_buf;
	uint32_t log_len;
	uint32_t copied_len;
	uint32_t status;
};

int gul_stats_init(struct gul_dev *gul_dev);
int gul_stats_exit(struct gul_dev *gul_dev);

#endif	/* __GUL_STATS_H_ */
