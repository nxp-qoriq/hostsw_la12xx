/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2026 NXP
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/rcupdate.h>
#include <linux/eventfd.h>
#include <linux/irqreturn.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/wait.h>
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
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include "gul_base.h"

#define WDOG_DEVICE_NAME_LEN 16

static int wdog_dev_major;
static struct class *gul_wdog_dev_class;
static dev_t wdog_dev_number;
static struct gul_wdog_device_data *wdog_dev_data_g[MAX_MODEM];
static char wdog_probed_once[MAX_MODEM];
extern struct gul_modem_dev_id gul_dev_id[MAX_MODEM];

struct wdog_priv {
	int irq;
	int irq_status_flag;
	int gpio;
	int wdog_id;
	int wdog_modem_status;
	int domain_nr;
	uint64_t wdog_count;
	bool uspace_registered;
	struct gul_dev *gul_dev;
	struct swait_queue_head wdog_wq;
	raw_spinlock_t wdog_wq_lock;
	struct eventfd_ctx *evt_fd_ctxt;
	struct work_struct modem_wq;
};

struct wdog_dev {
	char name[WDOG_DEVICE_NAME_LEN];
	struct wdog_priv wdog_priv_t[1];
};

/*Watchdog char Dev data holder */
struct gul_wdog_device_data {
	struct wdog_dev *wdog_dev;
	struct cdev cdev;
};

static int wdog_register_irq(struct wdog_dev *wdog_dev, struct wdog *wdog_t);
static void wdog_deregister_irq(struct wdog_dev *wdog_dev, struct wdog *wdog_t);
static int wdog_gpio_config(struct wdog_dev *wdog_dev, struct wdog *wdog_t);
static void wdog_reset_modem(struct wdog_dev *wdog_dev, struct wdog *wdog_t);

ssize_t
wdog_device_dump(int id, char *buf)
{
	struct wdog_priv *wpriv;

	wpriv = &wdog_dev_data_g[id]->wdog_dev->wdog_priv_t[0];

	sprintf(&buf[strlen(buf)],
		" WDOG:gulwdogdev%d irq %d (status %d),gpio %d modem status=%d\n",
		id, wpriv->irq,
		wpriv->irq_status_flag,
		wpriv->gpio,
		wpriv->wdog_modem_status);
	return 0;
}

/* Watchdog IRQ Handler */
static irqreturn_t wdog_irq_handler(int irq, void *data)
{
#define SIGNAL_TO_CHANNEL_LISTENER	1
	struct wdog_priv *wdog_priv_t = (struct wdog_priv *)data;

	if (wdog_priv_t != NULL) {
		pr_debug("%s: WDOG IRQ handler for irq %d is called, modem_id: %d\n",
						__func__, irq, wdog_priv_t->wdog_id);
		if (wdog_priv_t->wdog_modem_status != WDOG_MODEM_READY)
			return IRQ_HANDLED;
		else
			schedule_work(&wdog_priv_t->modem_wq);
	}
	return IRQ_HANDLED;
}

static int wdog_register_irq(struct wdog_dev *wdog_dev,
					struct wdog *wdog_t)
{
	int ret = 0;
	int wdog_interrupt_flags =  IRQF_NO_THREAD | IRQF_TRIGGER_RISING;
	struct wdog_priv *wdog_priv_t = NULL;
	struct task_struct *userspace_task = NULL;
	struct file *efd_file = NULL;

	wdog_priv_t = &wdog_dev->wdog_priv_t[wdog_t->wdogid];

	wdog_priv_t->evt_fd_ctxt = NULL;

	if (wdog_t->wdog_eventfd > 0) {
		/* Get current task context from which IOCTL was called */
		userspace_task = current;

		rcu_read_lock();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,0,0)
		efd_file = files_lookup_fd_raw(userspace_task->files,
					wdog_t->wdog_eventfd);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
		efd_file = files_lookup_fd_rcu(userspace_task->files,
					wdog_t->wdog_eventfd);
#else
		efd_file = fcheck_files(userspace_task->files,
					wdog_t->wdog_eventfd);
#endif
		rcu_read_unlock();

		wdog_priv_t->evt_fd_ctxt = eventfd_ctx_fileget(efd_file);
	}

	if (wdog_priv_t->irq != 0) {
		if (wdog_priv_t->irq_status_flag == 0) {
			/* IRQ request */
			ret = request_irq(wdog_priv_t->irq, wdog_irq_handler,
				wdog_interrupt_flags,
				"wdog_handler",
				wdog_priv_t);
			if (ret < 0) {
				pr_err("%s request irq %px(%d) -(%x) err - %d\n",
					__func__, wdog_dev, wdog_t->wdogid, wdog_priv_t->irq, ret);
				goto err;
			} else {
				wdog_priv_t->irq_status_flag = 1;
			}

		} else {
			pr_err("%s request irq %px(%d) -(%x) busy - %d\n",
					__func__, wdog_dev, wdog_t->wdogid, wdog_priv_t->irq, ret);
		}

	} else {
		pr_err("%s request irq %px(%d) -(%x) err - %d\n",
			__func__, wdog_dev, wdog_t->wdogid, wdog_priv_t->irq, ret);

		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	if (wdog_priv_t->evt_fd_ctxt) {
		eventfd_ctx_put(wdog_priv_t->evt_fd_ctxt);
		wdog_priv_t->evt_fd_ctxt = NULL;
	}

	return ret;
}

static void wdog_deregister_irq(struct wdog_dev *wdog_dev,
					struct wdog *wdog_t)
{
	struct wdog_priv *wdog_priv_t = &wdog_dev->wdog_priv_t[wdog_t->wdogid];

	if (wdog_priv_t != NULL) {
		/* IRQ request */
		if (wdog_priv_t->irq_status_flag != 0) {
			wdog_priv_t->irq_status_flag = 0;
			free_irq(wdog_priv_t->irq, wdog_priv_t);
		}

		if (wdog_priv_t->evt_fd_ctxt) {
			eventfd_ctx_put(wdog_priv_t->evt_fd_ctxt);
			wdog_priv_t->evt_fd_ctxt = NULL;
		}
	}
}

static int wdog_gpio_config(struct wdog_dev *wdog_dev, struct wdog *wdog_t)
{
	int ret = 0;
	struct wdog_priv *wdog_priv_t = NULL;

	wdog_priv_t = &wdog_dev->wdog_priv_t[wdog_t->wdogid];

	if (wdog_priv_t->gpio != 0) {
		/*GPIO request*/
		ret = gpio_request(wdog_priv_t->gpio,
				"watchdog modem reset");
		if (ret) {
			pr_err("%s:Can't request gpio %d\n", __func__, ret);
			goto err;
		}

		/* Configure GPIO pin as an output */
		ret = gpio_direction_output(wdog_priv_t->gpio, 1);
		if (ret < 0) {
			pr_err("%s: Can't configure gpio %d\n", __func__, ret);
			gpio_free(wdog_priv_t->gpio);
			goto err;
		}

	} else {
		pr_err("%s: GPIO is invalid\n", __func__);
		ret = -EINVAL;
		goto err;
	}
err:
	return ret;
}

static void wdog_reset_modem(struct wdog_dev *wdog_dev, struct wdog *wdog_t)
{
	struct wdog_priv *wdog_priv_t = NULL;

	if (wdog_t->wdogid >= MAX_MODEM) {
		pr_err("%s: wdog_id  is invalid\n", __func__);
	} else {
		wdog_priv_t = &wdog_dev->wdog_priv_t[wdog_t->wdogid];
		while (gul_dev_id[wdog_priv_t->wdog_id].is_pci_dev_removed == 0) {
			udelay(100);
		}

		if (wdog_priv_t->gpio != 0) {
			pr_info("%s: Resetting Modem\n", __func__);
			gpio_set_value_cansleep(wdog_priv_t->gpio, 0);
			mdelay(1);
			gpio_set_value_cansleep(wdog_priv_t->gpio, 1);
		} else {
			pr_err("%s: GPIO is invalid\n", __func__);
		}
	}
}

void wdog_reset_modem_ext(unsigned int id)
{
	struct wdog wdog_t = {0};

	wdog_reset_modem(wdog_dev_data_g[id]->wdog_dev, &wdog_t);
	if (wdog_dev_data_g[id]->wdog_dev->wdog_priv_t[0].irq_status_flag != 0)
		wdog_deregister_irq(wdog_dev_data_g[id]->wdog_dev, &wdog_t);
}
EXPORT_SYMBOL(wdog_reset_modem_ext);

int wdog_set_modem_status(int wdog_id, int status)
{
	struct wdog_priv *wdog_priv_t = NULL;
	unsigned long flags;

	if (wdog_id >= MAX_MODEM)
		return -ENODEV;

	if (wdog_dev_data_g[wdog_id] == NULL)
		return -ENODEV;

	wdog_priv_t = &wdog_dev_data_g[wdog_id]->wdog_dev->wdog_priv_t[0];
	if (wdog_priv_t) {
		wdog_priv_t->wdog_modem_status = status;
		/* Send signal to Wake up the watchdog wait queue */
		pr_info("%s: set modem (%d) status %d\n", __func__, wdog_id,
				wdog_priv_t->wdog_modem_status);
		raw_spin_lock_irqsave(&wdog_priv_t->wdog_wq_lock, flags);
		swake_up_all_locked(&wdog_priv_t->wdog_wq);
		raw_spin_unlock_irqrestore(&wdog_priv_t->wdog_wq_lock, flags);
	}
	return 0;
}

int wdog_set_pci_domain_nr(int wdog_id, int domain_nr)
{
	struct wdog_priv *wdog_priv_t = NULL;

	if (wdog_id >= MAX_MODEM)
		return -ENODEV;

	if (wdog_dev_data_g[wdog_id] == NULL)
		return -ENODEV;

	wdog_priv_t = &wdog_dev_data_g[wdog_id]->wdog_dev->wdog_priv_t[0];
	wdog_priv_t->domain_nr = domain_nr;
	pr_debug("%s: set domain %d\n", __func__, wdog_priv_t->domain_nr);

	return 0;
}

static void wdog_get_modem_status(struct wdog_dev *wdog_dev,
						struct wdog *wdog_t)
{
	struct wdog_priv *wdog_priv_t = NULL;

	wdog_priv_t = &wdog_dev->wdog_priv_t[wdog_t->wdogid];
	pr_info("%s: modem (%d) - %d\n", __func__,
			wdog_priv_t->wdog_id, wdog_priv_t->wdog_modem_status);
	wdog_t->wdog_modem_status = wdog_priv_t->wdog_modem_status;
}

static void wdog_get_pci_domain_nr(struct wdog_dev *wdog_dev,
						struct wdog *wdog_t)
{
	struct wdog_priv *wdog_priv_t = NULL;

	wdog_priv_t = &wdog_dev->wdog_priv_t[wdog_t->wdogid];
	pr_debug("Wdog pci domain: %d\n", wdog_priv_t->domain_nr);
	wdog_t->domain_nr = wdog_priv_t->domain_nr;
}

static int gul_wdog_dev_open(struct inode *inode, struct file *filp)
{
	struct gul_wdog_device_data *wdog_dev = NULL;

	wdog_dev = container_of(inode->i_cdev,
			struct gul_wdog_device_data,
			cdev);
	filp->private_data = wdog_dev;
	return 0;
}

static int gul_wdog_dev_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static ssize_t gul_wdog_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offset)
{
	int rc = 0;
	unsigned long flags;
	DECLARE_SWAITQUEUE(wdog_wait);
	struct gul_wdog_device_data *wdog_dev_data = NULL;

	wdog_dev_data = filp->private_data;
	raw_spin_lock_irqsave(
		&wdog_dev_data->wdog_dev->wdog_priv_t->wdog_wq_lock,
		flags);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	prepare_to_swait_exclusive(
#else
	prepare_to_swait(
#endif
		&wdog_dev_data->wdog_dev->wdog_priv_t->wdog_wq,
		&wdog_wait,
		TASK_INTERRUPTIBLE);
	raw_spin_unlock_irqrestore(
		&wdog_dev_data->wdog_dev->wdog_priv_t->wdog_wq_lock,
		flags);

	/*Now wait here, wdog notificaion will wakeup*/
	schedule();

	raw_spin_lock_irqsave(
		&wdog_dev_data->wdog_dev->wdog_priv_t->wdog_wq_lock,
		flags);
	finish_swait(
		&wdog_dev_data->wdog_dev->wdog_priv_t->wdog_wq,
		&wdog_wait);
	raw_spin_unlock_irqrestore(
		&wdog_dev_data->wdog_dev->wdog_priv_t->wdog_wq_lock,
		flags);

	rc = put_user(
		wdog_dev_data->wdog_dev->wdog_priv_t[0].wdog_modem_status,
		(int *)buf);
	if (!rc)
		rc = sizeof(
			wdog_dev_data->wdog_dev->wdog_priv_t->wdog_count);
	return rc;
}

static long gul_wdog_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	int ret = 0;
	struct wdog wdog_t = {0};
	struct gul_wdog_device_data *wdog_dev_data = NULL;
	struct wdog_dev *wdog_dev = NULL;

	wdog_dev_data = (struct gul_wdog_device_data *)filp->private_data;
	wdog_dev = wdog_dev_data->wdog_dev;

	switch (cmd) {
	case IOCTL_GUL_MODEM_WDOG_REGISTER:
		ret = copy_from_user(&wdog_t, (struct wdog *)arg,
				sizeof(struct wdog));
		if (ret != 0)
			return -EFAULT;
		wdog_dev->wdog_priv_t[0].uspace_registered = 1;
		wdog_handler_notifier[wdog_dev->wdog_priv_t[0].wdog_id] = 1;
		ret = wdog_register_irq(wdog_dev, &wdog_t);
		break;

	case IOCTL_GUL_MODEM_WDOG_DEREGISTER:
		ret = copy_from_user(&wdog_t, (struct wdog *)arg,
				sizeof(struct wdog));
		if (ret != 0)
			return -EFAULT;
		wdog_deregister_irq(wdog_dev, &wdog_t);
		wdog_handler_notifier[wdog_dev->wdog_priv_t[0].wdog_id] = 0;
		break;

	case IOCTL_GUL_MODEM_WDOG_RESET:
		ret = copy_from_user(&wdog_t, (struct wdog *)arg,
				sizeof(struct wdog));
		if (ret != 0)
			return -EFAULT;

		wdog_reset_modem(wdog_dev, &wdog_t);
		break;
	case IOCTL_GUL_MODEM_WDOG_GET_STATUS:
		ret = copy_from_user(&wdog_t, (struct wdog *)arg,
				sizeof(struct wdog));
		if (ret != 0)
			return -EFAULT;
		wdog_get_modem_status(wdog_dev, &wdog_t);

		ret = copy_to_user((struct wdog *)arg,
				&wdog_t,
				sizeof(struct wdog));
		if (ret != 0)
			return -EFAULT;
		break;

	case IOCTL_GUL_MODEM_WDOG_GET_DOMAIN:
		ret = copy_from_user(&wdog_t, (struct wdog *)arg,
				sizeof(struct wdog));
		if (ret != 0)
			return -EFAULT;
		wdog_get_pci_domain_nr(wdog_dev, &wdog_t);

		ret = copy_to_user((struct wdog *)arg,
				&wdog_t,
				sizeof(struct wdog));
		if (ret != 0)
			return -EFAULT;
		break;

	default:
		ret = -ENOTTY;
	}
	return ret;
}
/* Watchdog initialize file_operations */
static const struct file_operations gul_wdog_dev_fops = {
	.owner      = THIS_MODULE,
	.open       = gul_wdog_dev_open,
	.release    = gul_wdog_dev_release,
	.read       = gul_wdog_dev_read,
	.unlocked_ioctl = gul_wdog_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gul_wdog_dev_ioctl,
#endif
};

static int create_wdog_cdevs(void)
{
	/*Allocating chardev region
	 * and assigning Major number
	 */
	if (alloc_chrdev_region(&wdog_dev_number,
				0,
				MAX_MODEM,
				"gulwdogdev") < 0) {
		pr_err("%s:Cannot allocate major number\n",
				__func__);
		return -1;
	}
	/* Device Major number
	 */
	wdog_dev_major = MAJOR(wdog_dev_number);
	/*sysfs class creation
	 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	gul_wdog_dev_class = class_create("gulwdogdev");
#else
	gul_wdog_dev_class = class_create(THIS_MODULE, "gulwdogdev");
#endif
	if (gul_wdog_dev_class == NULL) {
		pr_err("%s:Cannot allocate major number\n",
				__func__);
		goto out_class;
	}
	return 0;
out_class:
	unregister_chrdev_region(wdog_dev_number, MAX_MODEM);
	return -1;
}
static void modem_worker(struct work_struct *work)
{
	struct wdog_priv *wdog_priv_t = container_of(work,
			struct wdog_priv, modem_wq);

	if (readl(&wdog_priv_t->gul_dev->hif->warmup_info.warmup_flags) &
					GUL_WDOG_WARMUP_MODEM_RESET){
		wdog_priv_t->wdog_modem_status = WDOG_MODEM_WARMUP_RESET;
	} else {
		if (wdog_priv_t->wdog_modem_status ==
				WDOG_MODEM_HSDCS_ERR)
			; /*do nothing with modem status*/
		else
			wdog_priv_t->wdog_modem_status =
					WDOG_MODEM_NOT_READY;
	}
	if (wdog_priv_t->uspace_registered) {
		if (wdog_priv_t->evt_fd_ctxt)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,19,0)
			eventfd_signal(wdog_priv_t->evt_fd_ctxt,
					SIGNAL_TO_CHANNEL_LISTENER);
#else
			eventfd_signal(wdog_priv_t->evt_fd_ctxt);
#endif

		/* Send signal to Wake up the watchdog wait queue */
		raw_spin_lock(&wdog_priv_t->wdog_wq_lock);
		if (warmup_flag[wdog_priv_t->wdog_id])
			warmup_flag[wdog_priv_t->wdog_id] = 0;

		swake_up_all_locked(&wdog_priv_t->wdog_wq);
		raw_spin_unlock(&wdog_priv_t->wdog_wq_lock);
	} else {
		if (warmup_flag[wdog_priv_t->wdog_id] ||
		wdog_priv_t->wdog_modem_status == WDOG_MODEM_HSDCS_ERR) {
			warmup_flag[wdog_priv_t->wdog_id] = 0;
			gul_pcidev_reset_device(wdog_priv_t->wdog_id);
		}
	}
}

int wdog_probe(struct gul_dev *gul_dev, int virq_count,
		struct virq_evt_map *virq_map)
{
	struct wdog_dev *wdog_dev = NULL;
	struct gul_wdog_device_data *wdog_dev_data;
	struct wdog wdog_t = {0};
	int i;
	int rc = 0;
	struct device_node *dn_modem_wdog;
	struct wdog wdog_t_irq = {0};

	/* Watchdog Interrupt Number and GPIO
	 * number extraction from device node
	 */
	if (disable_sideband) {
		dev_info(gul_dev->dev,
			"Watchdog: Not initialized. Sideband connections not available\n");
		return 0;
	}

	if (gul_dev->dn_modem)
		of_node_get(gul_dev->dn_modem);

	dn_modem_wdog = of_find_node_by_name(gul_dev->dn_modem, "modem_wdog");
	if (!dn_modem_wdog) {
		dev_err(gul_dev->dev, "modem_wdog:Node missing in DTB\n");
		return -ENODEV;
	}

	i = gul_dev->id;

	/*Probe will only be done for the lifetime of a given PCI modem */
	if (wdog_probed_once[i]) {
		wdog_dev_data_g[i]->wdog_dev->wdog_priv_t[0].gul_dev = gul_dev;
		return 0;
	}

	wdog_probed_once[i] = 1;

	dev_dbg(gul_dev->dev, "In probe function\n");

	wdog_dev = kmalloc(sizeof(struct wdog_dev), GFP_KERNEL);
	if (wdog_dev == NULL) {
		dev_err(gul_dev->dev, "WDOG device alloc failed\n");
		return -ENOMEM;
	}

	wdog_dev->wdog_priv_t[0].irq_status_flag = 0;
	wdog_dev->wdog_priv_t[0].irq = of_irq_get(dn_modem_wdog, 0);
	if (wdog_dev->wdog_priv_t[0].irq < 0) {
		dev_err(gul_dev->dev,
			"modem_wdog:IRQ not available in DTB for Modem Id:%d\n",
			i);
		rc = -EINVAL;
		goto out_irq;
	}

	wdog_dev->wdog_priv_t[0].gpio =
		of_get_named_gpio(dn_modem_wdog, "modem-reset-gpio", 0);
	if (!gpio_is_valid(wdog_dev->wdog_priv_t[0].gpio)) {
		dev_err(gul_dev->dev,
			"modem_wdog:id(%d)modem-reset-gpio not found\n", i);
		rc = -EINVAL;
		goto out_irq;
	}

	rc = wdog_gpio_config(wdog_dev, &wdog_t);
	if (rc < 0) {
		dev_err(gul_dev->dev, "WDOG GPIO config failed\n");
		rc = -ENODEV;
		goto out_gpio;
	}

	wdog_dev_data = kmalloc(sizeof(struct gul_wdog_device_data),
			GFP_KERNEL);
	if (wdog_dev_data == NULL) {
		rc = -ENOMEM;
		goto out_gpio;
	}

	wdog_dev->wdog_priv_t[0].wdog_id = i;
	wdog_dev->wdog_priv_t[0].wdog_modem_status = WDOG_MODEM_NOT_READY;
	wdog_dev->wdog_priv_t[0].domain_nr = PCI_DOMAIN_NR_INVALID;
	wdog_dev_data->wdog_dev = wdog_dev;
	wdog_dev_data_g[i] = wdog_dev_data;
	wdog_dev->wdog_priv_t[0].uspace_registered = 0;
	wdog_dev->wdog_priv_t[0].gul_dev = gul_dev;
	INIT_WORK(&wdog_dev->wdog_priv_t[0].modem_wq, modem_worker);

	if (warmup_flag[i])
		wdog_register_irq(wdog_dev, &wdog_t_irq);

	cdev_init(&wdog_dev_data->cdev, &gul_wdog_dev_fops);
	wdog_dev_data->cdev.ops = &gul_wdog_dev_fops;
	wdog_dev_data->cdev.owner = THIS_MODULE;
	/* Adding a device to the system:
	 * i-Minor number of new device
	 */
	cdev_add(&wdog_dev_data->cdev, MKDEV(wdog_dev_major, i), 1);
	if ((device_create(gul_wdog_dev_class,
				NULL,
				MKDEV(wdog_dev_major, i),
				NULL,
				"gulwdogdev%d", i)) == NULL) {
		dev_err(gul_dev->dev, "Cannot create the device");
		rc = -ENODEV;
		goto out_device;
	}

	/*simple wait queue init*/
	init_swait_queue_head(&wdog_dev->wdog_priv_t[0].wdog_wq);
	/*raw spinlock init for Watchdog module*/
	raw_spin_lock_init(&wdog_dev->wdog_priv_t[0].wdog_wq_lock);

	return 0;

out_device:
	cdev_del(&wdog_dev_data->cdev);
	kfree(wdog_dev_data);
	wdog_dev_data_g[i] = NULL;
out_gpio:
	if (wdog_dev->wdog_priv_t[0].gpio)
		gpio_free(wdog_dev->wdog_priv_t[0].gpio);
out_irq:
	if (wdog_dev->wdog_priv_t[0].irq_status_flag != 0)
		free_irq(wdog_dev->wdog_priv_t[0].irq,
			&wdog_dev->wdog_priv_t[0]);

	kfree(wdog_dev);
	return rc;
}
EXPORT_SYMBOL_GPL(wdog_probe);

int wdog_init(void)
{

	int rc = 0;

	rc = create_wdog_cdevs();
	if (rc < 0) {
		pr_err("modem_wdog: Failed to create chardevs\n");
		return rc;
	}
	return 0;
}

int gul_reset_modem(int modem_id, enum wdog_modem_status modem_status)
{
	struct wdog_priv *wdog_priv_t = NULL;

	wdog_priv_t =  &wdog_dev_data_g[modem_id]->wdog_dev->wdog_priv_t[0];
	if (wdog_priv_t != NULL) {
		wdog_priv_t->wdog_modem_status = modem_status;
		schedule_work(&wdog_priv_t->modem_wq);
	}
	return 0;
}
EXPORT_SYMBOL(gul_reset_modem);

int wdog_exit(void)
{
	int wdog_id;
	struct wdog_priv *wdog_priv_t = NULL;
	struct wdog wdog_t = {0};

	for (wdog_id = 0; wdog_id < MAX_MODEM; wdog_id++) {
		if (wdog_dev_data_g[wdog_id] == NULL)
			continue;

		wdog_priv_t =
			&wdog_dev_data_g[wdog_id]->wdog_dev->wdog_priv_t[0];

		wdog_reset_modem(wdog_dev_data_g[wdog_id]->wdog_dev, &wdog_t);
		if (wdog_priv_t->irq_status_flag != 0)
			free_irq(wdog_priv_t->irq, wdog_priv_t);
		if (wdog_priv_t->gpio)
			gpio_free(wdog_priv_t->gpio);
		device_destroy(gul_wdog_dev_class,
			MKDEV(wdog_dev_major, wdog_id));
		cdev_del(&wdog_dev_data_g[wdog_id]->cdev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
		cancel_work_sync(&wdog_priv_t->modem_wq);
#else
		flush_scheduled_work();
#endif
		mdelay(10);
		kfree(wdog_dev_data_g[wdog_id]->wdog_dev);
		kfree(wdog_dev_data_g[wdog_id]);
	}

	class_destroy(gul_wdog_dev_class);
	unregister_chrdev_region(wdog_dev_number, MAX_MODEM);

	return 0;
}
