/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2022-2024 NXP
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/irqreturn.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <linux/swait.h>
#include <linux/delay.h>
#include <linux/eventfd.h>
#include <linux/tty.h>          /*  For the tty declarations */
#include <linux/printk.h>
#include "gul_base.h"
#include "gul_cli_ioctl.h"

#define DEVICE_NAME_LEN	16
#undef CLI_DEBUG

static dev_t dev_num;
static uint32_t dev_major;
static uint32_t dev_minor;
static struct class *gul_cli_dev_class;
static struct gul_cli_device_data *cli_dev_data_g[MAX_MODEM];
struct cli_dev *g_cli_dev;

struct cli_priv_data {
	int msi_index;
	int irq;
	int irq_flag;
	int id;
	uint64_t count;
	struct cli_mbox cli_geul_mbox;
	struct cli_mbox cli_host_mbox;
	struct swait_queue_head wq;
	raw_spinlock_t wq_lock;
	struct eventfd_ctx *evt_fd_ctxt;
};

struct cli_dev {
	char name[DEVICE_NAME_LEN];
	struct cli_priv_data cli_priv_d[MAX_MODEM];
	struct gul_dev *gul_dev[MAX_MODEM];
};

struct gul_cli_device_data {
	struct cli_dev *cli_dev;
	struct cdev cdev;
};

static int print_string(char *buf)
{
	struct tty_struct *my_tty;
	const struct tty_operations *ttyops;
	int len = strlen(buf);
	int printed_till_now = 0;

	if (cli_dmesg_on) {
		printed_till_now = printk_emit_dmesg_only(3 /*LOG_USER*/,
				6 /*KERN_INFO*/, NULL, 0, "%s\n", buf);
		while (printed_till_now < len) {
			printed_till_now += printk_emit_dmesg_only(3 /*LOG_USER*/,
					6 /*KERN_INFO*/, NULL, 0, "%s\n", &buf[printed_till_now]);
		}
	}
	my_tty = get_current_tty();
	/*
	 * If my_tty is NULL, the current task has no tty you can print to
	 * (ie, if it's a daemon).  If so, there's nothing we can do.
	 */
	if (!my_tty)
		return pr_info("%s\n", buf);

	printed_till_now = 0;
	ttyops = my_tty->driver->ops;
	printed_till_now = (ttyops->write) (my_tty, buf, len);
	(ttyops->write) (my_tty, "\015\012", 2);
	while(printed_till_now < len) {
		printed_till_now += (ttyops->write) (my_tty, &buf[printed_till_now], len);
		(ttyops->write) (my_tty, "\015\012", 2);
	}
	tty_kref_put(my_tty);

	return len;
}

ssize_t cli_device_dump(int id, char *buf)
{
	struct cli_priv_data *cli_priv_d;

	if (cli_dev_data_g[id] == NULL)
		return 0;

	cli_priv_d = &g_cli_dev->cli_priv_d[id];
		sprintf(&buf[strlen(buf)],
		" gul_cli_dev%d irq %d irq status=%d\n",
			id, cli_priv_d->irq,
			cli_priv_d->irq_flag);
	return 0;
}

static irqreturn_t cli_irq_handler(int irq, void *data)
{
#define SIGNAL_TO_CHANNEL_LISTENER	1
	struct cli_priv_data *cli_priv_d = (struct cli_priv_data *)data;

	if (cli_priv_d) {
		raw_spin_lock(&cli_priv_d->wq_lock);
		swake_up_all_locked(&cli_priv_d->wq);
		raw_spin_unlock(&cli_priv_d->wq_lock);

		if (cli_priv_d->evt_fd_ctxt)
			eventfd_signal(cli_priv_d->evt_fd_ctxt,
					SIGNAL_TO_CHANNEL_LISTENER);
	}

	return IRQ_HANDLED;
}

static int cli_register_irq(struct cli_dev *cli_dev, int cli_id)
{
	int ret = 0;
	struct cli_priv_data *cli_priv_d = NULL;

	cli_priv_d = &cli_dev->cli_priv_d[cli_id];

	if (cli_priv_d->irq != 0) {
		if (cli_priv_d->irq_flag == 0) {
			ret = request_irq(cli_priv_d->irq,
						cli_irq_handler,
						IRQF_NO_THREAD |
						    IRQF_TRIGGER_RISING,
						"cli_handler",
						cli_priv_d);
			if (ret < 0) {
				pr_err("%s: request irq err - %d\n",
						 __func__, ret);
				goto err;
			} else {
				cli_priv_d->irq_flag = 1;
			}
		} else {
			pr_err("%s IRQ %d is busy\n",
						__func__, cli_priv_d->irq);
		}
	} else {
		pr_err("%s: IRQ is invalid\n", __func__);
		ret = -EINVAL;
		goto err;
	}
err:
	return ret;
}

static void cli_deregister_irq(struct cli_dev *cli_dev, int cli_id)
{
	struct cli_priv_data *cli_priv_d = &cli_dev->cli_priv_d[cli_id];

	if (cli_priv_d != NULL) {
		if (cli_priv_d->irq_flag != 0) {
			cli_priv_d->irq_flag = 0;
			free_irq(cli_priv_d->irq, cli_priv_d);
		}
	}
}

static ssize_t gul_cli_dev_read(struct file *filp, char __user *buf,
			size_t count, loff_t *offset)
{
	int rc = 0;
	unsigned long flags;
	DECLARE_SWAITQUEUE(cli_wait);
	struct gul_cli_device_data *cli_dev_data = NULL;
	struct cli *cli_t = NULL;
	struct cli_dev *cli_dev;
	struct gul_dev *gul_dev;
	struct gul_hif *hif;
	int cli_id = 0;
	struct cli_priv_data *cli_priv_d = NULL;

	cli_dev_data = filp->private_data;
	cli_dev = cli_dev_data->cli_dev;

	cli_t = (struct cli *)buf;

	cli_id = cli_t->id;
	cli_priv_d = &cli_dev->cli_priv_d[cli_id];

	gul_dev = cli_dev->gul_dev[cli_id];
	hif = gul_dev->hif;

	raw_spin_lock_irqsave(&cli_priv_d->wq_lock, flags);
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	prepare_to_swait_exclusive(
#else
	prepare_to_swait(
#endif
		&cli_priv_d->wq,
		&cli_wait,
		TASK_INTERRUPTIBLE);
	raw_spin_unlock_irqrestore(&cli_priv_d->wq_lock, flags);

	schedule();

	raw_spin_lock_irqsave(&cli_priv_d->wq_lock, flags);
	finish_swait(&cli_priv_d->wq, &cli_wait);
	raw_spin_unlock_irqrestore(&cli_priv_d->wq_lock, flags);

	rc = put_user(hif->cli_geul_mbox.msg_id, &cli_t->mbox.msg_id);
	rc += put_user(hif->cli_geul_mbox.data, &cli_t->mbox.data);
	rc += put_user(hif->cli_geul_mbox.data2, &cli_t->mbox.data2);
	rc += put_user(hif->cli_geul_mbox.data3, &cli_t->mbox.data3);

	if (!rc)
		rc = sizeof(cli_priv_d->count);

	return rc;
}

static int gul_cli_dev_open(struct inode *inode, struct file *filp)
{
	struct gul_cli_device_data *cli_dev = NULL;

	cli_dev = container_of(inode->i_cdev,
				struct gul_cli_device_data,
				cdev);
	filp->private_data = cli_dev;

	return 0;
}

static int gul_cli_dev_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static long gul_cli_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	int ret = 0;
	struct cli cli_t = {0};
	struct cli *cli_ptr = &cli_t;
	struct gul_cli_device_data *cli_dev_data = NULL;
	struct cli_dev *cli_dev = NULL;
	struct task_struct *userspace_task = NULL;
	struct file *efd_file = NULL;
	struct cli_priv_data *cli_priv_d = NULL;
	struct gul_dev *dev = NULL;
	struct gul_hif *hif = NULL;
	struct gul_ep_log *ep_log;
	int i = 0, log_len = 0;
	static char buf[GUL_LOG_BUF_SIZE];
	int complete_log_size = 0;

	cli_dev_data = (struct gul_cli_device_data *)filp->private_data;
	cli_dev = cli_dev_data->cli_dev;

	switch (cmd) {
	case IOCTL_GUL_CLI_REGISTER_EVENTFD:
		ret = copy_from_user(&cli_t,
					(struct cli *)arg,
					sizeof(struct cli));

		if ((ret != 0) || (cli_t.id >= MAX_MODEM))
			return -EFAULT;

		cli_priv_d = &cli_dev->cli_priv_d[cli_t.id];

		cli_priv_d->evt_fd_ctxt = NULL;
		if (cli_t.event_fd > 0) {
			/* Get current task from which IOCTL was called */
			userspace_task = current;

			rcu_read_lock();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
			efd_file = files_lookup_fd_rcu(userspace_task->files,
					cli_t.event_fd);
#else
			efd_file = fcheck_files(userspace_task->files,
					cli_t.event_fd);
#endif
			rcu_read_unlock();

			cli_priv_d->evt_fd_ctxt =
				eventfd_ctx_fileget(efd_file);
		}

		break;

	case IOCTL_GUL_CLI_DEREGISTER_EVENTFD:
		ret = copy_from_user(&cli_t, (struct cli *)arg,
			sizeof(struct cli));
		if (ret != 0 || (cli_t.id >= MAX_MODEM))
			return -EFAULT;

		cli_priv_d = &cli_dev->cli_priv_d[cli_t.id];

		if (cli_priv_d->evt_fd_ctxt) {
			eventfd_ctx_put(cli_priv_d->evt_fd_ctxt);
			cli_priv_d->evt_fd_ctxt = NULL;
		}
		break;

	case IOCTL_GUL_CLI_SEND_MSI_TO_MODEM:
		ret = copy_from_user(&cli_t,
					(struct cli *)arg,
					sizeof(struct cli));
		if (ret != 0)
			return -EFAULT;

		cli_ptr = (struct cli *)arg;

		dev = cli_dev->gul_dev[cli_ptr->id];
		hif = dev->hif;

		writel(cli_t.mbox.msg_id, &hif->cli_host_mbox.msg_id);
		writel(cli_t.mbox.data, &hif->cli_host_mbox.data);
		writel(cli_t.mbox.data2, &hif->cli_host_mbox.data2);
		writel(cli_t.mbox.data3, &hif->cli_host_mbox.data3);

		/* Raise MSI interrupt to Modem */
		raise_modem_msi(dev, MSI_TYPE_A, HOST_CLI_IRQ);
		break;
	case IOCTL_GUL_CLI_DUMP_CORE_LOG:
		ret = copy_from_user(&cli_t, (struct cli *)arg,
				sizeof(struct cli));

		if (ret != 0)
			return -EFAULT;

		cli_ptr = (struct cli *)arg;

		dev = cli_dev->gul_dev[cli_ptr->id];
		ep_log = &dev->ep_log[cli_ptr->core_id];

		dev_dbg(dev->dev, "GUL log buf dump, vaddr %p, offset %d\n",
			ep_log->buf, ep_log->offset);

		log_len = gul_collect_ep_log(ep_log, buf);
		if (log_len == 0) {
			for (i = 0; i < ep_log->len; i++) {
				if (ep_log->buf[i] != 0) {
					ep_log->offset = i;
					log_len =
					    gul_collect_ep_log(ep_log, buf);
				}
			}
		}

		complete_log_size = strlen(buf);
		if (complete_log_size != 0) {
			print_string(buf);
			memset(buf, 0, sizeof(buf));
		}

		break;

	default:
		ret = -ENOTTY;
	}
	return 0;
}

static const struct file_operations gul_cli_dev_fops = {
	.owner      = THIS_MODULE,
	.open       = gul_cli_dev_open,
	.release    = gul_cli_dev_release,
	.read       = gul_cli_dev_read,
	.unlocked_ioctl = gul_cli_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gul_cli_dev_ioctl,
#endif
};

static int create_cli_cdevs(struct gul_cli_device_data **cli_dev_data, int id)
{
	int ret = 0;

	cdev_init(&cli_dev_data[id]->cdev, &gul_cli_dev_fops);
	cli_dev_data[id]->cdev.ops = &gul_cli_dev_fops;
	cli_dev_data[id]->cdev.owner = THIS_MODULE;

	/* Adding a device to the system */
	cdev_add(&cli_dev_data[id]->cdev, MKDEV(dev_major, id), 1);
	if ((device_create(gul_cli_dev_class,
				NULL,
				MKDEV(dev_major, id),
				NULL,
				"gul_cli_dev%d", id)) == NULL) {
		pr_err("%s: Cannot create the tvd device(%d)\n", __func__, id);
		ret = -1;
	}
	return ret;
}

int cli_probe(struct gul_dev *gul_dev, int virq_count,
					struct virq_evt_map *virq_map)
{
	struct cli_dev *cli_dev = g_cli_dev;
	struct gul_cli_device_data *cli_dev_data[MAX_MODEM];
	struct gul_hif *hif;
	int i;
	int ret = 0;

	i = gul_dev->id;
	cli_dev->gul_dev[i] = gul_dev;
	hif = gul_dev->hif;

	cli_dev->cli_priv_d[i].msi_index = gul_dev_get_msi(gul_dev);
	cli_dev->cli_priv_d[i].irq =
		gul_dev->irq[cli_dev->cli_priv_d[i].msi_index].irq_val;

	cli_dev->cli_priv_d[i].irq_flag = 0;

	hif->msi_cli = cli_dev->cli_priv_d[i].msi_index;

	cli_dev_data[i] = kmalloc(sizeof(struct gul_cli_device_data),
				    GFP_KERNEL);
	if (cli_dev_data[i] == NULL)
		return -ENOMEM;

	cli_dev->cli_priv_d[i].id = i;
	cli_dev_data[i]->cli_dev = cli_dev;
	cli_dev_data_g[i] = cli_dev_data[i];

	ret = create_cli_cdevs(cli_dev_data, i);
	if (ret < 0) {
		dev_err(gul_dev->dev, "Failed to create RefaApp chardevs");
		goto err;
	}

	gul_dev->cli_priv = cli_dev;

	/* simple wait queue init */
	init_swait_queue_head(&cli_dev->cli_priv_d[i].wq);
	raw_spin_lock_init(&cli_dev->cli_priv_d[i].wq_lock);

	ret = cli_register_irq(cli_dev, cli_dev->cli_priv_d[i].id);
	if (ret < 0)
		goto out;

	return 0;
out:
	gul_dev_put_msi(gul_dev, cli_dev->cli_priv_d[i].msi_index);

	cli_deregister_irq(cli_dev, i);

	cdev_del(&cli_dev_data[i]->cdev);
	device_destroy(gul_cli_dev_class, MKDEV(dev_major, i));
err:
	kfree(cli_dev_data[i]);
	cli_dev_data_g[i] = NULL;
	gul_dev->cli_priv = NULL;

	return ret;
}

int cli_remove(struct gul_dev *gul_dev)
{
	struct cli_dev *cli_dev;
	struct cli_priv_data *cli_priv_d;
	int id = gul_dev->id;

	cli_dev = gul_dev->cli_priv;

	if (!cli_dev)
		return 0;

	cli_priv_d = &cli_dev->cli_priv_d[id];
	cli_deregister_irq(cli_dev, id);

	if (cli_priv_d->irq_flag != 0)
		free_irq(cli_priv_d->irq, cli_priv_d);

	gul_dev_put_msi(gul_dev, cli_priv_d->msi_index);

	device_destroy(gul_cli_dev_class, MKDEV(dev_major, id));

	cdev_del(&cli_dev_data_g[id]->cdev);
	kfree(cli_dev_data_g[id]);
	cli_dev_data_g[id] = NULL;

	gul_dev->cli_priv = NULL;
	return 0;
}

int cli_init(void)
{
	int ret = -1;
	struct cli_dev *cli_dev = NULL;

	pr_info("%s: init called\n", __func__);

	/*Allocating chardev region and assigning Major number */
	ret = alloc_chrdev_region(&dev_num, dev_minor, MAX_MODEM,
		    "gul_cli_dev");
	if (ret < 0) {
		pr_err("%s: Failed in getting major number\n",
				 __func__);
		return ret;
	}

	/* Device Major number */
	dev_major = MAJOR(dev_num);

	cli_dev = kmalloc(sizeof(struct cli_dev), GFP_KERNEL);
	if (!cli_dev)
		goto err;

	memset(cli_dev, 0, sizeof(struct cli_dev));

	/* sysfs class creation */
	gul_cli_dev_class = class_create(THIS_MODULE, "gul_cli_dev");
	if (gul_cli_dev_class == NULL) {
		pr_err("%s:Cannot allocate major number\n", __func__);
		ret = -1;
		goto out;
	}

	g_cli_dev = cli_dev;
	return ret;

out:
	class_destroy(gul_cli_dev_class);
	kfree(cli_dev);
err:
	unregister_chrdev_region(dev_num, MAX_MODEM);

	return ret;
}
EXPORT_SYMBOL_GPL(cli_init);

int cli_exit(void)
{
	class_destroy(gul_cli_dev_class);
	kfree(g_cli_dev);
	unregister_chrdev_region(dev_num, MAX_MODEM);

	return 0;
}
EXPORT_SYMBOL_GPL(cli_exit);
