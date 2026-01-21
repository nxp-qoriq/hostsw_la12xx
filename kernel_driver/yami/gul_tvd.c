/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2026 NXP
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
#include <linux/qoriq_thermal_interrupt.h>
#include <linux/eventfd.h>
#include "gul_base.h"
#include "gul_tvd_ioctl.h"

#define TVD_DEVICE_NAME_LEN	16
#define TVD_MIN_THRESHOLD	1
#define TVD_MAX_THRESHOLD	2
#define TVD_MIN_HYSTERESIS	1
#define TVD_MAX_HYSTERESIS	10
#define TVD_MSI_TIMEOUT_MSECS   2000

#define TVD_DEBUG		0
/* As per adjusted invalid temp value for int8_t */
#define INVALID_TEMP_COMB (0x88888888)

#define MAX_ATTEMPT 5
#define TMU_TRITSR0  0x1F80100
#define TMU_TRITSR_OFFSET_DIF 0x10
#define MAX_TEMP_MONITORING_SITE_ENABLED 3
#define TEMP_KELVIN_TO_CELSIUS(val)	(( val & 0x1FF ) - 273)

static dev_t tvd_dev_num;
static uint32_t tvd_dev_major;
static uint32_t tvd_dev_minor;
static struct class *gul_tvd_dev_class;
static struct gul_tvd_device_data *tvd_dev_data_g[MAX_MODEM];
struct tvd_dev *g_tvd_dev;

int mtd_get_temp_nowait(struct tvd_dev *tvd_dev, uint32_t tvdid,
		enum mtd_temp_sites site, int32_t *mtd_temp, bool single_site);

#define swap_data(data) \
	((((data) >> 16) & 0x0000FFFF) | (((data) << 16) & 0xFFFF0000))

struct tvd_priv_data {
	int msi_index;
	int irq;
	int msi_temp_index;
	int irq_temp;
	int irq_status_flag;
	int irq_temp_flag;
	int tvd_id;
	uint64_t tvd_count;
	int mtd_event_type;
	int mtd_cur_temp;
	int rtd_event_type;
	int rtd_cur_temp;
	int ctd_event_type;
	int ctd_cur_temp;
	int mtd_event_flag;
	int ctd_event_flag;
	int rtd_event_flag;
	int mtd_power_event_flag;
	int get_mtd_cur_temp;
	int get_ctd_cur_temp;
	int get_rtd_cur_temp;
	struct swait_queue_head tvd_wq;
	raw_spinlock_t tvd_wq_lock;
	struct swait_queue_head tvd_temp_wq;
	raw_spinlock_t tvd_temp_wq_lock;
	struct eventfd_ctx *evt_fd_ctxt;
};

struct tvd_dev {
	char name[TVD_DEVICE_NAME_LEN];
	struct tvd_priv_data tvd_priv_d[MAX_MODEM];
	struct gul_dev *gul_dev[MAX_MODEM];
};

/* TVD char Dev data holder */
struct gul_tvd_device_data {
	struct tvd_dev *tvd_dev;
	struct cdev cdev;
};

static irqreturn_t tvd_irq_handler(int irq, void *data)
{
#define SIGNAL_TO_CHANNEL_LISTENER	1
	struct tvd_priv_data *tvd_priv_d = (struct tvd_priv_data *)data;

	/* Send signal to Wake up the TVD wait queue */
	if (tvd_priv_d)  {
		tvd_priv_d->mtd_event_flag = 1;
		tvd_priv_d->rtd_event_flag = 1;

		raw_spin_lock(&tvd_priv_d->tvd_wq_lock);
		swake_up_all_locked(&tvd_priv_d->tvd_wq);
		raw_spin_unlock(&tvd_priv_d->tvd_wq_lock);

		if (tvd_priv_d->evt_fd_ctxt)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,19,0)
			eventfd_signal(tvd_priv_d->evt_fd_ctxt,
					SIGNAL_TO_CHANNEL_LISTENER);
#else
			eventfd_signal(tvd_priv_d->evt_fd_ctxt);
#endif
	}

	return IRQ_HANDLED;
}

static int tvd_register_irq(struct tvd_dev *tvd_dev,
				int tvd_id)
{
	int ret = 0;
	struct tvd_priv_data *tvd_priv_d = NULL;

	tvd_priv_d = &tvd_dev->tvd_priv_d[tvd_id];

	if (tvd_priv_d->irq != 0) {
		if (tvd_priv_d->irq_status_flag == 0) {
			ret = request_irq(tvd_priv_d->irq,
						tvd_irq_handler,
						IRQF_TRIGGER_RISING,
						"tvd_handler",
						tvd_priv_d);
			if (ret < 0) {
				pr_err("%s: request irq err - %d\n",
						 __func__, ret);
				goto err;
			} else {
				tvd_priv_d->irq_status_flag = 1;
			}

		} else {
			pr_err("%s TVD_MTD IRQ %d is busy\n",
						__func__, tvd_priv_d->irq);
		}

	} else {
		pr_err("%s: TVD_MTD IRQ is invalid\n", __func__);
		ret = -EINVAL;
		goto err;
	}
err:
	return ret;
}

static void tvd_deregister_irq(struct tvd_dev *tvd_dev,
				int tvd_id)
{
	struct tvd_priv_data *tvd_priv_d = &tvd_dev->tvd_priv_d[tvd_id];

	if (tvd_priv_d != NULL) {
		if (tvd_priv_d->irq_status_flag != 0) {
			tvd_priv_d->irq_status_flag = 0;
			free_irq(tvd_priv_d->irq, tvd_priv_d);
		}
	}
}

static int mtd_update_threshold(struct gul_dev *gul_dev,
					struct thermal_threshold *threshold_t)
{
	struct gul_hif *hif;
	int i = 0;

	hif = gul_dev->hif;

	i = threshold_t->mtd_threshold.threshold_count;
	if ((i > TVD_MAX_THRESHOLD) || (i < TVD_MIN_THRESHOLD)) {
		pr_err("\n%s: Thresholds count %d is not in range\n",
					__func__, i);
		return -EINVAL;
	}
	/* Write new thresholds to HIF */
	hif->mtd_threshold.threshold_count =
				threshold_t->mtd_threshold.threshold_count;
	for ( ; i > 0; i--) {
		hif->mtd_threshold.threshold[i-1] =
				threshold_t->mtd_threshold.threshold[i-1];
	}

#if (TVD_DEBUG != 0)
	pr_info("\n%s:threshold_cnt=%u\n", __func__,
		hif->mtd_threshold.threshold_count);
	for (i = 0; i < threshold_t->mtd_threshold.threshold_count; i++)
		pr_info("\tthreshold[%d]=%u\n", i,
				hif->mtd_threshold.threshold[i]);
#endif

	/* Raise MSI interrupt to Modem */
	raise_modem_msi(gul_dev, MSI_TYPE_A, HOST_MSI_TMU);

	return 0;
}

static int rtd_update_threshold(struct gul_dev *gul_dev,
					struct thermal_threshold *threshold_t)
{
	struct gul_hif *hif;
	int i = 0;

	hif = gul_dev->hif;

	dma_rmb();

	i = threshold_t->rtd_threshold.threshold_count;
	if ((i > TVD_MAX_THRESHOLD) || (i < TVD_MIN_THRESHOLD)) {
		pr_err("\n%s: Thresholds count %d is not in range\n",
					__func__, i);
		return -EINVAL;
	}

	/* Write new thresholds to HIF */
	hif->rtd_threshold.threshold_count =
				threshold_t->rtd_threshold.threshold_count;
	for ( ; i > 0; i--) {
		hif->rtd_threshold.threshold[i-1] =
				threshold_t->rtd_threshold.threshold[i-1];
	}

#if (TVD_DEBUG != 0)
	pr_info("\n%s:threshold_cnt=%u\n", __func__,
		hif->rtd_threshold.threshold_count);
	for (i = 0; i < threshold_t->rtd_threshold.threshold_count; i++)
		pr_info("\tthreshold[%d]=%u\n", i,
				hif->rtd_threshold.threshold[i]);
#endif

	return 0;
}
static irqreturn_t tvd_temp_irq_handler(int irq, void *data)
{
	struct tvd_priv_data *tvd_priv_d = (struct tvd_priv_data *)data;

	pr_debug("%s: TVD_MTD_TEMP MSI Interrupt Handler for irq %d is called\n",
				__func__, irq);

	/* Send signal to Wake up the TVD_TEMP wait queue */
	if (tvd_priv_d) {
		raw_spin_lock(&tvd_priv_d->tvd_temp_wq_lock);
		swake_up_all_locked(&tvd_priv_d->tvd_temp_wq);
		raw_spin_unlock(&tvd_priv_d->tvd_temp_wq_lock);
	}
	return IRQ_HANDLED;
}

static int mtd_get_site_temp(union mtdcurentTemp mtd_site_temp,
		enum mtd_temp_sites site, bool is_remote_temp)
{
	int ret = 0;

	if (!is_remote_temp)
		mtd_site_temp.temp = cpu_to_be32(mtd_site_temp.temp);

	switch (site) {
	case VSPA_TEMP:
		ret = TMU_ADJUST_TEMP_HOST_CTXT(mtd_site_temp.vspa_temp);
		break;
	case FECA_TEMP:
		ret = TMU_ADJUST_TEMP_HOST_CTXT(mtd_site_temp.feca_temp);
		break;
	case PCI_TEMP:
		ret = TMU_ADJUST_TEMP_HOST_CTXT(mtd_site_temp.pci_temp);
		break;
	case DIODE_TEMP:
		ret = TMU_ADJUST_TEMP_HOST_CTXT(mtd_site_temp.diode_temp);
		if (ret == MTD_TEMP_INVALID)
			pr_info("MTD I2C temp sensor not available/disabled");
		break;
	case COMBINED_TEMP:
		ret = mtd_site_temp.temp;
		break;
	default:
		ret = TMU_ADJUST_TEMP_HOST_CTXT(mtd_site_temp.vspa_temp);
		break;
	};
	return ret;
}


static int mtd_get_temp(struct tvd_dev *tvd_dev, uint32_t tvdid,
		enum mtd_temp_sites site, int32_t *pmtd_temp)
{
	int ret = 0;
	struct gul_dev *gul_dev;
	signed long time_remain;
	struct gul_hif *hif;
	struct tvd_priv_data *tvd_priv_d = NULL;
	unsigned long flags;
	DECLARE_SWAITQUEUE(tvd_wait);

	tvd_priv_d = &tvd_dev->tvd_priv_d[tvdid];
	gul_dev = tvd_dev->gul_dev[tvdid];
	hif = gul_dev->hif;

	/* Register MSI Interrupt for getting MTD Temp */
	if (tvd_priv_d->irq_temp != 0) {
		if (tvd_priv_d->irq_temp_flag == 0) {
			ret = request_irq(tvd_priv_d->irq_temp,
					tvd_temp_irq_handler,
					IRQF_TRIGGER_RISING,
					"mtd tvd_temp_handler",
					tvd_priv_d);
			if (ret < 0) {
				dev_err(gul_dev->dev, "request irq err - %d\n",
					ret);
				return ret;
			} else {
				tvd_priv_d->irq_temp_flag = 1;
			}

		} else {
			dev_dbg(gul_dev->dev, "TVD_MTD IRQ %d is busy\n",
					tvd_priv_d->irq_temp);
		}

	} else {
		dev_err(gul_dev->dev, "TVD_MTD_TEMP IRQ is invalid\n");
		return -EINVAL;
	}

	/* Write Get Temp request to HIF and Raise MSI to Modem */
	if (hif->tvd_mtdEvent != TVD_MTD_CURENT_TEMP_REQUESTED) {
		hif->tvd_mtdEvent = TVD_MTD_CURENT_TEMP_REQUESTED;
		raise_modem_msi(gul_dev, MSI_TYPE_A, HOST_MSI_TMU);
	}

	/* Waitqueue */
	raw_spin_lock_irqsave(
		&tvd_priv_d->tvd_temp_wq_lock,
		flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	prepare_to_swait_exclusive(&tvd_priv_d->tvd_temp_wq,
		&tvd_wait,
		TASK_INTERRUPTIBLE);
#else
	prepare_to_swait(&tvd_priv_d->tvd_temp_wq,
		&tvd_wait,
		TASK_INTERRUPTIBLE);
#endif
	raw_spin_unlock_irqrestore(
		&tvd_priv_d->tvd_temp_wq_lock,
		flags);
	/*Now wait here, TVD notificaion from IRQ handler or
	 * CTD Callback will wakeup
	 */
	time_remain = schedule_timeout(msecs_to_jiffies(TVD_MSI_TIMEOUT_MSECS));
	raw_spin_lock_irqsave(
		&tvd_priv_d->tvd_temp_wq_lock,
		flags);
	finish_swait(
		&tvd_priv_d->tvd_temp_wq,
		&tvd_wait);
	raw_spin_unlock_irqrestore(
		&tvd_priv_d->tvd_temp_wq_lock,
		flags);

	if (time_remain == 0) {
		dev_err(gul_dev->dev, "mtd get temp irq timeout\n");
		if (site == COMBINED_TEMP)
			*pmtd_temp = INVALID_TEMP_COMB;
		else
			*pmtd_temp = MTD_TEMP_INVALID;
		return -1;
	}

	tvd_priv_d->get_mtd_cur_temp = mtd_get_site_temp(hif->mtd_curentTemp,
							site, false);
	*pmtd_temp = tvd_priv_d->get_mtd_cur_temp;

	return 0;
}

int mtd_get_temp_nowait(struct tvd_dev *tvd_dev, uint32_t tvdid,
		enum mtd_temp_sites site, int32_t *mtd_temp, bool single_site)
{
	int retry = 0, valid = 0, index = 0, temp = 0, ret = 0;
	struct gul_dev *gul_dev = NULL;
	struct gul_hif *hif = NULL;
	struct tvd_priv_data *tvd_priv_d = NULL;
	struct gul_mem_region_info *ccsr_region = NULL;
	union mtdcurentTemp curentTemp = {.temp = INVALID_TEMP_COMB};

	tvd_priv_d = &tvd_dev->tvd_priv_d[tvdid];
	gul_dev = tvd_dev->gul_dev[tvdid];
	hif = gul_dev->hif;
	ccsr_region = &gul_dev->mem_regions[GUL_MEM_REGION_CCSR];

	if (!ccsr_region) {
		pr_err("ccsr_region is NULL !\n");
		if (site == COMBINED_TEMP)
			*mtd_temp = INVALID_TEMP_COMB;
		else
			*mtd_temp = MTD_TEMP_INVALID;
		return -1;
	}

	for (single_site ? index = site : index;
			index < MAX_TEMP_MONITORING_SITE_ENABLED; index++)
	{
		for (retry =0; retry < MAX_ATTEMPT; retry++)
		{
			valid = readl(ccsr_region->vaddr +
					(TMU_TRITSR0 +
					 (index * TMU_TRITSR_OFFSET_DIF))) ;
			if (!(valid & 0x80000000))
				/*waiting here to let the site come out of busy state & retry*/
				udelay(1);
			else
				break;
		}
		if (retry == MAX_ATTEMPT) {
			dev_err(gul_dev->dev, "Site %d is busy OR temp out of range !!!\n",
						index);
			temp = MTD_TEMP_INVALID;
			ret = -1;
		} else {
			temp = TEMP_KELVIN_TO_CELSIUS(valid);
		}
		switch (index) {
			case VSPA_TEMP:
				curentTemp.vspa_temp = TMU_ADJUST_TEMP_MDM_CTXT(temp);
				break;
			case FECA_TEMP:
				curentTemp.feca_temp = TMU_ADJUST_TEMP_MDM_CTXT(temp);
				break;
			case PCI_TEMP:
				curentTemp.pci_temp = TMU_ADJUST_TEMP_MDM_CTXT(temp);
				break;
		};

		if (single_site)
			break;
	}
	/*Reading diode temp not supported in fast mode so, set as MTD_TEMP_INVALID*/
	hif->mtd_curentTemp.temp = curentTemp.temp;
	tvd_priv_d->get_mtd_cur_temp = mtd_get_site_temp(hif->mtd_curentTemp,
								site, true);
	*mtd_temp = tvd_priv_d->get_mtd_cur_temp;
	if (ret < 0)
		return -1;
	return 0;
}

static int mtd_get_power_info(struct tvd_dev *tvd_dev, uint32_t tvdid,
		union mtdpowerInfo *pmtdpowerInfo)
{
	int ret = 0;
	struct gul_dev *gul_dev;
	signed long time_remain;
	struct gul_hif *hif;
	struct tvd_priv_data *tvd_priv_d = NULL;
	unsigned long flags;
	DECLARE_SWAITQUEUE(tvd_wait);

	tvd_priv_d = &tvd_dev->tvd_priv_d[tvdid];
	gul_dev = tvd_dev->gul_dev[tvdid];
	hif = gul_dev->hif;

	/* Register MSI Interrupt for getting MTD Temp */
	if (tvd_priv_d->irq_temp != 0) {
		if (tvd_priv_d->irq_temp_flag == 0) {
			ret = request_irq(tvd_priv_d->irq_temp,
					tvd_temp_irq_handler,
					IRQF_TRIGGER_RISING,
					"mtd power info irq_handler",
					tvd_priv_d);
			if (ret < 0) {
				dev_err(gul_dev->dev, "request irq err - %d\n",
						ret);
				goto err;
			} else {
				tvd_priv_d->irq_temp_flag = 1;
			}

		} else {
			dev_dbg(gul_dev->dev, "MTD_POWER_INFO IRQ %d is busy\n",
					tvd_priv_d->irq_temp);
		}

	} else {
		dev_err(gul_dev->dev, "MTD_POWER_INFO IRQ is invalid\n");
		ret = -EINVAL;
		goto err;
	}

	/* Write get power info request to HIF and Raise MSI to Modem */
	if (hif->tvd_mtdPowerEvent != TVD_MTD_POWER_INFO_REQUESTED) {
		hif->tvd_mtdPowerEvent = TVD_MTD_POWER_INFO_REQUESTED;
		raise_modem_msi(gul_dev, MSI_TYPE_A, HOST_MSI_MTD_POWER);
	}

	/* Waitqueue */
	raw_spin_lock_irqsave(
			&tvd_priv_d->tvd_temp_wq_lock,
			flags);
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	prepare_to_swait_exclusive(&tvd_priv_d->tvd_temp_wq,
			&tvd_wait,
			TASK_INTERRUPTIBLE);
#else
	prepare_to_swait(&tvd_priv_d->tvd_temp_wq,
			&tvd_wait,
			TASK_INTERRUPTIBLE);
#endif
	raw_spin_unlock_irqrestore(
			&tvd_priv_d->tvd_temp_wq_lock,
			flags);
	/*Now wait here, TVD notificaion from IRQ handler or
	 * CTD Callback will wakeup
	 */
	time_remain = schedule_timeout(msecs_to_jiffies(TVD_MSI_TIMEOUT_MSECS));
	raw_spin_lock_irqsave(
			&tvd_priv_d->tvd_temp_wq_lock,
			flags);
	finish_swait(
			&tvd_priv_d->tvd_temp_wq,
			&tvd_wait);
	raw_spin_unlock_irqrestore(
			&tvd_priv_d->tvd_temp_wq_lock,
			flags);

	if (time_remain == 0) {
		dev_err(gul_dev->dev, "mtd get power info irq timeout\n");
		ret = -EINVAL;
		pmtdpowerInfo->power_info = -EINVAL;
		goto err;
	}

	pmtdpowerInfo->power_info_fh =
		swap_data(hif->mtd_power_info.power_info_fh);
	pmtdpowerInfo->power_info_sh =
		swap_data(hif->mtd_power_info.power_info_sh);
	ret = 0;
#if (TVD_DEBUG != 0)
	pr_info("power_info_fh: %x sh: %x\n",
			pmtdpowerInfo->power_info_fh,
			pmtdpowerInfo->power_info_sh);
	pr_info(
	"Current_val: %xmA\nshunt_val: %xmV\nbus_volt: %xmV\nPower_val: %xmW\n",
			pmtdpowerInfo->current_val,
			pmtdpowerInfo->shunt_volt,
			pmtdpowerInfo->bus_volt,
			pmtdpowerInfo->power_val);
#endif

err:
	return ret;
}

static int rtd_get_temp(struct tvd_dev *tvd_dev, uint32_t tvdid)
{
	int ret = 0;
	struct gul_dev *gul_dev;
	signed long time_remain;
	struct gul_hif *hif;
	struct tvd_priv_data *tvd_priv_d = NULL;
	unsigned long flags;
	DECLARE_SWAITQUEUE(tvd_wait);

	tvd_priv_d = &tvd_dev->tvd_priv_d[tvdid];
	gul_dev = tvd_dev->gul_dev[tvdid];
	hif = gul_dev->hif;

	/* Register MSI Interrupt for getting RTD Temp */
	if (tvd_priv_d->irq_temp != 0) {
		if (tvd_priv_d->irq_temp_flag == 0) {
			ret = request_irq(tvd_priv_d->irq_temp,
					tvd_temp_irq_handler,
					IRQF_TRIGGER_RISING,
					"tvd_temp_handler",
					tvd_priv_d);
			if (ret < 0) {
				dev_err(gul_dev->dev, "rtd tvd request irq err - %d\n",
					ret);
				goto err;
			} else {
				tvd_priv_d->irq_temp_flag = 1;
			}

		} else {
			dev_dbg(gul_dev->dev, "TVD_RTD IRQ %d is busy\n",
					tvd_priv_d->irq_temp);
		}

	} else {
		dev_err(gul_dev->dev, "TVD_RTD_TEMP IRQ is invalid\n");
		ret = -EINVAL;
		goto err;
	}

	/* Write Get Temp request to HIF and Raise MSI to Modem */
	if (hif->tvd_rtdEvent != TVD_RTD_CURENT_TEMP_REQUESTED) {
		hif->tvd_rtdEvent = TVD_RTD_CURENT_TEMP_REQUESTED;
		raise_modem_msi(gul_dev, MSI_TYPE_A, HOST_MSI_RTD);
	}

	/* Waitqueue */
	raw_spin_lock_irqsave(
		&tvd_priv_d->tvd_temp_wq_lock,
		flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	prepare_to_swait_exclusive(&tvd_priv_d->tvd_temp_wq,
		&tvd_wait,
		TASK_INTERRUPTIBLE);
#else
	prepare_to_swait(&tvd_priv_d->tvd_temp_wq,
		&tvd_wait,
		TASK_INTERRUPTIBLE);
#endif
	raw_spin_unlock_irqrestore(
		&tvd_priv_d->tvd_temp_wq_lock,
		flags);
	/*Now wait here, TVD notificaion from IRQ handler or
	 * CTD Callback will wakeup
	 */
	time_remain = schedule_timeout(msecs_to_jiffies(TVD_MSI_TIMEOUT_MSECS));
	raw_spin_lock_irqsave(
		&tvd_priv_d->tvd_temp_wq_lock,
		flags);
	finish_swait(
		&tvd_priv_d->tvd_temp_wq,
		&tvd_wait);
	raw_spin_unlock_irqrestore(
		&tvd_priv_d->tvd_temp_wq_lock,
		flags);

	if (time_remain == 0) {
		dev_err(gul_dev->dev, "rtd get temp irq timeout\n");
		ret = MTD_TEMP_INVALID;
		goto err;
	}

	/* Write RTD temp value to userspace buffer */
	tvd_priv_d->get_rtd_cur_temp = hif->rtd_curentTemp;
	ret = tvd_priv_d->get_rtd_cur_temp;
err:
	return ret;
}
static int mtd_update_hysteresis(struct gul_dev *gul_dev,
					struct thermal_hysteresis *hysteresis_t)
{
	struct gul_hif *hif;
	int val = 0;

	hif = gul_dev->hif;

	val = hysteresis_t->mtd_hysteresis_val;
	if ((val > TVD_MAX_HYSTERESIS) || (val < TVD_MIN_HYSTERESIS)) {
		pr_err("\n%s: Hystersis value %d is not in range\n",
						__func__, val);
		return -EINVAL;
	}
	/* Write new hysteresis value to HIF */
	hif->mtd_hysteresisVal = hysteresis_t->mtd_hysteresis_val;

	/* Raise MSI interrupt to Modem */
	hif->tvd_mtdEvent = TVD_MTD_HYSTERESIS_UPDATE_REQUESTED;
	raise_modem_msi(gul_dev, MSI_TYPE_A, HOST_MSI_TMU);

#if (TVD_DEBUG != 0)
	dev_info(gul_dev->dev, "mtd_hysteresis_val=%d\n",
			hif->mtd_hysteresisVal);
#endif
	return 0;
}

static int ctd_update_hysteresis(struct tvd_dev *tvd_dev,
					struct thermal_hysteresis *hysteresis_t)
{
	int val = 0;
	int ret = 0;

	val = hysteresis_t->ctd_hysteresis_val;
	if ((val > TVD_MAX_HYSTERESIS) || (val < TVD_MIN_HYSTERESIS)) {
		pr_err("\n%s: Hysteresis value %d is not in range\n",
							__func__, val);
		return -EINVAL;
	}
	ret = ctd_program_hysteresis(val);
	if (ret < 0) {
		pr_err("%s failed\n", __func__);
		return ret;
	}

#if (TVD_DEBUG != 0)
	pr_info("%s: ctd_hysteresis_val=%d\n", __func__, val);
#endif
	return ret;
}

static int ctd_update_threshold(struct tvd_dev *tvd_dev,
					struct thermal_threshold *threshold_t)
{
	struct ctd_thermal_threshold ctd_threshold = {0};
	int i;
	int ret = 0;

	ctd_threshold.theshold_cnt = threshold_t->ctd_threshold.threshold_count;
	i = threshold_t->ctd_threshold.threshold_count;
	if ((i > TVD_MAX_THRESHOLD) || (i < TVD_MIN_THRESHOLD)) {
		pr_err("\n%s: Thresholds count %d is not in range\n",
						__func__, i);
		return -EINVAL;
	}
	for ( ; i > 0; i--) {
		ctd_threshold.threshold[i-1] =
				threshold_t->ctd_threshold.threshold[i-1];
	}

#if (TVD_DEBUG != 0)
	pr_info("%s:threshold_cnt=%u\n", __func__, ctd_threshold.theshold_cnt);
	for (i = 0; i < ctd_threshold.theshold_cnt; i++)
		pr_info("\tthreshold[%d]=%u\n", i, ctd_threshold.threshold[i]);
#endif
	ret = ctd_program_threshold(&ctd_threshold);
	if (ret < 0) {
		pr_err("%s failed\n", __func__);
		return ret;
	}
	return ret;
}

static void ctd_cbk(struct ctd_thermal_event *ctd_event)
{
	struct tvd_priv_data *tvd_priv_d = NULL;
	int tvd_id = 0;

	pr_debug("%s: CTD Thermal callback handler called\n", __func__);

	for (tvd_id = 0; tvd_id < MAX_MODEM; tvd_id++) {

		if (tvd_dev_data_g[tvd_id] == NULL)
			continue;

		tvd_priv_d =
			&tvd_dev_data_g[tvd_id]->tvd_dev->tvd_priv_d[tvd_id];

		/* Send signal to Wake up the TVD wait queue */
		if (tvd_priv_d) {
			tvd_priv_d->ctd_event_flag = 1;
			/* Fill CTD thermal events in TVD structure */
			tvd_priv_d->ctd_event_type = ctd_event->thermal_event_id;
			tvd_priv_d->ctd_cur_temp = ctd_event->temp;

			if (tvd_priv_d->evt_fd_ctxt)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,19,0)
				eventfd_signal(tvd_priv_d->evt_fd_ctxt,
						SIGNAL_TO_CHANNEL_LISTENER);
#else
				eventfd_signal(tvd_priv_d->evt_fd_ctxt);
#endif

			/* Wake up for CTD event */
			raw_spin_lock(&tvd_priv_d->tvd_wq_lock);
			swake_up_all_locked(&tvd_priv_d->tvd_wq);
			raw_spin_unlock(&tvd_priv_d->tvd_wq_lock);
		}
	}
}

static void get_thermal_event(struct tvd_dev *tvd_dev, struct tvd *tvd_t)
{
	unsigned long flags;
	DECLARE_SWAITQUEUE(tvd_wait);
	struct gul_dev *gul_dev;
	struct gul_hif *hif;
	int	ctd_flag = 0;
	int	mtd_flag = 0;
	int	rtd_flag = 0;
	int tvd_id = tvd_t->tvdid;
	struct tvd_priv_data *tvd_priv_d = NULL;

	tvd_priv_d = &tvd_dev->tvd_priv_d[tvd_id];

	gul_dev = tvd_dev->gul_dev[tvd_id];
	hif = gul_dev->hif;

	raw_spin_lock_irqsave(&tvd_priv_d->tvd_wq_lock, flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	prepare_to_swait_exclusive(&tvd_priv_d->tvd_wq,
		&tvd_wait,
		TASK_INTERRUPTIBLE);
#else
	prepare_to_swait(&tvd_priv_d->tvd_wq,
		&tvd_wait,
		TASK_INTERRUPTIBLE);
#endif
	raw_spin_unlock_irqrestore(&tvd_priv_d->tvd_wq_lock, flags);

	/*Now wait here, TVD notificaion from IRQ handler or
	 * CTD Callback will wakeup
	 */
	schedule();
	raw_spin_lock_irqsave(&tvd_priv_d->tvd_wq_lock, flags);
	finish_swait(&tvd_priv_d->tvd_wq, &tvd_wait);
	raw_spin_unlock_irqrestore(&tvd_priv_d->tvd_wq_lock,
		flags);

	if ((tvd_priv_d->ctd_event_flag == 1) &&
		(tvd_priv_d->mtd_event_flag == 1)) {
		put_user(BOTH_CTD_MTD_EVENT,
				&tvd_t->thermal_event_source.thermal_id);
	}

	if (tvd_priv_d->ctd_event_flag == 1) {
		/* Update CTD Thermal events to Userspace Buffer */
		tvd_priv_d->ctd_event_flag = 0;
		ctd_flag = 1;

		put_user(CTD_EVENT,
				&tvd_t->thermal_event_source.thermal_id);
		put_user(tvd_priv_d->ctd_event_type,
				&tvd_t->thermal_event_source.ctd.event_type);
		put_user(tvd_priv_d->ctd_cur_temp,
				&tvd_t->thermal_event_source.ctd.curr_temp);
	}

	if (tvd_priv_d->mtd_event_flag == 1) {
		/* Update MTD Thermal events */
		tvd_priv_d->mtd_event_flag = 0;
		mtd_flag = 1;

		tvd_priv_d->mtd_cur_temp =
			mtd_get_site_temp(hif->mtdThermalEvent.curTemp,
					tvd_t->mtd_site, false);
		tvd_priv_d->mtd_event_type =
			hif->mtdThermalEvent.tmuEvent;

		/* Copy MTD Thermal events to Userspace Buffer */
		put_user(MTD_EVENT,
				&tvd_t->thermal_event_source.thermal_id);
		put_user(tvd_priv_d->mtd_cur_temp,
				&tvd_t->thermal_event_source.mtd.curr_temp);
		put_user(tvd_priv_d->mtd_event_type,
				&tvd_t->thermal_event_source.mtd.event_type);
	}

	if (((tvd_priv_d->ctd_event_flag == 1) &&
		(tvd_priv_d->mtd_event_flag == 1))  ||
		(ctd_flag && mtd_flag)) {
		put_user(BOTH_CTD_MTD_EVENT,
			&tvd_t->thermal_event_source.thermal_id);
	}
	//HEMANT -fix it for multiple events
	if (tvd_priv_d->rtd_event_flag == 1) {
		/* Update RTD Thermal events */
		tvd_priv_d->rtd_event_flag = 0;
		rtd_flag = 1;

		tvd_priv_d->rtd_cur_temp =
					hif->rtdThermalEvent.curTemp;
		tvd_priv_d->rtd_event_type =
					hif->rtdThermalEvent.tmuEvent;

		/* Copy RTD Thermal events to Userspace Buffer */
		if (tvd_priv_d->rtd_event_type) {
			put_user(RTD_EVENT,
					&tvd_t->thermal_event_source.thermal_id);
			put_user(tvd_dev->tvd_priv_d->rtd_cur_temp,
					&tvd_t->thermal_event_source.rtd.curr_temp);
			put_user(tvd_dev->tvd_priv_d->rtd_event_type,
					&tvd_t->thermal_event_source.rtd.event_type);
			hif->rtdThermalEvent.tmuEvent = 0;
		}
	}
}

static int get_ctd_temp(void)
{
	int ret = 0, temp = 0;

	ret = ctd_get_temp_v2(&temp);
	if (ret == -EAGAIN) {
		ret = ctd_get_temp_v2(&temp);
		if (ret < 0) {
			pr_err("%s: Invalid ctd temp\n", __func__);
			return INVALID_CTD_TEMP;
		}
	} else if (ret & BIT(31)) {
		pr_err("%s: Invalid ctd temp\n", __func__);
		return INVALID_CTD_TEMP;
	}
	return temp;
}

ssize_t
tvd_device_dump(int id, char *buf)
{
	struct tvd_priv_data *tvd_priv_d;
	union mtdcurentTemp mtd_temp;
	union mtdpowerInfo mtd_power_info;
	int32_t mtdtemp = 0;
	if (tvd_dev_data_g[id] == NULL)
		return 0;

	tvd_priv_d = &g_tvd_dev->tvd_priv_d[id];
	sprintf(&buf[strlen(buf)],
		" TVD:gultvddev%d irq %d irq status=%d\n",
			id, tvd_priv_d->irq,
			tvd_priv_d->irq_status_flag);

	sprintf(&buf[strlen(buf)], " Host Temp: %d°C\n", get_ctd_temp());

	mtd_get_temp_nowait(g_tvd_dev, id, COMBINED_TEMP, &mtdtemp, false);
	mtd_temp.temp = mtdtemp;
	mtd_get_power_info(g_tvd_dev, id, &mtd_power_info);

	sprintf(&buf[strlen(buf)],
			" VSPA:%d°C FECA:%d°C PCI:%d°C\n",
			TMU_ADJUST_TEMP_HOST_CTXT(mtd_temp.vspa_temp),
			TMU_ADJUST_TEMP_HOST_CTXT(mtd_temp.feca_temp),
			TMU_ADJUST_TEMP_HOST_CTXT(mtd_temp.pci_temp));

	if ((TMU_ADJUST_TEMP_HOST_CTXT(mtd_temp.diode_temp)) != MTD_TEMP_INVALID)
		sprintf(&buf[strlen(buf)], " Diode:%d°C\n",
					TMU_ADJUST_TEMP_HOST_CTXT(mtd_temp.diode_temp));

	if ( (mtd_power_info.power_info != -EINVAL) &&
			(mtd_power_info.power_info != 0)) {
		sprintf(&buf[strlen(buf)],
		" Cur:%d.%dA SVolt:%d.%dV BVolt:%d.%dV Pwr:%d.%dW\n",
			(mtd_power_info.current_val / 1000),
			(mtd_power_info.current_val % 1000),
			(mtd_power_info.shunt_volt / 1000),
			(mtd_power_info.shunt_volt % 1000),
			(mtd_power_info.bus_volt / 1000),
			(mtd_power_info.bus_volt % 1000),
			(mtd_power_info.power_val / 1000),
			(mtd_power_info.power_val % 1000));
	}

/*	ret = rtd_get_temp(g_tvd_dev, id);
	if (ret != RTD_TEMP_INVALID)
		sprintf(&buf[strlen(buf)], " RF Card Temperature =%dC\n", ret);
	else
		sprintf(&buf[strlen(buf)], " RF Card Temperature Not Available(%d)\n", ret);
*/

	return 0;
}

static ssize_t gul_tvd_dev_read(struct file *filp, char __user *buf,
			size_t count, loff_t *offset)
{
	int rc = 0;
	unsigned long flags;
	DECLARE_SWAITQUEUE(tvd_wait);
	struct gul_tvd_device_data *tvd_dev_data = NULL;
	struct tvd *tvd_t = NULL;
	struct tvd_dev *tvd_dev;
	struct gul_dev *gul_dev;
	struct gul_hif *hif;
	int    ctd_flag = 0;
	int    mtd_flag = 0;
	int    rtd_flag = 0;
	int tvd_id = 0;
	struct tvd_priv_data *tvd_priv_d = NULL;

	tvd_dev_data = filp->private_data;

	tvd_dev = tvd_dev_data->tvd_dev;

	tvd_t = (struct tvd *)buf;

	tvd_id = tvd_t->tvdid;
	tvd_priv_d = &tvd_dev->tvd_priv_d[tvd_id];


	gul_dev = tvd_dev->gul_dev[tvd_id];
	hif = gul_dev->hif;

	raw_spin_lock_irqsave(
		&tvd_priv_d->tvd_wq_lock,
		flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	prepare_to_swait_exclusive(&tvd_priv_d->tvd_wq,
		&tvd_wait,
		TASK_INTERRUPTIBLE);
#else
	prepare_to_swait(&tvd_priv_d->tvd_wq,
		&tvd_wait,
		TASK_INTERRUPTIBLE);
#endif
	raw_spin_unlock_irqrestore(
		&tvd_priv_d->tvd_wq_lock,
		flags);

	/*Now wait here, TVD notificaion from IRQ handler or
	 * CTD Callback will wakeup
	 */
	schedule();
	raw_spin_lock_irqsave(&tvd_priv_d->tvd_wq_lock,
		flags);
	finish_swait(&tvd_priv_d->tvd_wq,
		&tvd_wait);
	raw_spin_unlock_irqrestore(&tvd_priv_d->tvd_wq_lock,
		flags);

	if ((tvd_priv_d->ctd_event_flag == 1) &&
		(tvd_priv_d->mtd_event_flag == 1)) {
		rc = put_user(BOTH_CTD_MTD_EVENT,
				&tvd_t->thermal_event_source.thermal_id);
	}

	if (tvd_priv_d->ctd_event_flag == 1) {
		/* Update CTD Thermal events to Userspace Buffer */
		tvd_dev_data->tvd_dev->tvd_priv_d->ctd_event_flag = 0;
		ctd_flag = 1;

		rc = put_user(CTD_EVENT,
				&tvd_t->thermal_event_source.thermal_id);
		rc = put_user(tvd_priv_d->ctd_event_type,
				&tvd_t->thermal_event_source.ctd.event_type);
		rc = put_user(tvd_priv_d->ctd_cur_temp,
				&tvd_t->thermal_event_source.ctd.curr_temp);
	}

	if (tvd_priv_d->mtd_event_flag == 1) {
		/* Update MTD Thermal events */
		tvd_priv_d->mtd_event_flag = 0;
		mtd_flag = 1;

		tvd_priv_d->mtd_cur_temp = mtd_get_site_temp(
						hif->mtdThermalEvent.curTemp,
						tvd_t->mtd_site, false);
		tvd_priv_d->mtd_event_type = hif->mtdThermalEvent.tmuEvent;

		/* Copy MTD Thermal events to Userspace Buffer */
		rc = put_user(MTD_EVENT,
				&tvd_t->thermal_event_source.thermal_id);
		rc = put_user(tvd_priv_d->mtd_cur_temp,
				&tvd_t->thermal_event_source.mtd.curr_temp);
		rc = put_user(tvd_priv_d->mtd_event_type,
				&tvd_t->thermal_event_source.mtd.event_type);
	}

	if (((tvd_priv_d->ctd_event_flag == 1) &&
		(tvd_priv_d->mtd_event_flag == 1))  ||
		(ctd_flag && mtd_flag)) {
		rc = put_user(BOTH_CTD_MTD_EVENT,
				&tvd_t->thermal_event_source.thermal_id);
	}

	if (tvd_priv_d->rtd_event_flag == 1) {
		/* Update RTD Thermal events */
		tvd_priv_d->rtd_event_flag = 0;
		rtd_flag = 1;

		tvd_priv_d->rtd_cur_temp =
					hif->rtdThermalEvent.curTemp;
		tvd_priv_d->rtd_event_type =
					hif->rtdThermalEvent.tmuEvent;

		/* Copy RTD Thermal events to Userspace Buffer */
		if (tvd_priv_d->rtd_event_type) {

			rc = put_user(RTD_EVENT,
					&tvd_t->thermal_event_source.thermal_id);
			rc = put_user(tvd_dev_data->tvd_dev->tvd_priv_d->rtd_cur_temp,
					&tvd_t->thermal_event_source.rtd.curr_temp);
			rc = put_user(tvd_dev_data->tvd_dev->tvd_priv_d->rtd_event_type,
					&tvd_t->thermal_event_source.rtd.event_type);
			hif->rtdThermalEvent.tmuEvent = 0;
		}

	}

	if (!rc)
		rc = sizeof(tvd_priv_d->tvd_count);

	return rc;
}

static int gul_tvd_dev_open(struct inode *inode, struct file *filp)
{
	struct gul_tvd_device_data *tvd_dev = NULL;

	tvd_dev = container_of(inode->i_cdev,
				struct gul_tvd_device_data,
				cdev);
	filp->private_data = tvd_dev;

	return 0;
}

static int gul_tvd_dev_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static long gul_tvd_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	int ret = 0;
	struct tvd tvd_t = {0};
	struct tvd *tvd_ptr = &tvd_t;
	struct gul_tvd_device_data *tvd_dev_data = NULL;
	struct tvd_dev *tvd_dev = NULL;
	struct task_struct *userspace_task = NULL;
	struct file *efd_file = NULL;
	struct tvd_priv_data *tvd_priv_d = NULL;
	union mtdpowerInfo mtd_power;
	int32_t mtd_temp = 0;
	tvd_dev_data = (struct gul_tvd_device_data *)filp->private_data;
	tvd_dev = tvd_dev_data->tvd_dev;

	switch (cmd) {
	case IOCTL_GUL_TVD_REGISTER_EVEFD:
		ret = copy_from_user(&tvd_t,
					(struct tvd *)arg,
					sizeof(struct tvd));

		if ((ret != 0) || (tvd_t.tvdid >= MAX_MODEM))
			return -EFAULT;

		tvd_priv_d = &tvd_dev->tvd_priv_d[tvd_t.tvdid];

		tvd_priv_d->evt_fd_ctxt = NULL;
		if (tvd_t.tvd_eventfd > 0) {
			/* Get current task context from which IOCTL was called */
			userspace_task = current;

			rcu_read_lock();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,0,0)
			efd_file = files_lookup_fd_raw(userspace_task->files,
                                        tvd_t.tvd_eventfd);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
			efd_file = files_lookup_fd_rcu(userspace_task->files,
					tvd_t.tvd_eventfd);
#else
			efd_file = fcheck_files(userspace_task->files,
					tvd_t.tvd_eventfd);
#endif
			rcu_read_unlock();

			tvd_priv_d->evt_fd_ctxt = eventfd_ctx_fileget(efd_file);
		}
		break;

	case IOCTL_GUL_TVD_DEREGISTER_EVEFD:
		ret = copy_from_user(&tvd_t,
					(struct tvd *)arg,
					sizeof(struct tvd));
		if (ret != 0)
			return -EFAULT;

		tvd_priv_d = &tvd_dev->tvd_priv_d[tvd_t.tvdid];

		if (tvd_priv_d->evt_fd_ctxt) {
			eventfd_ctx_put(tvd_priv_d->evt_fd_ctxt);
			tvd_priv_d->evt_fd_ctxt = NULL;
		}
		break;

	case IOCTL_GUL_TVD_GET_THERMAL_EVENT:
		ret = copy_from_user(&tvd_t,
				(struct tvd *)arg,
				sizeof(struct tvd));
		if (ret != 0)
			return -EFAULT;

		tvd_ptr = (struct tvd *)arg;
		get_thermal_event(tvd_dev, tvd_ptr);
		break;

	case IOCTL_GUL_TVD_PROGRAM_MTD_THRESHOLD:
	ret = copy_from_user(&tvd_t,
			(struct tvd *)arg,
			sizeof(struct tvd));
		if (ret != 0)
			return -EFAULT;

		tvd_ptr = (struct tvd *)arg;
		ret = mtd_update_threshold(tvd_dev->gul_dev[tvd_ptr->tvdid],
					&tvd_ptr->thermal_threshold_source);
		if (ret < 0)
			return ret;
		break;

	case IOCTL_GUL_TVD_PROGRAM_CTD_THRESHOLD:
		ret = copy_from_user(&tvd_t,
					(struct tvd *)arg,
					sizeof(struct tvd));
		if (ret != 0)
			return -EFAULT;

		tvd_ptr = (struct tvd *)arg;
		ret  = ctd_update_threshold(tvd_dev,
					    &tvd_ptr->thermal_threshold_source);
		if (ret < 0)
			return ret;
		break;

	case IOCTL_GUL_TVD_PROGRAM_RTD_THRESHOLD:
		ret = copy_from_user(&tvd_t,
					(struct tvd *)arg,
					sizeof(struct tvd));
		if (ret != 0)
			return -EFAULT;
		ret = rtd_update_threshold(tvd_dev->gul_dev[tvd_ptr->tvdid],
					&tvd_ptr->thermal_threshold_source);
		if (ret < 0)
			return ret;
		break;

	case IOCTL_GUL_TVD_MTD_GET_TEMP:
		ret = copy_from_user(&tvd_t, (struct tvd *)arg,
					sizeof(struct tvd));
		if (ret != 0)
			return -EFAULT;

		tvd_ptr = (struct tvd *)arg;
		mtd_get_temp(tvd_dev, tvd_ptr->tvdid, tvd_ptr->mtd_site, &mtd_temp);
		/* Write MTD temp value to userspace buffer */
		put_user(mtd_temp, &tvd_ptr->get_mtd_curr_temp);
		break;

	case IOCTL_GUL_TVD_MTD_GET_POWER_INFO:
		ret = copy_from_user(&tvd_t, (struct tvd *)arg,
					sizeof(struct tvd));
		if (ret != 0)
			return -EFAULT;

		tvd_ptr = (struct tvd *)arg;
		ret = mtd_get_power_info(tvd_dev, tvd_ptr->tvdid, &mtd_power);
		put_user(mtd_power.power_info, &tvd_ptr->get_mtd_power_info);
		break;
	case IOCTL_GUL_TVD_CTD_GET_TEMP:
		ret = copy_from_user(&tvd_t,
					(struct tvd *)arg,
					sizeof(struct tvd));
		if (ret != 0)
			return -EFAULT;

		tvd_ptr = (struct tvd *)arg;
		tvd_dev_data->tvd_dev->tvd_priv_d[tvd_ptr->tvdid].get_ctd_cur_temp =
							get_ctd_temp();

		/* Write CTD temp value to userspace buffer */
		put_user(tvd_dev_data->tvd_dev->tvd_priv_d[tvd_ptr->tvdid].get_ctd_cur_temp,
					&tvd_ptr->get_ctd_curr_temp);
		break;

	case IOCTL_GUL_TVD_RTD_GET_TEMP:
		ret = copy_from_user(&tvd_t,
					(struct tvd *)arg,
					sizeof(struct tvd));
		tvd_ptr = (struct tvd *)arg;
		tvd_dev_data->tvd_dev->tvd_priv_d[tvd_ptr->tvdid].get_rtd_cur_temp =
					rtd_get_temp(tvd_dev, tvd_ptr->tvdid);
		/* Write RTD temp value to userspace buffer */
		put_user(tvd_dev_data->tvd_dev->tvd_priv_d[tvd_ptr->tvdid].get_rtd_cur_temp,
					&tvd_ptr->get_rtd_curr_temp);
		if (ret < 0)
			return ret;
		break;

	case IOCTL_GUL_TVD_PROGRAM_MTD_HYSTERESIS:
		ret = copy_from_user(&tvd_t,
					(struct tvd *)arg,
					sizeof(struct tvd));
		if (ret != 0)
			return -EFAULT;
		tvd_ptr = (struct tvd *)arg;
		ret = mtd_update_hysteresis(tvd_dev->gul_dev[tvd_ptr->tvdid],
					&tvd_ptr->hysteresis);
		if (ret < 0)
			return ret;
		break;

	case IOCTL_GUL_TVD_PROGRAM_CTD_HYSTERESIS:
		ret = copy_from_user(&tvd_t,
					(struct tvd *)arg,
					sizeof(struct tvd));
		if (ret != 0)
			return -EFAULT;
		tvd_ptr = (struct tvd *)arg;
		ret  = ctd_update_hysteresis(tvd_dev, &tvd_ptr->hysteresis);
		if (ret < 0)
			return ret;
		break;

	default:
		ret = -ENOTTY;
	}
	return ret;
}

/* TVD file_operations */
static const struct file_operations gul_tvd_dev_fops = {
	.owner      = THIS_MODULE,
	.open       = gul_tvd_dev_open,
	.release    = gul_tvd_dev_release,
	.read       = gul_tvd_dev_read,
	.unlocked_ioctl = gul_tvd_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gul_tvd_dev_ioctl,
#endif
};

static int create_tvd_cdevs(struct gul_tvd_device_data **tvd_dev_data, int id)
{
	int ret = 0;

	cdev_init(&tvd_dev_data[id]->cdev, &gul_tvd_dev_fops);
	tvd_dev_data[id]->cdev.ops = &gul_tvd_dev_fops;
	tvd_dev_data[id]->cdev.owner = THIS_MODULE;

	/* Adding a device to the system */
	cdev_add(&tvd_dev_data[id]->cdev, MKDEV(tvd_dev_major, id), 1);
	if ((device_create(gul_tvd_dev_class,
				NULL,
				MKDEV(tvd_dev_major, id),
				NULL,
				"gultvddev%d", id)) == NULL) {
		pr_err("%s: Cannot create the tvd device(%d)\n", __func__, id);
		ret = -1;
	}
	return ret;
}

int tvd_probe(struct gul_dev *gul_dev, int virq_count,
				struct virq_evt_map *virq_map)
{
	struct tvd_dev *tvd_dev = g_tvd_dev;
	struct gul_tvd_device_data *tvd_dev_data[MAX_MODEM];
	struct gul_hif *hif;
	struct thermal_threshold threshold_t;
	struct thermal_hysteresis hysteresis_t;
	int i;
	int ret = 0;

	dev_dbg(gul_dev->dev, "In probe function\n");

	i = gul_dev->id;
	tvd_dev->gul_dev[i] = gul_dev;
	hif = gul_dev->hif;

	hif->tvd_mtdEvent = 0;
	hif->tvd_rtdEvent = 0;
	hif->tvd_mtdPowerEvent = 0;
	tvd_dev->tvd_priv_d[i].msi_index = gul_dev_get_msi(gul_dev);
	tvd_dev->tvd_priv_d[i].irq =
		gul_dev->irq[tvd_dev->tvd_priv_d[i].msi_index].irq_val;

	tvd_dev->tvd_priv_d[i].msi_temp_index = gul_dev_get_msi(gul_dev);
	tvd_dev->tvd_priv_d[i].irq_temp =
		gul_dev->irq[tvd_dev->tvd_priv_d[i].msi_temp_index].irq_val;

	tvd_dev->tvd_priv_d[i].irq_status_flag = 0;
	tvd_dev->tvd_priv_d[i].irq_temp_flag = 0;
	tvd_dev->tvd_priv_d[i].mtd_event_flag = 0;
	tvd_dev->tvd_priv_d[i].ctd_event_flag = 0;
	tvd_dev->tvd_priv_d[0].rtd_event_flag = 0;
	tvd_dev->tvd_priv_d[i].mtd_power_event_flag = 0;

	/* Write MSI to HIF so that Modem TMU driver use it for
	 * rising interrupt from Modem to Host
	 */
	hif->msi_mtd_tvd = tvd_dev->tvd_priv_d[i].msi_index;
	hif->msi_mtd_tvd_curentTemp = tvd_dev->tvd_priv_d[i].msi_temp_index;
	hif->msi_rtd_tvd_curentTemp = tvd_dev->tvd_priv_d[i].msi_temp_index;
	hif->msi_mtd_power_info = tvd_dev->tvd_priv_d[i].msi_temp_index;
	tvd_dev_data[i] = kmalloc(sizeof(struct gul_tvd_device_data),
					GFP_KERNEL);
	if (tvd_dev_data[i] == NULL)
		return -ENOMEM;

	tvd_dev->tvd_priv_d[i].tvd_id = i;
	tvd_dev_data[i]->tvd_dev = tvd_dev;
	tvd_dev_data_g[i] = tvd_dev_data[i];

	ret = create_tvd_cdevs(tvd_dev_data, i);
	if (ret < 0) {
		dev_err(gul_dev->dev, "TVD Failed to create chardevs");
		goto err;
	}

	gul_dev->tvd_priv = tvd_dev;

	/*simple wait queue init*/
	init_swait_queue_head(&tvd_dev->tvd_priv_d[i].tvd_wq);
	init_swait_queue_head(&tvd_dev->tvd_priv_d[i].tvd_temp_wq);

	/*raw spinlock init for TVD module*/
	raw_spin_lock_init(&tvd_dev->tvd_priv_d[i].tvd_wq_lock);
	raw_spin_lock_init(&tvd_dev->tvd_priv_d[i].tvd_temp_wq_lock);

	/* MTD Event IRQ request */
	ret = tvd_register_irq(tvd_dev, tvd_dev->tvd_priv_d[i].tvd_id);
	if (ret < 0)
		goto out;

	/* Apply Default MTD thresholds */
	threshold_t.mtd_threshold.threshold_count = DEFAULT_MAX_MTD_THRESHOLD;
	if (threshold_t.mtd_threshold.threshold_count == 1) {
		threshold_t.mtd_threshold.threshold[0] =
					DEFAULT_MTD_THRESHOLD_1;

	} else {
		threshold_t.mtd_threshold.threshold[0] =
					DEFAULT_MTD_THRESHOLD_1;
		threshold_t.mtd_threshold.threshold[1] =
					DEFAULT_MTD_THRESHOLD_2;
	}
	ret = mtd_update_threshold(gul_dev, &threshold_t);
	if (ret < 0)
		goto out;

	/* Apply Default MTD hysteresis */
	hysteresis_t.mtd_hysteresis_val = DEFAULT_MTD_HYSTERESIS;
	ret = mtd_update_hysteresis(gul_dev, &hysteresis_t);
	if (ret < 0)
		goto out;
	return 0;
out:
	gul_dev_put_msi(gul_dev, tvd_dev->tvd_priv_d[i].msi_index);
	gul_dev_put_msi(gul_dev, tvd_dev->tvd_priv_d[i].msi_temp_index);

	tvd_deregister_irq(tvd_dev, i);
	if (tvd_dev->tvd_priv_d[i].irq_temp_flag != 0)
		free_irq(tvd_dev->tvd_priv_d[i].irq_temp, (void *)(&tvd_dev->tvd_priv_d[i]));

	cdev_del(&tvd_dev_data[i]->cdev);
	device_destroy(gul_tvd_dev_class, MKDEV(tvd_dev_major, i));
err:
	kfree(tvd_dev_data[i]);
	tvd_dev_data_g[i] = NULL;
	gul_dev->tvd_priv = NULL;

	return ret;
}

int tvd_remove(struct gul_dev *gul_dev)
{
	struct tvd_dev *tvd_dev;
	struct tvd_priv_data *tvd_priv_d;
	int tvd_id = gul_dev->id;

	tvd_dev = gul_dev->tvd_priv;

	if (!tvd_dev)
		return 0;

	tvd_priv_d = &tvd_dev->tvd_priv_d[tvd_id];
	tvd_deregister_irq(tvd_dev, tvd_id);
	if (tvd_priv_d->irq_temp_flag != 0)
		free_irq(tvd_priv_d->irq_temp, tvd_priv_d);
	gul_dev_put_msi(gul_dev, tvd_priv_d->msi_index);
	gul_dev_put_msi(gul_dev, tvd_priv_d->msi_temp_index);
	device_destroy(gul_tvd_dev_class, MKDEV(tvd_dev_major, tvd_id));
	cdev_del(&tvd_dev_data_g[tvd_id]->cdev);
	kfree(tvd_dev_data_g[tvd_id]);
	tvd_dev_data_g[tvd_id] = NULL;

	gul_dev->tvd_priv = NULL;
	return 0;
}

int tvd_init(void)
{
	int ret = -1;
	struct tvd_dev *tvd_dev = NULL;
	struct thermal_threshold threshold_t;
	struct thermal_hysteresis hysteresis_t;

	pr_info("%s:TVD init called\n", __func__);

	/*Allocating chardev region and assigning Major number */
	ret = alloc_chrdev_region(&tvd_dev_num,
					tvd_dev_minor,
					MAX_MODEM,
					"gultvddev");
	if (ret < 0) {
		pr_err("%s: Failed in getting major number\n",
				 __func__);
		return ret;
	}

	/* Device Major number */
	tvd_dev_major = MAJOR(tvd_dev_num);

	tvd_dev = kmalloc(sizeof(struct tvd_dev), GFP_KERNEL);
	if (!tvd_dev) {
		pr_err("%s:Memory allocation failure for tvd_dev\n", __func__);
		goto err;
	}
	memset(tvd_dev, 0, sizeof(struct tvd_dev));

	/* sysfs class creation */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	gul_tvd_dev_class = class_create("gultvddev");
#else
	gul_tvd_dev_class = class_create(THIS_MODULE, "gultvddev");
#endif
	if (gul_tvd_dev_class == NULL) {
		pr_err("%s:Cannot allocate major number\n", __func__);
		ret = -1;
		goto out;
	}

	/* CTD callback register */
	ret = ctd_register(ctd_cbk);
	if (ret < 0) {
		pr_info("%s: ctd_register failed\n", __func__);
		goto out_class;
	}

	/* Apply Default CTD thresholds */
	threshold_t.threshold_type = CTD_THERMAL_THRESHOLD;
	threshold_t.ctd_threshold.threshold_count = DEFAULT_MAX_CTD_THRESHOLD;
	if (threshold_t.ctd_threshold.threshold_count == 1) {
		threshold_t.ctd_threshold.threshold[0] =
			DEFAULT_CTD_THRESHOLD_1;

	} else {
		threshold_t.ctd_threshold.threshold[0] =
			DEFAULT_CTD_THRESHOLD_1;
		threshold_t.ctd_threshold.threshold[1] =
			DEFAULT_CTD_THRESHOLD_2;
	}

	/* Apply Default CTD hysteresis */
	hysteresis_t.hysteresis_type = CTD_THERMAL_HYSTERESIS;
	hysteresis_t.ctd_hysteresis_val = DEFAULT_CTD_HYSTERESIS;
	ret = ctd_update_hysteresis(tvd_dev, &hysteresis_t);
	if (ret < 0)
		goto out_class;

	ret = ctd_update_threshold(tvd_dev, &threshold_t);
	if (ret < 0)
		goto out_class;

	g_tvd_dev = tvd_dev;
	return ret;
out_class:
	ctd_deregister();
out:
	class_destroy(gul_tvd_dev_class);
	kfree(tvd_dev);
err:
	unregister_chrdev_region(tvd_dev_num, MAX_MODEM);

	return ret;
}
EXPORT_SYMBOL_GPL(tvd_init);

int tvd_exit(void)
{

	ctd_deregister();
	class_destroy(gul_tvd_dev_class);
	kfree(g_tvd_dev);
	unregister_chrdev_region(tvd_dev_num, MAX_MODEM);

	return 0;
}
EXPORT_SYMBOL_GPL(tvd_exit);

