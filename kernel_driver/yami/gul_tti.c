/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2019-2023 NXP
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
#include "gul_tti_ioctl.h"
#include "gul_base.h"

#define SIGNAL_TO_USERSPACE	1

/* TTI IRQ Handler */
static irqreturn_t tti_irq_handler(int irq, void *data)
{
	struct tti_priv *tti_priv_t = (struct tti_priv *)data;
	/* Send signal to User space */
	if (tti_priv_t) {
		raw_spin_lock(&tti_priv_t->wq_lock);
		swake_up_all_locked(&tti_priv_t->tti_wq);
		raw_spin_unlock(&tti_priv_t->wq_lock);
		if (tti_priv_t->evt_fd_ctxt) {
			eventfd_signal(tti_priv_t->evt_fd_ctxt,
				SIGNAL_TO_USERSPACE);
		}
	}
	return IRQ_HANDLED;
}

int tti_register_irq(struct tti_dev *tti_dev, struct tti *tti_t)
{
	int ret = 0;
	int tti_interrupt_flags = IRQF_NO_THREAD | IRQF_TRIGGER_RISING;
	struct tti_priv *tti_priv_t = NULL;
	struct task_struct *userspace_task = NULL;
	struct file *efd_file = NULL;

	tti_priv_t = &tti_dev->tti_priv_t[0];
	tti_priv_t->tti_id = tti_t->ttid;
	tti_priv_t->evt_fd_ctxt = NULL;

	if (tti_priv_t->irq != 0) {
		if (tti_priv_t->tti_irq_status == 0) {
		/* IRQ request */
			ret = request_irq(tti_priv_t->irq, tti_irq_handler,
					tti_interrupt_flags, "tti_handler", tti_priv_t);
			if (ret < 0) {
				pr_err("%s request irq err - %d\n",
							__func__, ret);
				goto err;
			} else {
				tti_priv_t->tti_irq_status = 1;
			}
		} else {
			pr_err("%s TTI IRQ %d is busy\n",
				__func__, tti_priv_t->irq);
		}
	} else {
		pr_err("%s IRQ is invalid\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	if (tti_t->tti_eventfd > 0) {
		userspace_task = current;
		rcu_read_lock();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
		efd_file = files_lookup_fd_rcu(userspace_task->files,
				tti_t->tti_eventfd);
#else
		efd_file = fcheck_files(userspace_task->files,
				tti_t->tti_eventfd);
#endif
		rcu_read_unlock();
		tti_priv_t->evt_fd_ctxt = eventfd_ctx_fileget(efd_file);
	}
err:
	return ret;
}

void tti_deregister_irq(struct tti_dev *tti_dev, struct tti *tti_t)
{
	struct tti_priv *tti_priv_t = &tti_dev->tti_priv_t[0];

	if (tti_priv_t != NULL) {
		/* IRQ request */
		free_irq(tti_priv_t->irq, tti_priv_t);
		tti_priv_t->tti_irq_status = 0;

		if (tti_priv_t->evt_fd_ctxt) {
			eventfd_ctx_put(tti_priv_t->evt_fd_ctxt);
			tti_priv_t->evt_fd_ctxt = NULL;
		}
	}
}

