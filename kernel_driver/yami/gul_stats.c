/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2022 NXP
 */

#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#include <gul_host_if.h>
#include "gul_base.h"
#include "gul_stats.h"

int gul_host_add_stats(struct gul_dev *gul_dev,
		       struct gul_stats_ops *stats_ops)
{
	struct gul_submodule_stats *stats;
	struct gul_stats_desc *sd = &gul_dev->stats_desc;

	if (!stats_ops) {
		dev_err(gul_dev->dev, "GUL add stats");
		return -EINVAL;
	}

	stats = kzalloc(sizeof(struct gul_submodule_stats), GFP_KERNEL);
	if (stats == NULL) {
		dev_err(gul_dev->dev, "%s: memory alloc fail!\n", __func__);
		return -ENOMEM;
	}

	memcpy(&stats->stats_ops, stats_ops, sizeof(struct gul_stats_ops));
	list_add_tail(&stats->list, &sd->list);

	return 0;
}

static ssize_t gul_stats_collect(struct gul_stats_desc *sd)
{
	ssize_t list_stats_len = 0, stats_len = 0;
	struct gul_submodule_stats *stats;
	struct gul_dev *gul_dev = container_of(sd, struct gul_dev, stats_desc);
	char *buf = sd->log_buf;

	list_for_each_entry(stats, &sd->list, list) {
		if (stats_len > GUL_LOG_BUF_SIZE) {
			dev_err(gul_dev->dev, "GUL stats > max allocated mem\n");
			return stats_len;
		}
		list_stats_len = stats->stats_ops.gul_show_stats(
				stats->stats_ops.stats_args, buf, gul_dev);
		stats_len += list_stats_len;
		buf += list_stats_len;
	}

	return stats_len;
}

static int gul_stats_reset(struct gul_stats_desc *sd)
{
	struct gul_submodule_stats *stats;

	list_for_each_entry(stats, &sd->list, list) {
		stats->stats_ops.gul_reset_stats(stats->stats_ops.stats_args);
	}
	return 0;
}

static void gul_del_stats_list(struct gul_stats_desc *sd)
{
	struct gul_submodule_stats *stats, *temp;

	list_for_each_entry_safe(stats, temp, &sd->list, list) {
		list_del(&stats->list);
		kfree(stats);
	}
}

static int gul_stats_open(struct inode *inode, struct file *file)
{
	struct gul_stats_desc *sd =  container_of(inode->i_cdev,
						  struct gul_stats_desc, cdev);
	file->private_data = sd;

	return 0;
}

static int gul_stats_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t gul_stats_write(struct file *file, const char __user *ubuf,
			   size_t count, loff_t *f_pos)
{
	struct gul_stats_desc *sd = file->private_data;

	return gul_stats_reset(sd);
}

static ssize_t gul_stats_read(struct file *file, char __user *buf, size_t size,
			      loff_t *offset)
{
	struct gul_stats_desc *sd = file->private_data;
	size_t bytes = 0;
	char data;

	if (sd->status == 0) {
		sd->copied_len = 0;
		sd->log_len = gul_stats_collect(sd);
		sd->status = 1;
	}

	while ((size != bytes) && (sd->log_len > sd->copied_len)) {
		data = sd->log_buf[sd->copied_len];
		if (copy_to_user(&buf[bytes], &data, 1))
			return -EFAULT;
		sd->copied_len++;
		++bytes;
	}

	if (bytes == 0)
		sd->status = 0;

	return bytes;
}

static const struct file_operations gul_stats_fops = {
	.owner = THIS_MODULE,
	.open = gul_stats_open,
	.release = gul_stats_release,
	.read = gul_stats_read,
	.write = gul_stats_write,
};

int gul_stats_init(struct gul_dev *gul_dev)
{
	int ret;

	struct gul_stats_desc *sd = &gul_dev->stats_desc;

	sprintf(sd->name, "%s%s", gul_dev->name, GUL_STATS_DEV_NAME_PREFIX);

	dev_dbg(gul_dev->dev, "stat device name : %s\n", sd->name);

	ret = alloc_chrdev_region(&sd->devnr, 0, 1,
				  GUL_STATS_DEV_NAME_PREFIX);
	if (ret < 0) {
		dev_err(gul_dev->dev, "stats: major number failure %d %s\n",
			ret, sd->name);
		return ret;
	}

	sd->dev = device_create(gul_dev->class, NULL,
				sd->devnr,
				NULL, sd->name);
	if (IS_ERR(sd->dev)) {
		dev_err(gul_dev->dev, "Error device_create %s\n", sd->name);
		ret = -ENODEV;
	}

	cdev_init(&sd->cdev, &gul_stats_fops);
	sd->cdev.owner = THIS_MODULE;
	sd->cdev.ops = &gul_stats_fops;
	ret = cdev_add(&sd->cdev, sd->devnr, 1);
	if (ret) {
		dev_err(gul_dev->dev, "Error %d adding %s\n", ret, sd->name);
		goto fail_cdev;
	}

	sd->log_buf = vmalloc(GUL_LOG_BUF_SIZE);
	if (!(sd->log_buf)) {
		dev_err(gul_dev->dev, "vmalloc failed for %s\n", sd->name);
		goto fail_mem;
	}

	sd->copied_len = 0;
	sd->status = 0;

	INIT_LIST_HEAD(&sd->list);

	return ret;

fail_mem:
	device_destroy(gul_dev->class, sd->devnr);
	cdev_del(&sd->cdev);
fail_cdev:
	unregister_chrdev_region(sd->devnr, 1);

	return ret;
}

int gul_stats_exit(struct gul_dev *gul_dev)
{
	struct gul_stats_desc *sd = &gul_dev->stats_desc;

	gul_del_stats_list(sd);
	vfree(sd->log_buf);
	cdev_del(&sd->cdev);
	device_destroy(gul_dev->class, sd->devnr);
	unregister_chrdev_region(sd->devnr, 1);

	return 0;
}
