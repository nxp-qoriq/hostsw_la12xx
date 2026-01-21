/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2020-2026 NXP
 */
#ifndef __GUL_BASE_H__
#define __GUL_BASE_H__

#include <linux/if.h>
#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <gul_host_if.h>
#include <linux/firmware.h>
#include <asm/delay.h>
#include "gul_pci.h"
#include "gul_ipc_kern.h"
#include "gul_tti_ioctl.h"
#include "gul_stats.h"
#include "gul_wdog_ioctl.h"
#include <gul_modinfo.h>

#ifdef RF_FR2_DRVR_ENABLED
#include <gul_fr2_cmds.h>
#endif /* RF_FR2_DRVR_ENABLED */

#define FIRMWARE_RTOS			"geul_e200.elf"
#define FIRMWARE_RTOS_WARMUP    "geul_e200_warmup.elf"
#define FIRMWARE_NAME_SIZE 128
#define HS_ENABLE_ALL    0x3   /* default enable all HS DAC/ADC */
#define LS_ENABLE_ALL	 0xf /* default enable all LS DAC/ADC */


/*Boot HandShake timeout in jiffies and retry count */
#define GUL_HOST_BOOT_HSHAKE_TIMEOUT	1
#define GUL_HOST_BOOT_HSHAKE_RETRIES	600
#define GUL_IPC_INIT_WAIT_TIMEOUT	100
#define GUL_IPC_INIT_WAIT_RETRIES	50
#define GUL_RFIC_INIT_WAIT_TIMEOUT	100
#define GUL_RFIC_INIT_WAIT_RETRIES	50

/*Enable the multiple MSIs support */
#define GUL_REAL_MSI_FLAG       (1 << 0)

#define MSI_TYPE_A		0
#define MSI_TYPE_B		1
#define MSI_TYPE_C		2

/* Project specific Modem Share are partition from the scratch buffer */
/* L1 Area, L2 area, RF Area */
#if defined(LA1238RDB) || defined(LA1238CPE) || defined(GEUL_LA12XXGDE)
#define MODEM_SHARE_MODEM_SIZE (32 * 1024 * 1024) /*32 MB*/
#define MODEM_SHARE_RF_SIZE	(64 * 1024 * 1024)/* 64 MB */
#else
#define MODEM_SHARE_MODEM_SIZE (112 * 1024 * 1024) /*112 MB*/
#define MODEM_SHARE_RF_SIZE	(0 * 1024 * 1024)/* 0 MB */
#endif

#define GUL_DEFAULT_MODEM_SHARE_SIZE	\
	(MODEM_SHARE_MODEM_SIZE + MODEM_SHARE_RF_SIZE)

/* Scratch buffer partitioning */
/* Modem Share Area, IMAGE loading, EP logging , VSPA OVERYLAY */

/*Ep logger size in scratch buffer */
#define GUL_CORE_LOG_BUF_SIZE	(4 * 1024)	/* 4 Kb for each Core */
#define GUL_EP_LOGGER_SIZE	(GUL_EP_CORE_MAX * GUL_CORE_LOG_BUF_SIZE)
/*Firmware (VSPA, e200, HSDCS) Buffer Max Size */
#define GUL_MAX_IMAGE_SIZE	((4 * 1024 * 1024) - GUL_EP_LOGGER_SIZE)
/*VSPA Firmware Max size for buffer*/
#define GUL_VSPA_SCRATCH_BUF_MAX_SIZE   (10 * 1024 * 1024)
/* 2 MB in scratch for Overlay */
#define GUL_VSPA_OVERLAY_SIZE	(2 * 1024 * 1024)

#define GUL_DEFAULT_SCRATCH_SIZE (GUL_DEFAULT_MODEM_SHARE_SIZE \
				+ GUL_MAX_IMAGE_SIZE \
				+ GUL_VSPA_SCRATCH_BUF_MAX_SIZE \
				+ GUL_EP_LOGGER_SIZE \
				+ GUL_VSPA_OVERLAY_SIZE)

/* Macros to mark status of IRQ line */
#define GUL_MSI_IRQ_FREE	0x45455246	/* 'F''R''E''E' */
#define GUL_MSI_IRQ_BUSY	0x59535542	/* 'B''U''S''Y' */

/* Number of Modem Instances and so number of Watchdog devices */
#define MAX_MODEM		4

/* Macros to perform address alignment */
#define isaligned(ptr, align)  \
	(((ptr) & (align - 1)) == 0)

#define _align_mask(x, mask)    (((x) + (mask)) & ~(mask))

#define align(x, a)     _align_mask(x, (typeof(x))(a) - 1)

#define IN_MB(x) ((x)/(1024*1024))

#define MAX_VSPA 8

#if GUL_QDMA_FOR_BOOT_ENABLE == 1
#define DMEM_ADDRESS 0xE0100000
#endif

#define DEFAULT_WARM_UP_TEMP                0
#define DEFAULT_WARM_UP_TIMEOUT_IN_SEC      0
#define DEFAULT_WARM_UP_POLL_INTRL_VAL      5
#define TIMEGAP_USEC_MIN					90000
#define TIMEGAP_USEC_MAX					100000

#define FACT_UNIQUE_ID			0x1E8021C
#define DCFG_CCSSR_BASE_ADDR		0x1E00000
#define FUSESR_OFFSET			0x60
#define FUSE_PERSONALITY_SHIFT		12

enum soc_fuse {
	FUSE_LA1200 = 0,
	FUSE_LA1201,
	FUSE_LA1212,
	FUSE_LA1214,
	FUSE_LA1215,
	FUSE_LA1216,
	FUSE_LA1223,
	FUSE_LA1225,
	FUSE_LA1234,
	FUSE_LA1235,
	FUSE_LA1236,
	FUSE_LA1218,
	FUSE_LA1238,
	FUSE_LA1232,
	FUSE_RESV,
	FUSE_LA1224,
	FUSE_INVALID,
};


struct gul_global {
	bool active;
	bool e200_load_status;
	bool lsdcs_load_status;
	bool hsdcs_load_status;
	bool vspa_firmware_status[MAX_VSPA];
	char dev_name[64];
	uint32_t soc_version;
	int share_buf_size;
	int scratch_buf_size;
	uint64_t share_buf_phys_addr;
	uint64_t scratch_buf_phys_addr;
};

extern struct gul_global g_gul_global[];

extern int tti_per_dev;
extern int cli_dmesg_on;
extern int ls_dac_sps;
extern int ls_adc_sps;
extern int lsdcs_disable;
extern int hsdcs_sps;
extern int hsdcs_enable;
extern int hsdac_mask;
extern int hsadc_mask;
extern int lsdac_mask;
extern int lsadc_mask;
extern int tbgen2_disable;
extern int vspa_disable;
extern int rfic_disable;
extern int tvd_disable;
extern int wdog_disable;
extern int global_scratch_buf_size;
extern int modem_share_buf_size;
extern int modem_rf_data_size;
extern int modem_host_data_size;
extern uint64_t scratch_buf_phys_addr;
extern char vspa_fw_name_prefix[];
extern char firmware_name[];
extern uint32_t warmup_temp;
extern uint32_t warmup_timeout;
extern uint32_t warmup_poll_intvl;
extern bool warmup_flag[MAX_MODEM];
extern int modem_host_uart;
extern int disable_sideband;

enum gul_init_stage {
	GUL_DMA_INIT_STAGE = 1,
	GUL_SCRATCH_DMA_INIT_STAGE,
	GUL_SYSFS_INIT_STAGE,
	GUL_HANDSHAKE_INIT_STAGE,
	GUL_IRQ_INIT_STAGE,
	GUL_SUBDRV_PROBE_STAGE
};

enum soc_fuse_val {
	LA1216 = 5,
	LA1236 = 10,
	LA1218 = 11,
	LA1238 = 12
};
struct virq_evt_map {
	gul_irq_evt_bits_t evt;
	int virq;
	int msi_value;
};

struct gul_irq_mux_stats {
	unsigned long num_virq_evt_raised;
	unsigned long num_hw_irq_recv;
	unsigned long num_msg_unit_irq_evt_raised;
};

struct gul_irq_mux_pram {
	int irq_base;
	u32 *irq_evt_cfg_reg;
	u32 *irq_evt_en_reg;
	u32 *irq_evt_sts_reg;
	u32 *irq_evt_clr_reg;
	struct gul_irq_mux_stats irq_stats;
	u32 num_irq;
	struct virq_evt_map *virq_map;
};

/**
 *  GUL mem region types
 *  NOTE:XXX: Bar regions should be defined
 *  before GUL_MEM_REGION_BAR_END. these regions
 *  are stored in gul_dev->mem_regions
 *  NOTE:XXX: DMA BUF regions must be defined
 *  after GUL_MEM_REGION_BARD_END. these
 *  regions are stored in gul_dev->dma_info
 */

enum gul_mem_region_t {
	GUL_MEM_REGION_CCSR = 0,
	GUL_MEM_REGION_DCSR,
	GUL_MEM_REGION_PEBM,
	GUL_MEM_REGION_FECA,
	GUL_MEM_REGION_BAR_END,
	GUL_MEM_REGION_FW,
	GUL_MEM_REGION_VSPA,
	GUL_MEM_REGION_IPC,
	GUL_MEM_REGION_MODEM_SHARE,
	GUL_MEM_REGION_DBG_LOG,
	GUL_MEM_REGION_MAX,
};

enum gul_mem_region_offset_t {
	GUL_MEM_REGION_CCSR_OFFSET = GUL_MMAP_CCSR_OFFSET,
	GUL_MEM_REGION_DCSR_OFFSET = GUL_MMAP_DCSR_OFFSET,
	GUL_MEM_REGION_PEBM_OFFSET = GUL_MMAP_PEBM_OFFSET,
	GUL_MEM_REGION_FECA_OFFSET = GUL_MEM_REGION_FECA_MEM_OFFSET,
};

enum gul_stat_control_t {
	EP_CONTROL_IRQ_STATS = 0,
	EP_CONTROL_AVI_STATS,
	EP_CONTROL_EDMA_STATS,
	EP_CONTROL_WDOG_STATS,
	EP_CONTROL_IPC_STATS,
	HOST_CONTROL_IPC_STATS,
	HOST_CONTROL_IRQ_STATS,
	HOST_CONTROL_VSPA_STATS,
	HOST_CONTROL_RFIC_STATS,
	HOST_CONTROL_HAWK_STATS,
};

#define GUL_ENABLE_STATS	1
#define GUL_DISABLE_STATS	0
#define GUL_STATS_DEFAULT_ENABLE_MASK	\
	((GUL_ENABLE_STATS << EP_CONTROL_IRQ_STATS) | \
	(GUL_ENABLE_STATS << EP_CONTROL_AVI_STATS) | \
	(GUL_ENABLE_STATS << EP_CONTROL_WDOG_STATS) | \
	(GUL_ENABLE_STATS << EP_CONTROL_IPC_STATS) | \
	(GUL_ENABLE_STATS << HOST_CONTROL_IPC_STATS) | \
	(GUL_ENABLE_STATS << HOST_CONTROL_IRQ_STATS) | \
	(GUL_ENABLE_STATS << HOST_CONTROL_VSPA_STATS))

/* Number of DMA regions*/
#define GUL_DMA_REGIONS (GUL_MEM_REGION_MAX - GUL_MEM_REGION_BAR_END - 1)

struct gul_mem_region_info {
	phys_addr_t phys_addr;
	u8 __iomem *vaddr;
	size_t size;
	enum gul_mem_region_t type;
};

struct dcfg_scratch_regs {
	u32	scratchrw[32];	/* Scratch Read/Write */
	u32	scratchw1r[4];	/* Scratch Read (Write once) */
};

#if defined (LA1224)
#define MULTI_LA12XX_MULTI_IMAGE_SUPPORT 1
#endif

int check_file(const char *filename);

/*
 * struct gul_dma_info
 *
 * GUL DMA buffer contains details of
 * DMAable buffer shared with GUL over PCI. The buffer exsits
 * in Host memory.
 *
 * @host_buf : DMA buffer allocated by linux DMA API (pci_alloc_consistent/
 * dma_alloc_coherent). host_buf.vaddr/host_buf.phys_addr point
 * to virtual and physical address in Host address space. This buffer
 * is mapped to the End-point (GUL) PCIe address space by creating
 * a outbound window.
 *
 * @ep_phys_addr_start: Physical address of buffer (host_buf.vaddr) in
 * end point (GUL) address space.
 *
 * @ep_bufs: DMA buffers for sub-drivers. These are initialized by cutting of
 * chunks of buffers from host_buf, thus these buffers are not allocated by
 * Linux DMA API (pci_alloc_consistent/dma_alloc_coherent) and they should
 * not be freed using Linux DMA API. eb_buf[SUBDRV].phys_addr is the physical
 * address of buffer in End-point PCIe address space. It can be programmed
 * directly as source/destination for DMAs inside DMA without any conversion.
 */
struct gul_dma_info {
	struct gul_mem_region_info host_buf;
	phys_addr_t ep_pcie_addr;
	struct gul_mem_region_info ep_bufs[GUL_DMA_REGIONS];
};

#define GUL_SUBDRV_DMA_REGION_IDX(i) (i - GUL_MEM_REGION_BAR_END - 1)

/* DMA buffer size definitions
 * IPC - No buffer required, all mdata is in PEBM.
 * XXX:TBD:FW_DMA SIZE
 * DBUG_LOG buffer - 4k
 */
#define GUL_VSPA_DMA_SIZE		(0)
#define GUL_IPC_DMA_SIZE		(0)
#define GUL_FW_DMA_SIZE			(0)
#define GUL_DBUG_LOG_SIZE		(4 * 1024)

/* Mem region separator */
#define GUL_DMA_SEPARATOR_SIZE		(64)
#define GUL_DMA_SEPARATOR_PAINT_CHAR	(0xFC)


#define GUL_DMA_SEPARATOR_TOTAL_SIZE	(GUL_DMA_SEPARATOR_SIZE * \
					 GUL_DMA_REGIONS)
#define GUL_DMA_ALIGNMENT		(64)
#define GUL_DMA_BUF_SIZE	(GUL_VSPA_DMA_SIZE + GUL_IPC_DMA_SIZE + \
				 GUL_FW_DMA_SIZE + GUL_DBUG_LOG_SIZE + \
				 GUL_DMA_SEPARATOR_TOTAL_SIZE + \
				 GUL_DMA_ALIGNMENT)

#define GUL_DCSR_SIZE	(1 * 1024 * 1024)

struct gul_ep_log {
	u8 *buf;
	int len;
	int offset;
};

struct scratch_allocator {
	uint64_t host_phys;
	u8 __iomem *host_vaddr;
	uint64_t mod_phys;
	uint64_t mod_phys_current;
	uint64_t mod_phys_end;
	size_t size;
};

extern uint8_t wdog_handler_notifier[MAX_MODEM];

/* ELF FILE configuration */
#define POWERPC_MACH_CODE           0x14

#define EI_NIDENT               16

struct program_header {
	uint32_t prg_type;            /* Segment type */
	uint32_t prg_offset;          /* Segment file offset */
	uint32_t prg_vaddr;           /* Segment virtual address */
	uint32_t prg_paddr;           /* Segment physical address */
	uint32_t prg_filesz;          /* Segment size in file */
	uint32_t prg_memsz;           /* Segment size in memory */
	uint32_t prg_flags;           /* Segment flags */
	uint32_t prg_align;           /* Segment alignment */
};

struct file_header {
	uint8_t  fh_ident[EI_NIDENT];
	uint16_t fh_type;
	uint16_t fh_machine;
	uint32_t fh_version;
	uint32_t fh_entry;
	uint32_t fh_phoff;
	uint32_t fh_shoff;
	uint32_t fh_flags;
	uint16_t fh_ehsize;
	uint16_t fh_phentsize;
	uint16_t fh_phnum;
	uint16_t fh_shentsize;
	uint16_t fh_shnum;
	uint16_t fh_shstrndx;
};
struct section_header {
	uint32_t sec_name;      /* Section name (string tbl index) */
	uint32_t sec_type;      /* Section type */
	uint32_t sec_flags;     /* Section flags */
	uint32_t sec_addr;      /* Section virtual addr at execution */
	uint32_t sec_offset;    /* Section file offset */
	uint32_t sec_size;      /* Section size in bytes */
	uint32_t sec_link;      /* Link to another section */
	uint32_t sec_info;      /* Additional section information */
	uint32_t sec_addralign; /* Section alignment */
	uint32_t sec_entsize;   /* Entry size if section holds table */
};


/**
 * struct gul_dev - The GUL device Private structure.
 *
 * This sturcure keeps all the context information of a GUL device
 * TBD:XXX: explain other fields as well. Currently priv is explained
 * so that sub-driver developers can understand usage.
 *
 * @vspa_priv:	Sub driver Priv pointers are supposted to point to
 *		sub-driver private data structure that they need to maintain
 *		for all data/info regarding this device. The priv structure
 *		will be allocated by sub-driver during the sub-driver's probe
 *		called by GUL core (gul_sub_driver_ops->probe), and sub-driver
 *		has to initialize gul_dev-><sub_drv>_priv with pointer to its
 *		private data stucrure. The sub driver SHOULD & MUST NOT keep any
 *		data global, it should be always part of private structure to
 *		make sure that driver scales when when more than one GUL device
 *		are found on system.
 */
struct irq_info {
	int irq_val;
	int msi_val;
	int free;
	int core_id;
};

struct tti_priv {
	int irq;
	int msi_index;
	int tti_id;
	uint64_t tti_count;
	/* TBD Timestamp; */
	struct swait_queue_head tti_wq;
	raw_spinlock_t wq_lock;
	int tti_irq_status;
	struct eventfd_ctx *evt_fd_ctxt;
};

struct tti_dev {
	struct tti_priv tti_priv_t[1];
};

/*TTI char Dev data holder */
struct gul_tti_device_data {
	struct tti_dev *tti_dev;
	struct cdev cdev;
};

struct gul_dev {
	struct pci_dev *pdev;
	struct device *dev;
	struct rfdev *rfdev;
	struct hawkdev *hawkdev;
	struct class *class;
	char fw_name[FIRMWARE_NAME_SIZE];
	int fw_size;
	char name[IFNAMSIZ];
	int id;
	u32 flags;
	struct gul_sub_driver *sub_drvs;
	struct gul_mem_region_info mem_regions[GUL_MEM_REGION_BAR_END];
	struct gul_mem_region_info scratch_buf_region[GUL_SCRATCH_END];
	struct scratch_allocator scratch_allocator;
	struct gul_msg_unit *msg_units[GUL_MSG_UNIT_CNT];
	struct gul_dma_info dma_info;
	struct gul_boot_header __iomem *boot_header;
	struct gul_hif *hif;
	u32 hif_offset;
	u32 hif_size;
	struct device_node *dn_modem;
	struct gul_ep_log ep_log[GUL_EP_CORE_MAX];
	//int irq[GUL_MSI_MAX_CNT];
	struct irq_info irq[GUL_MSI_MAX_CNT];
	int irq_count;
	struct gul_irq_mux_pram *gul_irq_priv;
	void *vspa_priv;
	uint32_t vspa_reset_mask;
	void *rfic_priv;
	void *dcs_priv;
	void *usim_priv;
	void *ipc_priv;
	void *tvd_priv;
	void *cli_priv;
	struct gul_stats_desc stats_desc;
	struct list_head list;
	struct gul_rfic_common_mdata *rfic_c_mdata;
	struct semaphore rfic_mdata_lock;
	ipc_handle_t rfic_ipc_handler;
	uint32_t pci_outbound_win_start_addr;
	uint32_t pci_outbound_win_current_addr;
	uint32_t pci_outbound_win_limit;
};

struct gul_modem_dev_id {
	struct pci_dev *pdev;
	int id;
	int is_pci_dev_removed;
	int remove_in_progress;
};


/*gul_dev->flags*/
#define GUL_FLG_PCI_MSI_EN	(1 << 0)
#define GUL_FLG_PCI_MSIX_EN	(1 << 1)
#define GUL_FLG_PCI_INT_LEGACY	(1 << 2)
#define GUL_FLG_PCI_8MSI_EN     (1 << 3)

#define GUL_SET_FLG(flag_var, flg) (flag_var |= flg)
#define GUL_CHK_FLG(flag_var, flg) (flag_var & flg)
#define GUL_CLR_FLG(flag_var, flg) (flag_var &= (~flg))


typedef int (*sub_drv_probe_t) (struct gul_dev *gul_dev, int virq_count,
		struct virq_evt_map *virq_map);

typedef int (*sub_drv_remove_t)(struct gul_dev *gul_dev);
typedef int (*sub_drv_mod_init_t)(void);
typedef int (*sub_drv_mod_exit_t)(void);

struct gul_sub_driver_ops {
	sub_drv_probe_t probe;
	sub_drv_remove_t remove;
	sub_drv_mod_init_t mod_init;
	sub_drv_mod_exit_t mod_exit;
};

enum gul_warmup_status {
	GUL_WARMUP_IN_PROGRESS = 0,
	GUL_WARMUP_COMPLETE
};

enum gul_subdrv_type_t {
	GUL_SUBDRV_TYPE_VSPA = 1,
	GUL_SUBDRV_TYPE_IPC,
	GUL_SUBDRV_TYPE_WDOG,
	GUL_SUBDRV_TYPE_RFIC,
	GUL_SUBDRV_TYPE_DCS,
	GUL_SUBDRV_TYPE_V2H,
	GUL_SUBDRV_TYPE_TVD,
	GUL_SUBDRV_TYPE_CLI,
	GUL_SUBDRV_TYPE_HAWK,
	GUL_SUBDRV_TYPE_USIM,
	GUL_SUBDRV_TYPE_TEST
};

struct gul_sub_driver {
	char *name;
	enum gul_subdrv_type_t type;
	struct gul_sub_driver_ops ops;
};

enum gul_reset_type {
	GUL_PO_RESET = 1,
	GUL_COLD_RESET,
	GUL_WARM_RESET,
};

int  vspa_probe(struct gul_dev *gul_dev, int virq_count,
		struct virq_evt_map *virq_map);
int  vspa_remove(struct gul_dev *gul_dev);

int  dcs_probe(struct gul_dev *gul_dev, int virq_count,
		struct virq_evt_map *virq_map);
int  dcs_remove(struct gul_dev *gul_dev);
int dcs_init(void);
int dcs_exit(void);

int  ipc_probe(struct gul_dev *gul_dev, int virq_count,
		struct virq_evt_map *virq_map);
int  ipc_remove(struct gul_dev *gul_dev);
ssize_t gul_ipc_show(struct gul_dev *dev, char *buf);

int  gul_test_probe(struct gul_dev *gul_dev, int virq_count,
		struct virq_evt_map *virq_map);
int  gul_test_remove(struct gul_dev *gul_dev);
int  gul_v2h_probe(struct gul_dev *gul_dev, int virq_count,
		struct virq_evt_map *virq_map);
int  gul_v2h_remove(struct gul_dev *gul_dev);

extern int gul_subdrv_mod_init(void);
extern void gul_subdrv_mod_exit(void);
extern void gul_pcidev_reset_device(unsigned int gul_id);
extern void wdog_reset_modem_ext(unsigned int id);

void gul_subdrv_remove(struct gul_dev *gul_dev);
int gul_base_deinit(struct gul_dev *gul_dev, int stage, int drv_index);
int gul_load_rtos_img(struct gul_dev *gul_dev);
int gul_rfic_start_up(struct gul_dev *gul_dev, enum gul_reset_type rst_type);
int gul_do_reset_handshake(struct gul_dev *gul_dev);
void gul_hexdump(const void *ptr, size_t sz);
int gul_map_mem_regions(struct gul_dev *gul_dev);
void gul_unmap_mem_regions(struct gul_dev *gul_dev);
int gul_base_probe(struct gul_dev *gul_dev);
int gul_errata_a010867(struct gul_dev *gul_dev);
int gul_base_remove(struct gul_dev *gul_dev);
int gul_udev_load_firmware(struct gul_dev *gul_dev, char *buf,
			   int buff_sz, char *name, int *fw_size);
struct gul_mem_region_info *gul_get_scratch_region(struct gul_dev *gul_dev,
					enum scratch_buf_request_id id);
struct gul_mem_region_info *gul_get_dma_region(struct gul_dev *gul_dev,
					enum gul_mem_region_t type);
int gul_init_sysfs(struct gul_dev *gul_dev);
void gul_remove_sysfs(struct gul_dev *gul_dev);
int gul_create_outbound_msi(struct gul_dev *gul_dev);
int gul_host_add_stats(struct gul_dev *gul_dev,
			struct gul_stats_ops *stats_ops);
int gul_create_outbound_msi(struct gul_dev *gul_dev);
extern int gul_get_msi_irq(struct gul_dev *gul_dev, enum gul_msi_id type);
int wdog_test_init(void);
int wdog_test_deinit(void);
int v2h_callback_test_init(struct gul_dev *gul_dev);
int v2h_callback_test_deinit(void);
struct gul_dev *get_gul_dev_byname(const char *name);
void gul_init_ep_pcie_allocator(struct gul_dev *gul_dev);
uint32_t gul_alloc_ep_pcie_addr(struct gul_dev *gul_dev, uint32_t window_size);
void gul_set_host_ready(struct gul_dev *gul_dev, u32 set_bit);
int gul_raise_msgunit_irq(struct gul_dev *gul_dev,
			  int msg_unit_idx, int bit_num);
struct gul_mem_region_info *scratch_buf_allocator(struct gul_dev *gul_dev,
				enum scratch_buf_request_id id,
				int size);
int init_tti_dev(void);
int tti_register_irq(struct tti_dev *tti_dev, struct tti *tti_t);
void tti_deregister_irq(struct tti_dev *tti_dev, struct tti *tti_t);

int wdog_set_modem_status(int wdog_id, int status);
int wdog_set_pci_domain_nr(int wdog_id, int domain_nr);
void raise_modem_msi(struct gul_dev *gul_dev, int msi_type, int irq_no);
void remove_tti_dev(void);

int gul_dev_get_msi(struct gul_dev *gul_dev);
void gul_dev_put_msi(struct gul_dev *gul_dev, int index);

int gul_init_global_sysfs(void);
void gul_remove_global_sysfs(void);

ssize_t gul_collect_ep_log(struct gul_ep_log *ep_log, char *buf);

int get_fuse_val(struct gul_dev *gul_dev);
ssize_t gul_show_global_status(char *buf);
ssize_t gul_device_dump(struct gul_dev *dev, char *sbuf);
ssize_t wdog_device_dump(int id, char *buf);
ssize_t tti_device_dump(int id, char *buf);
ssize_t tvd_device_dump(int id, char *buf);
void gul_set_warmup_status(int id, enum gul_warmup_status);
int gul_reset_modem(int modem_id, enum wdog_modem_status modem_status);

int gul_modinfo_init(struct gul_dev *dev);
int gul_modinfo_exit(struct gul_dev *dev);
void gul_modinfo_get(struct gul_dev *gul_dev, modinfo_t *mi);

#if defined (LA1224) || defined(LA1238RDB)
char gul_get_host_board_rev(void);
#endif

ssize_t gul_ep_show_stats(void *stats_args, char *buf, void *dev);
void gul_ep_reset_stats(void *stats_args);
int gul_register_ep_stats_ops(struct gul_dev *gul_dev);
int gul_ipc_probe(struct gul_dev *gul_dev,
		int virq_count,	struct virq_evt_map *virq_map);
int gul_ipc_remove(struct gul_dev *gul_dev);
int gul_ipc_init(void);
int gul_ipc_exit(void);
int wdog_probe(struct gul_dev *gul_dev, int virq_count, struct virq_evt_map *virq_map);
int wdog_init(void);
int wdog_exit(void);
int tvd_init(void);
int tvd_exit(void);
int tvd_probe(struct gul_dev *gul_dev, int virq_count, struct virq_evt_map *virq_map);
int tvd_remove(struct gul_dev *gul_dev);
int cli_init(void);
int cli_exit(void);
int cli_probe(struct gul_dev *gul_dev, int virq_count, struct virq_evt_map *virq_map);
int cli_remove(struct gul_dev *gul_dev);
#endif
