/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2017-2022 NXP
 */


#include <linux/irq.h>
#include <linux/interrupt.h>
#include "gul_base.h"

/* IRQ event regitster masks */
#define IRQ_EVT_EN_OFFSET	0x1
#define NIRQ_WORDS_MASK		0xff
#define NIRQ_MASK		0xff00

/* IRQ event clear message unit interrupt bit num */
#define GUL_RAISE_CLR_EVT	0x0

#define gul_mem_r(addr) readl(addr)
#define gul_mem_w(val, addr) writel(val, addr)

int gul_init_irq_mux(struct irq_evt_regs *irq_evt_regs,
		     struct gul_irq_mux_pram *irq_mux)
{
	u32 num_irq_words = gul_mem_r(&irq_evt_regs->irq_evt_cfg) &
		NIRQ_WORDS_MASK;

	irq_mux->irq_evt_cfg_reg = &irq_evt_regs->irq_evt_cfg;
	irq_mux->irq_evt_en_reg = irq_mux->irq_evt_cfg_reg + IRQ_EVT_EN_OFFSET;
	irq_mux->irq_evt_sts_reg = irq_mux->irq_evt_en_reg + num_irq_words;
	irq_mux->irq_evt_clr_reg = irq_mux->irq_evt_sts_reg + num_irq_words;
	irq_mux->irq_base = irq_alloc_descs(-1, 0, irq_mux->num_irq, 0);

	return irq_mux->irq_base;
}

/*XXX:TBD: Sysfs is not yet supported*/
#if 0
static ssize_t gul_show_irq_mux_stats(void *irq_mux_stats, char *buf,
				      void *dev)
{
	int print_len;
	struct gul_dev *gul_dev = (struct gul_dev *)dev;
	struct gul_irq_mux_stats *irq_stats =
		(struct gul_irq_mux_stats *)irq_mux_stats;

	if ((gul_dev->stats_desc.stats_control &
		(1 << HOST_CONTROL_IRQ_STATS)) == 0)
		return 0;

	print_len = sprintf(buf, "Host VIRQ stats\n");
	print_len += sprintf((buf + print_len), "num_virq_evt_raised: %7lu\n",
			    irq_stats->num_virq_evt_raised);
	print_len += sprintf((buf + print_len), "num_hw_irq_recv: %7lu\n",
			     irq_stats->num_hw_irq_recv);
	print_len += sprintf((buf + print_len),
			     "num_msg_unit_irq_evt_raised: %7lu\n",
			     irq_stats->num_msg_unit_irq_evt_raised);

	return print_len;
}

static void gul_reset_irq_mux_stats(void *irq_mux_stats)
{
	struct gul_irq_mux_stats *irq_stats =
		(struct gul_irq_mux_stats *)irq_mux_stats;

	irq_stats->num_virq_evt_raised = 0;
	irq_stats->num_hw_irq_recv = 0;
	irq_stats->num_msg_unit_irq_evt_raised = 0;
}
#endif

/* -------------------- IRQ chip functions -------------------- */
void gul_set_imask(struct gul_irq_mux_pram *irq_mux, int irq, u32 set_flg)
{
	int irq_bit = irq - irq_mux->irq_base;
	unsigned long irq_evt_en_reg;

	irq_evt_en_reg = gul_mem_r(irq_mux->irq_evt_en_reg + (irq_bit /
					GUL_EVT_BTS_PER_WRD));
	if (set_flg)
		set_bit((irq_bit % GUL_EVT_BTS_PER_WRD),
			(void *)&irq_evt_en_reg);
	else
		clear_bit((irq_bit %
			   GUL_EVT_BTS_PER_WRD), (void *)&irq_evt_en_reg);
	gul_mem_w(irq_evt_en_reg, irq_mux->irq_evt_en_reg + (irq_bit /
					GUL_EVT_BTS_PER_WRD));
}

static void gul_irq_mask(struct irq_data *d)
{
	struct gul_irq_mux_pram *irq_mux = irq_data_get_irq_chip_data(d);

	gul_set_imask(irq_mux, d->irq, 0);

}

static void gul_irq_unmask(struct irq_data *d)
{
	struct gul_irq_mux_pram *irq_mux = irq_data_get_irq_chip_data(d);

	gul_set_imask(irq_mux, d->irq, 1);
}

static struct irq_chip gul_irq_chip = {
	.name			= "gul_irq_mux",
	.irq_mask		= gul_irq_mask,
	.irq_unmask		= gul_irq_unmask,
};

void gul_set_irq_chip(struct gul_irq_mux_pram *irq_mux)
{
	int i;
	int irq;

	for (i = 0; i < irq_mux->num_irq; i++) {
		unsigned long clr = 0, set = IRQ_NOREQUEST |
			IRQ_NOPROBE | IRQ_NOAUTOEN;
		irq = irq_mux->irq_base + i;
		irq_set_chip_data(irq, (void *)irq_mux);
		/* Setup irq  */
		irq_set_chip_and_handler(irq, &gul_irq_chip,
				handle_simple_irq);
		irq_clear_status_flags(irq, IRQ_NOREQUEST);
		//set_irq_flags(irq, IRQF_VALID); /* Deprecated */
		clr |= IRQ_NOREQUEST;
		irq_modify_status(irq, clr, set & ~clr);
	}
}

#if 0
static void create_virq_bit_map(struct gul_dev *gul_dev,
			 struct gul_irq_mux_pram *irq_mux)
{
	u32 num_irq = irq_mux->num_irq;
	struct virq_evt_map *virq_map = irq_mux->virq_map;
	int i;

	for (i = 0; i < IRQ_EVT_LAST_BIT; i++) {
		if (num_irq) {
			virq_map  = (irq_mux->virq_map + i);
			virq_map->evt = i;
			virq_map->virq = irq_mux->irq_base + i;
			dev_dbg(gul_dev->dev,
				"%s: virq_map: %px, evt: %d, virq: %d\n",
				__func__, virq_map, virq_map->evt,
				virq_map->virq);
		} else {
			dev_warn(gul_dev->dev, "Num irq insufficient");
			break;
		}
		num_irq -= 1;
	}
}

/*XXX:TBD: Syfs is not yet supported */
static int gul_init_irq_stats(struct gul_dev *gul_dev,
			struct gul_irq_mux_pram *irq_mux)
{
	struct gul_stats_ops stats_ops;

	stats_ops.gul_show_stats = gul_show_irq_mux_stats;
	stats_ops.gul_reset_stats = gul_reset_irq_mux_stats;
	stats_ops.stats_args = (void *)&gul_dev->gul_irq_priv->irq_stats;
	return gul_host_add_stats(gul_dev, &stats_ops);
}
static int gul_probe_irq_mux(struct gul_dev *gul_dev,
		struct irq_evt_regs *irq_evt_regs)
{
	struct gul_irq_mux_pram *irq_mux;
	u32 num_irq = (gul_mem_r(&irq_evt_regs->irq_evt_cfg) & NIRQ_MASK) >> 8;
	int ret = 0;

	irq_mux = kzalloc(sizeof(struct gul_irq_mux_pram), GFP_KERNEL);
	if (irq_mux == NULL) {
		dev_err(gul_dev->dev, "%s: memory alloc fail!\n", __func__);
		return -ENOMEM;
	}
	irq_mux->virq_map = kzalloc((sizeof(struct virq_evt_map) * num_irq),
				    GFP_KERNEL);
	if (irq_mux->virq_map == NULL) {
		ret = -ENOMEM;
		dev_err(gul_dev->dev, "%s: memory alloc fail!\n", __func__);
		goto err1;
	}
	gul_dev->gul_irq_priv = irq_mux;
	irq_mux->num_irq = num_irq;
	irq_mux->irq_base = gul_init_irq_mux(irq_evt_regs, irq_mux);
	if (irq_mux->irq_base < 0) {
		dev_err(gul_dev->dev, "Failed to allocate IRQ irq_base:	%d\n",
			irq_mux->irq_base);
		ret = irq_mux->irq_base;
		goto err2;
	}
	create_virq_bit_map(gul_dev, gul_dev->gul_irq_priv);
	gul_set_irq_chip(irq_mux);
	/*XXX:TBD: Sysfs is not yet supported*/
	ret = gul_init_irq_stats(gul_dev, irq_mux);
	if (ret < 0)
		goto err2;
	return ret;
err2:
	kfree(irq_mux->virq_map);
err1:
	kfree(irq_mux);
	return ret;
}
#endif

static void gul_irq_processed(int irq, void *dev)
{
	struct gul_dev *gul_dev = (struct gul_dev *)dev;

#if 0
	struct gul_irq_mux_pram *irq_mux = gul_dev->gul_irq_priv;
	unsigned long irq_evt_clr_reg;
#endif
	struct gul_msg_unit *ccsr_mur;
	u32 *msiir1_reg;
	u8 *mem;
#if 0
	int irq_bit = irq - irq_mux->irq_base;
#endif
	mem = (gul_dev->mem_regions[GUL_MEM_REGION_CCSR].vaddr +
		MSG_UNIT_OFFSET);
	ccsr_mur = (struct gul_msg_unit *)mem;
	ccsr_mur += GUL_MSG_UNIT_1;
#if 0
	irq_evt_clr_reg = gul_mem_r(irq_mux->irq_evt_clr_reg + (irq_bit /
				GUL_EVT_BTS_PER_WRD));
	set_bit((irq_bit %
		   GUL_EVT_BTS_PER_WRD), (void *)&irq_evt_clr_reg);
	gul_mem_w(irq_evt_clr_reg, irq_mux->irq_evt_clr_reg + (irq_bit /
					GUL_EVT_BTS_PER_WRD));
#endif
	dma_wmb();
	/* raise message interrupt to Modem */
	msiir1_reg = &ccsr_mur->msiir;
	writel(GUL_RAISE_CLR_EVT, msiir1_reg);
#if 0
	irq_mux->irq_stats.num_msg_unit_irq_evt_raised++;
#endif
}

static irqreturn_t gul_irq_handler(int irq, void *dev)
{
	struct gul_dev *gul_dev = (struct gul_dev *)dev;
	struct gul_irq_mux_pram *irq_mux = NULL; //gul_dev->gul_irq_priv;
	int i;
	int j;
	int sts_bits;
	int match = 0;
	u32 num_irq = 0;
	u32 irq_evt_sts_reg;
	u32 irq_evt_en_reg;
	int irq_base;
	int num_words = 0;
#if 0
	if (!gul_dev->gul_irq_priv)
		irq_mux = gul_dev->gul_irq_priv;
	if (!irq_mux->num_irq)
		num_irq = irq_mux->num_irq;
	if (irq_mux->irq_evt_cfg_reg != NULL)
		num_words = gul_mem_r(irq_mux->irq_evt_cfg_reg) &
				NIRQ_WORDS_MASK;
	else
		num_words = 2;

	irq_mux->irq_stats.num_hw_irq_recv++;
#endif
	num_words = 1;
	for (j = 0; j < num_words; j++) {
		sts_bits = (num_irq < GUL_EVT_BTS_PER_WRD) ? num_irq :
			GUL_EVT_BTS_PER_WRD;
#if 0
		irq_base = irq_mux->irq_base + (j * GUL_EVT_BTS_PER_WRD);
		irq_evt_sts_reg = gul_mem_r(irq_mux->irq_evt_sts_reg + j);
		irq_evt_en_reg = gul_mem_r(irq_mux->irq_evt_en_reg + j);
#endif
		for (i = 0; i < sts_bits; i++) {
			if ((irq_evt_sts_reg & BIT(i)) & irq_evt_en_reg) {
				generic_handle_irq(irq_base + i);
				match = 1;
				gul_irq_processed((irq_base + i), dev);
				irq_mux->irq_stats.num_virq_evt_raised++;
			}
		}
	}

	if (!match)
		dev_warn(gul_dev->dev, "No irq_mux line matched irq %d\n", irq);

	return IRQ_HANDLED;
}

int gul_request_irq(struct gul_dev *gul_dev,
			struct irq_evt_regs *irq_evt_regs)
{
	int err = 0;
#if 0
	u32 num_irq = (gul_mem_r(&irq_evt_regs->irq_evt_cfg) & NIRQ_MASK) >> 8;
#endif
	u32 num_irq = (u32)8; /* Hard code */

	if (!num_irq) {
		dev_err(gul_dev->dev, "Invalid rhl config! 'num irq 0'\n");
		return -EINVAL;
	}
#if 0
	err = gul_probe_irq_mux(gul_dev, irq_evt_regs);
#endif
	if (!err) {
		err = request_irq(gul_get_msi_irq(gul_dev, MSI_IRQ_MUX),
					gul_irq_handler, 0, gul_dev->name,
					(void *)gul_dev);
		if (!err) {
			gul_create_outbound_msi(gul_dev);
		} else {
			irq_free_descs(gul_dev->gul_irq_priv->irq_base,
				num_irq);
			kfree(gul_dev->gul_irq_priv);
			gul_dev->gul_irq_priv = NULL;
		}
	}

	return err;
}

int gul_clean_request_irq(struct gul_dev *gul_dev,
			struct irq_evt_regs *irq_evt_regs)
{
	u32 num_irq = (gul_mem_r(&irq_evt_regs->irq_evt_cfg) & NIRQ_MASK) >> 8;

	if (gul_dev->gul_irq_priv) {
		if (num_irq && gul_dev->gul_irq_priv->irq_base)
			irq_free_descs(gul_dev->gul_irq_priv->irq_base,
					num_irq);
		kfree(gul_dev->gul_irq_priv->virq_map);
		kfree(gul_dev->gul_irq_priv);
	}
	return 0;
}
