/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2020-2026 NXP
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/delay.h>
#include "gul_pci.h"
#include "gul_base.h"
#include "gul_frtos_boot.h"

#ifdef BOOTROM_TEST
static struct cfword cfw[128] = {

	//addr, value
	{0xf9e00200, 0x1},
	{0xf9e00204, 0x1},
	{0xf9e00208, 0x1},
	{0xf9e0020c, 0x1},
	{0xf9e00210, 0x1},
	{0xf9e00214, 0x1},
	{0xf9e00218, 0x1},
	{0xf9e0021c, 0x1},
	{0xf9e00200, 0x2},
	{0xf9e00204, 0x2},
	{0xf9e00208, 0x2},
	{0xf9e0020c, 0x2},
	{0xf9e00210, 0x2},
	{0xf9e00214, 0x2},
	{0xf9e00218, 0x2},
	{0xf9e0021c, 0x2},
	{0xf9e00200, 0x3},
	{0xf9e00204, 0x3},
	{0xf9e00208, 0x3},
	{0xf9e0020c, 0x3},
	{0xf9e00210, 0x3},
	{0xf9e00214, 0x3},
	{0xf9e00218, 0x3},
	{0xf9e0021c, 0x3},
	{0xf9e00200, 0x4},
	{0xf9e00204, 0x4},
	{0xf9e00208, 0x4},
	{0xf9e0020c, 0x4},
	{0xf9e00210, 0x4},
	{0xf9e00214, 0x4},
	{0xf9e00218, 0x4},
	{0xf9e0021c, 0x4},
	{0xf9e00200, 0x5},
	{0xf9e00204, 0x5},
	{0xf9e00208, 0x5},
	{0xf9e0020c, 0x5},
	{0xf9e00210, 0x5},
	{0xf9e00214, 0x5},
	{0xf9e00218, 0x5},
	{0xf9e0021c, 0x5},
	{0xf9e00200, 0x6},
	{0xf9e00204, 0x6},
	{0xf9e00208, 0x6},
	{0xf9e0020c, 0x6},
	{0xf9e00210, 0x6},
	{0xf9e00214, 0x6},
	{0xf9e00218, 0x6},
	{0xf9e0021c, 0x6},
	{0xf9e00200, 0x7},
	{0xf9e00204, 0x7},
	{0xf9e00208, 0x7},
	{0xf9e0020c, 0x7},
	{0xf9e00210, 0x7},
	{0xf9e00214, 0x7},
	{0xf9e00218, 0x7},
	{0xf9e0021c, 0x7},
	{0xf9e00200, 0x8},
	{0xf9e00204, 0x8},
	{0xf9e00208, 0x8},
	{0xf9e0020c, 0x8},
	{0xf9e00210, 0x8},
	{0xf9e00214, 0x8},
	{0xf9e00218, 0x8},
	{0xf9e0021c, 0x8},
	{0xf9e00200, 0x9},
	{0xf9e00204, 0x9},
	{0xf9e00208, 0x9},
	{0xf9e0020c, 0x9},
	{0xf9e00210, 0x9},
	{0xf9e00214, 0x9},
	{0xf9e00218, 0x9},
	{0xf9e0021c, 0x9},
	{0xf9e00200, 0xa},
	{0xf9e00204, 0xa},
	{0xf9e00208, 0xa},
	{0xf9e0020c, 0xa},
	{0xf9e00210, 0xa},
	{0xf9e00214, 0xa},
	{0xf9e00218, 0xa},
	{0xf9e0021c, 0xa},
	{0xf9e00200, 0xb},
	{0xf9e00204, 0xb},
	{0xf9e00208, 0xb},
	{0xf9e0020c, 0xb},
	{0xf9e00210, 0xb},
	{0xf9e00214, 0xb},
	{0xf9e00218, 0xb},
	{0xf9e0021c, 0xb},
	{0xf9e00200, 0xc},
	{0xf9e00204, 0xc},
	{0xf9e00208, 0xc},
	{0xf9e0020c, 0xc},
	{0xf9e00210, 0xc},
	{0xf9e00214, 0xc},
	{0xf9e00218, 0xc},
	{0xf9e0021c, 0xc},
	{0xf9e00200, 0xd},
	{0xf9e00204, 0xd},
	{0xf9e00208, 0xd},
	{0xf9e0020c, 0xd},
	{0xf9e00210, 0xd},
	{0xf9e00214, 0xd},
	{0xf9e00218, 0xd},
	{0xf9e0021c, 0xd},
	{0xf9e00200, 0xe},
	{0xf9e00204, 0xe},
	{0xf9e00208, 0xe},
	{0xf9e0020c, 0xe},
	{0xf9e00210, 0xe},
	{0xf9e00214, 0xe},
	{0xf9e00218, 0xe},
	{0xf9e0021c, 0xe},
	{0xf9e00200, 0xf},
	{0xf9e00204, 0xf},
	{0xf9e00208, 0xf},
	{0xf9e0020c, 0xf},
	{0xf9e00210, 0xf},
	{0xf9e00214, 0xf},
	{0xf9e00218, 0xf},
	{0xf9e0021c, 0xf},
	{0xf9e00200, 0x11111111},
	{0xf9e00204, 0x22222222},
	{0xf9e00208, 0x33333333},
	{0xf9e0020c, 0x44444444},
	{0xf9e00210, 0xaaaaaaaa},
	{0xf9e00214, 0xbbbbbbbb},
	{0xf9e00218, 0xcccccccc},
	{0xf9e0021c, 0xdddddddd},
};

static int bootrom_test_read_and_load_program(struct gul_dev *gul_dev, struct gul_boot_header *p_ibr_hdr,
		char *fw_pci_phys_addr, char *fw_pci_addr, uint32_t pg_hd_off,
		uint32_t ph_num, uint32_t fw_size)
{
	int prog_num = 0, prog_idx = 0;
	uint32_t *program_ptr  = NULL;
	struct program_header *prog_header = NULL;
	uint32_t program_offset = 0, program_size = 0, program_vma = 0;
	uint32_t program_aligned_size = 0;

	if (fw_pci_addr == NULL) {
		dev_err(gul_dev->dev, "Load sections: file start is NULL");
		return -EEXIST;
	}

	/*Fetching Program header from firmware file */
	prog_header = (struct program_header *)(fw_pci_addr + pg_hd_off);
	dev_info(gul_dev->dev, "INFO:%s:Program Count: %d\n", __func__, ph_num);

	/*
	SG table entries for BOOTROM Testing

	Entry   Module                  destination address                     size
	======= =========               ===================                     ======
	  0                     Core 0 DMEM             0xe010_0000             0x11a4
	  1                     SRAM                    0xe01f_8000             0x90
	  2                     PEBM_e020               0xe02f_7000             0x287ec
	  3                     PEBM_e020               0xe032_8800             0xa4f4
	  4                     PEBM_e020               0xe0a4_0000             0x90
	  5                     PEBM_e060               0xe060_2000             0x1000
	  6                     PEBM_e060               0xe060_1000             0x1000
	  7                     PEBM_e020               0xe030_1000             0x1000
	*/

	for (; prog_num < ph_num; prog_num++) {

		program_vma = __be32_to_cpu(prog_header[prog_num].prg_vaddr);
		program_size = __be32_to_cpu(prog_header[prog_num].prg_filesz);
		program_offset = __be32_to_cpu(prog_header[prog_num].prg_offset);

		/* skip zero size */
		if (program_size == 0)
			continue;

#if GUL_QDMA_FOR_BOOT_ENABLE == 1
		if (prog_num == 0) {
			program_vma = DMEM_ADDRESS;
		}
#endif
		if (program_size > fw_size) {
			dev_err(gul_dev->dev, "Load Program: Program too big 0x%08X",
					program_size);
			return -ENOMEM;
		}

		program_aligned_size = align(program_size, 4);
		dev_info(gul_dev->dev,
			"INFO: Program Num: %d, Program Addr: %x, Program Size: %x\n",
			prog_num, program_vma, program_size);
		dev_dbg(gul_dev->dev, "INFO: Program Aligned Size: %x\n",
			program_aligned_size);

		program_ptr = (uint32_t *) (fw_pci_addr + program_offset);
		dev_dbg(gul_dev->dev,
				"INFO: LOAD from: %px, to: %x\n",
				program_ptr, program_vma);

		if (prog_idx > GEUL_MAX_SEG_ENTRIES - 1) {
			dev_err(gul_dev->dev, "Load Program: unable to load, GEUL_MAX_SEG_ENTRIES(%d) limit reached\n",
					GEUL_MAX_SEG_ENTRIES);
			return -ENOMEM;
		}

		p_ibr_hdr->sgtbl[prog_idx].len = __cpu_to_be32(program_aligned_size);
		p_ibr_hdr->sgtbl[prog_idx].resv = __cpu_to_be32(GEUL_RSVD);
		p_ibr_hdr->sgtbl[prog_idx].src =
			__cpu_to_be32((long)fw_pci_phys_addr + program_offset);
		p_ibr_hdr->sgtbl[prog_idx].dest = __cpu_to_be32(program_vma);

	    dev_info(gul_dev->dev,
			"INFO: Program Num: %d, Program Addr: %x, Program Size: %x\n",
			prog_idx,
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].dest),
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].len));

	    dev_info(gul_dev->dev,
			"INFO: LOAD from: %x, to: %x\n",
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].src),
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].dest));
	prog_idx++;
	}

	// PEBM - 0xe060_0000, duplicated from p_ibr_hdr->sgtbl[2]
	p_ibr_hdr->sgtbl[prog_idx].len = __cpu_to_be32(0x1000);
	p_ibr_hdr->sgtbl[prog_idx].resv = __cpu_to_be32(GEUL_RSVD);
	p_ibr_hdr->sgtbl[prog_idx].src = (p_ibr_hdr->sgtbl[2].src);
	p_ibr_hdr->sgtbl[prog_idx].dest = __cpu_to_be32(0xe0602000);
	dev_info(gul_dev->dev,
			"INFO: Program Num: %d, Program Addr: %x, Program Size: %x\n",
			prog_idx,
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].dest),
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].len));

	dev_info(gul_dev->dev,
			"INFO: LOAD from: %x, to: %x\n",
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].src),
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].dest));
	prog_idx++;

	// PEBM - 0xe060_1000, duplicated from p_ibr_hdr->sgtbl[3]
	p_ibr_hdr->sgtbl[prog_idx].len = __cpu_to_be32(0x1000);
	p_ibr_hdr->sgtbl[prog_idx].resv = __cpu_to_be32(GEUL_RSVD);
	p_ibr_hdr->sgtbl[prog_idx].src = (p_ibr_hdr->sgtbl[3].src);
	p_ibr_hdr->sgtbl[prog_idx].dest = __cpu_to_be32(0xe0601000);
	dev_info(gul_dev->dev,
			"INFO: Program Num: %d, Program Addr: %x, Program Size: %x\n",
			prog_idx,
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].dest),
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].len));

	dev_info(gul_dev->dev,
			"INFO: LOAD from: %x, to: %x\n",
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].src),
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].dest));


	prog_idx++;

	// PEBM - 0xe030_1000, duplicated from p_ibr_hdr->sgtbl[0]
	p_ibr_hdr->sgtbl[prog_idx].len = __cpu_to_be32(0x1000);
	p_ibr_hdr->sgtbl[prog_idx].resv = __cpu_to_be32(GEUL_RSVD);
	p_ibr_hdr->sgtbl[prog_idx].src = (p_ibr_hdr->sgtbl[0].src);
	p_ibr_hdr->sgtbl[prog_idx].dest = __cpu_to_be32(0xe0301000);
	dev_info(gul_dev->dev,
			"INFO: Program Num: %d, Program Addr: %x, Program Size: %x\n",
			prog_idx,
					      __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].dest),
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].len));

	dev_info(gul_dev->dev,
			"INFO: LOAD from: %x, to: %x\n",
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].src),
	    __be32_to_cpu(p_ibr_hdr->sgtbl[prog_idx].dest));
	prog_idx++;

	/* program the sgentries */
	p_ibr_hdr->sgentries = __cpu_to_be32(prog_idx);

	//update CFwords in boot header
	p_ibr_hdr->cfword_count = __cpu_to_be32(0x80);

	for (prog_num = 0; prog_num < 128; prog_num++) {
		p_ibr_hdr->cfwrd[prog_num].addr = __cpu_to_be32(cfw[prog_num].addr);
		p_ibr_hdr->cfwrd[prog_num].data = __cpu_to_be32(cfw[prog_num].data);
	}
	return 0;
}
#endif

int check_file(const char *filename)
{
	struct file *file;
	char path[FIRMWARE_NAME_SIZE];

	if (!filename || !*filename)
		return -EINVAL;

	sprintf(path, "/lib/firmware/%s", filename);

	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	fput(file);
	return 0;
}

#ifndef BOOTROM_TEST
static int fw_read_and_load_program(struct gul_dev *gul_dev, struct gul_boot_header *p_ibr_hdr,
		char *fw_pci_phys_addr, char *fw_pci_addr, uint32_t pg_hd_off,
		uint32_t ph_num, uint32_t fw_size)
{
	int prog_num = 0, prog_idx = 0;
	uint32_t *program_ptr  = NULL;
	struct program_header *prog_header = NULL;
	uint32_t program_offset = 0, program_size = 0, program_vma = 0;
	uint32_t program_aligned_size = 0;

	if (fw_pci_addr == NULL) {
		dev_err(gul_dev->dev, "Load sections: file start is NULL");
		return -EEXIST;
	}

	/*Fetching Program header from firmware file */
	prog_header = (struct program_header *)(fw_pci_addr + pg_hd_off);
	dev_info(gul_dev->dev, "INFO: %s:Program Count: %d\n", __func__, ph_num);

	for (; prog_num < ph_num; prog_num++) {

		program_vma = __be32_to_cpu(prog_header[prog_num].prg_vaddr);
		program_size = __be32_to_cpu(prog_header[prog_num].prg_filesz);
		program_offset = __be32_to_cpu(prog_header[prog_num].prg_offset);

		/* skip zero size */
		if (program_size == 0)
			continue;

		if (program_size > fw_size) {
			dev_err(gul_dev->dev, "Load Program: Program too big 0x%08X",
					program_size);
			return -ENOMEM;
		}

#if GUL_QDMA_FOR_BOOT_ENABLE == 1
		if (prog_idx == 0) {
			program_vma = DMEM_ADDRESS;
			dev_info(gul_dev->dev, "INFO: Qdma used for Copying Programs\n");
		}
#endif
		program_aligned_size = align(program_size, 4);
		dev_info(gul_dev->dev,
			"INFO: Program Num: %d, Program Addr: %x, Program Size: %x\n",
			prog_num, program_vma, program_size);
		dev_dbg(gul_dev->dev, "INFO: Program Aligned Size: %x\n",
			program_aligned_size);

		program_ptr = (uint32_t *) (fw_pci_addr + program_offset);
		dev_dbg(gul_dev->dev,
				"INFO: LOAD from: %px, to: %x\n",
				program_ptr, program_vma);

		if (prog_idx > GEUL_MAX_SEG_ENTRIES - 1) {
			dev_err(gul_dev->dev, "Load Program: unable to load, GEUL_MAX_SEG_ENTRIES(%d) limit reached\n",
					GEUL_MAX_SEG_ENTRIES);
			return -ENOMEM;
		}

		p_ibr_hdr->sgtbl[prog_idx].len = __cpu_to_be32(program_aligned_size);
		p_ibr_hdr->sgtbl[prog_idx].resv = __cpu_to_be32(GEUL_RSVD);
		p_ibr_hdr->sgtbl[prog_idx].src =
			__cpu_to_be32((long)fw_pci_phys_addr + program_offset);
		p_ibr_hdr->sgtbl[prog_idx].dest = __cpu_to_be32(program_vma);

		prog_idx++;
	}

	/* program the sgentries */
	p_ibr_hdr->sgentries = __cpu_to_be32(prog_idx);
	return 0;
}
#endif /* BOOTROM_TEST */

static int gul_load_boot_images(struct gul_dev *gul_dev,
					struct gul_boot_header *p_ibr_hdr,
					char *fw_pci_phys_addr,
					char *fw_pci_addr,
					int fw_size)
{
	struct file_header *hd;

	/* FW Header Initialization*/
	hd = (struct file_header *) fw_pci_addr;

	/* preamble and seg entries */
	p_ibr_hdr->bl_entry = hd->fh_entry;

#if GUL_QDMA_FOR_BOOT_ENABLE == 1
	p_ibr_hdr->flags = __cpu_to_be32(GEUL_USE_QDMA);
#else
	p_ibr_hdr->flags = __cpu_to_be32(GEUL_USE_MEMCOPY);
#endif

	dev_dbg(gul_dev->dev, "INFO: hd->fh_machine %x\n", __be16_to_cpu(hd->fh_machine));
	dev_dbg(gul_dev->dev, "INFO: hd->fh_entry %x\n", __be32_to_cpu(hd->fh_entry));

	/* Image Header Format Checking */
	if (__be16_to_cpu(hd->fh_machine) != POWERPC_MACH_CODE) {
		dev_err(gul_dev->dev, "Load_elf: bad hdr fh_machine 0x%02X",
							__be16_to_cpu(hd->fh_machine));
		kvfree(fw_pci_addr);
		return -ENOEXEC;
	}

	dev_dbg(gul_dev->dev, "INFO: hd->fh_phoff %x, hd->fh_shoff %x, hd->fh_phnum, %x, hd->fh_shnum %x, hd->fh_shstrndx %x\n",
			__be32_to_cpu(hd->fh_phoff), __be32_to_cpu(hd->fh_shoff), __be16_to_cpu(hd->fh_phnum),  __be16_to_cpu(hd->fh_shnum),
			__be16_to_cpu(hd->fh_shstrndx));

	/*FW image parsing and load sections*/
#ifdef BOOTROM_TEST
	if (bootrom_test_read_and_load_program(gul_dev, p_ibr_hdr, fw_pci_phys_addr, fw_pci_addr, __be32_to_cpu(hd->fh_phoff),
		__be16_to_cpu(hd->fh_phnum), fw_size)) {
		dev_err(gul_dev->dev, "ERR %s: Image Program Loading Failed",
									__func__);
		return -EIO;
	}
#else
	if (fw_read_and_load_program(gul_dev, p_ibr_hdr, fw_pci_phys_addr, fw_pci_addr, __be32_to_cpu(hd->fh_phoff),
		__be16_to_cpu(hd->fh_phnum), fw_size)) {
		dev_err(gul_dev->dev, "ERR %s: Image Program Loading Failed",
						__func__);
		return -EIO;
	}
#endif
	return 0;
}

int gul_do_reset_handshake(struct gul_dev *gul_dev)
{
	int rc = 0, retries = GUL_HOST_BOOT_HSHAKE_RETRIES;
	struct dcfg_scratch_regs *scratch_regs;
	u32 hif_offset, hif_size;

	volatile u32 *hif_offset_reg, *hif_size_reg;

	scratch_regs = (struct dcfg_scratch_regs *)
			((u64) gul_dev->mem_regions[GUL_MEM_REGION_CCSR].vaddr
			+ DCFG_SCRATCH_OFFSET);
	hif_size_reg = &scratch_regs->scratchrw[GUL_BOOT_HSHAKE_HIF_SIZ_REG];
	dma_rmb();
	hif_offset_reg = &scratch_regs->scratchrw[GUL_BOOT_HSHAKE_HIF_REG];
	dma_rmb();

	dev_info(gul_dev->dev, "Reset HandShake: Waiting for HIF offset from modem\n");

	set_current_state(TASK_INTERRUPTIBLE);
	/* Wait for FreeRTOS to complete reset hand shake */
	schedule_timeout(msecs_to_jiffies(GUL_HOST_BOOT_HSHAKE_TIMEOUT));
	dma_rmb();

	hif_offset = readl(hif_offset_reg);
	dma_rmb();
	hif_size = readl(hif_size_reg);
	dma_rmb();

	while ((!hif_size) && retries) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(
					GUL_HOST_BOOT_HSHAKE_TIMEOUT));
		retries--;
		dma_rmb();
		hif_offset = readl(hif_offset_reg);
		hif_size = readl(hif_size_reg);
	}

	if (hif_size) {
		dev_info(gul_dev->dev, "Reset HandShake: Done. HIF 0x%x, size 0x%x (%d)\n",
			hif_offset, hif_size, hif_size);
		gul_dev->hif_offset = hif_offset;
		gul_dev->hif_size = hif_size;
	} else {
		dev_err(gul_dev->dev, "Reset HandShake: Failed. HIF 0x%x, size 0x%x (%d)\n",
			hif_offset, hif_size, hif_size);
		rc = -EBUSY;
	}

	return rc;
}

/* Load FRTOS Images for GEUL on PEBM and DMEM
 * Along with this we need to load BOOT ROM header.
 *
 * Flow will be as follows:
 *
 * 1. TEXT of all images concatenated in one single
 *	  image needs to be loaded to a PCI address.
 * 2. DMEM of each Core needs to be loaded separately
 *	  in PCI addresses.
 * 3. Load FRTOS image BOOT HEADER on PEBM
 * 4. Write PREAMBLE to enabled BOOTROM running on GEUL
 *	  to proceed
 */

/* Loads FRTOS images on GEUL using helper routines
 */
int gul_load_rtos_img(struct gul_dev *gul_dev)
{
	int rc, fw_size;
	struct gul_mem_region_info *rtos_fw_region;
	struct gul_mem_region_info *pebm_region;
	struct gul_boot_header __iomem *boot_header;
	int size;
	char freertos_img[FIRMWARE_NAME_SIZE];

	pebm_region = &gul_dev->mem_regions[GUL_MEM_REGION_PEBM];

	boot_header = (struct gul_boot_header *)((u8 *)pebm_region->vaddr
			+ GUL_EP_BOOT_HDR_OFFSET);
	gul_dev->boot_header = boot_header;

	rtos_fw_region = scratch_buf_allocator(gul_dev, GUL_FIRMWARE,
			GUL_MAX_IMAGE_SIZE);
	size = rtos_fw_region->size;	/* scratch_buf_size */

	//TODO: This should not be used for multi-modem case
#if !(defined(LA1238RDB) || defined(LA1238CPE))
	/*multi-modem case - looking to use different files */
	/*check if modem-id based firmware file present */
	if (warmup_flag[gul_dev->id] == 1)
		sprintf(freertos_img, "geul_e200-warmup_%d.elf", gul_dev->id);
	else
		sprintf(freertos_img, "geul_e200_%d.elf", gul_dev->id);
	rc = check_file(freertos_img);
#else
	rc = 1;
#endif
	if (rc) {
		/* use default file name */
		if (warmup_flag[gul_dev->id] == 1)
			sprintf(freertos_img, "%s", FIRMWARE_RTOS_WARMUP);
		else
			sprintf(freertos_img, "%s", firmware_name);
		rc = check_file(freertos_img);
		if (rc) {
			dev_err(gul_dev->dev,
					"FreeRTOS Image file(%s) not present\n",
					freertos_img);
			goto out;
		}
	}

	rc = gul_udev_load_firmware(gul_dev, rtos_fw_region->vaddr,
			size, freertos_img, &fw_size);
	if (rc < 0) {
		dev_err(gul_dev->dev, "%s: udev firmware request failed\n",
							__func__);
		goto out;
	}

	/* Read FRTOS images and load them to Host */
	rc = gul_load_boot_images(gul_dev, boot_header,
			(char *)rtos_fw_region->phys_addr,
			rtos_fw_region->vaddr,
			size);

	if (rc) {
		dev_err(gul_dev->dev, "udev Firmware [%s] request failed\n",
				freertos_img);
		goto out;
	}

	strcpy(gul_dev->fw_name, freertos_img);
	gul_dev->fw_size = fw_size;

	/* WRITE boot preamble on PEBM of Modem */
	dma_wmb(); // memory barrier before writing PREAMBLE
	writel(PREAMBLE, &boot_header->preamble);

	dev_info(gul_dev->dev, "Waiting for FreeRTOS (%s) boot.\n", freertos_img);
	return 0;
out:
	return rc;
}
