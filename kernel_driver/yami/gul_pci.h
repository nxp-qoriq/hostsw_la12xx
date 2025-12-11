/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2024 NXP
 */

#ifndef _PCI_UTILITIES_H
#define _PCI_UTILITIES_H

#include <linux/device.h>
#include <linux/pci.h>
#include "gul_pci_def.h"

extern struct list_head pcidev_list;
extern char *gul_dev_name_prefix_g;
#define PCI_IRQ_MSI             (1 << 1) /* allow MSI interrupts */

#define PCIE_ABSERR             0x8d0
#define PCIE_ABSERR_SETTING     0x9401

#define GUL_MMAP_CCSR_OFFSET	0x8000000
#define GUL_MMAP_CCSR_SIZE	0x4000000

#define GUL_MMAP_DCSR_OFFSET	0xC000000
#define GUL_MMAP_DCSR_SIZE	0x100000

#define GUL_MMAP_PEBM_OFFSET	0x200000
#define GUL_MMAP_PEBM_SIZE	0x200000

#define GUL_MEM_REGION_FECA_MEM_OFFSET	0x5000000
#define GUL_MEM_REGION_FECA_MEM_SIZE	0x2000000

#define GUL_PCI_CFG_REG		0x3400000
#define GUL_PCI_LINK_CAP_REG		0x7c
#define GUL_PCI_LINK_STATUS_REG		0x80
#define get_pci_linkcap(i)	((GUL_PCI_CFG_REG + i * 0x100000) + GUL_PCI_LINK_CAP_REG)
#define get_pci_curr_linkstat(i)	((GUL_PCI_CFG_REG + i * 0x100000) + GUL_PCI_LINK_STATUS_REG)

#endif /* _PCI_UTILITIES_H */
