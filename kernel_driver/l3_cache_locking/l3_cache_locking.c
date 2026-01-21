/* SPDX-License-Identifier: GPL-2.0
   Copyright 2023-2026 NXP
*/

/*
 * Enable l3 cache locking configuration at EL1 level.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/memblock.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <l3locking_ioctl.h>

#if !defined(__aarch64__)
#error Module can only be compiled on ARM64 machines.
#endif

#define SIP_SMC64_CACHE_LOCKING     0xC200FF20
#define SIP_SMC64_CACHE_WAYS_SHIFT  16

static unsigned int ways = 0;
static unsigned long base0 = 0x0;
static unsigned long base1 = 0x0;
static unsigned long base2 = 0x0;
static unsigned long base3 = 0x0;

/*
 * This struct defines the way the registers are stored
 * on the stack during an exception.
 */
struct smc_regs {
    unsigned long elr;
    unsigned long regs[31];
};

/* file ops */
static long
cache_locking_ioctl(struct file *f, unsigned int cmd, unsigned long arg);

static void smc_call(struct smc_regs *args)
{
    asm volatile(
        "ldr x0, %0\n"
        "ldr x1, %1\n"
        "ldr x2, %2\n"
        "ldr x3, %3\n"
        "ldr x4, %4\n"
        "ldr x5, %5\n"
        "ldr x6, %6\n"
        "smc    #0\n"
        "str x0, %0\n"
        "str x1, %1\n"
        "str x2, %2\n"
        "str x3, %3\n"
        : "+m" (args->regs[0]), "+m" (args->regs[1]),
          "+m" (args->regs[2]), "+m" (args->regs[3])
        : "m" (args->regs[4]), "m" (args->regs[5]),
          "m" (args->regs[6])
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
          "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
          "x16", "x17");
}

static int is_valid(unsigned long long phyaddr, size_t size) {
    /* need linux kernel export symbol "memblock_is_region_memory" */
    if(memblock_is_region_memory(phyaddr, size) || (phyaddr == 0)) {
        return 1;
    } else {
        return 0;
    }
}

static int enable_l3_cache_locking(unsigned int ways, unsigned long base0, unsigned long base1, unsigned long base2, unsigned long base3) {

    struct smc_regs regs;
    size_t size1 = 0, size2 = 0;

    if(ways != 1 && ways != 2 && ways != 4 && ways != 8 && ways != 12) {
        printk("Invalid configuration, number of ways must be one of {1,2,4,8,12}\n");
        return -1;
    }

    if(ways <= 4) {
        size1 = 0x80000;
        size2 = 0x80000;
    } else if(ways == 8) {
        size1 = 0x100000;
        size2 = 0x100000;
    } else if(ways == 12) {
        size1 = 0x100000;
        size2 = 0x200000;
    }

    if(ways >= 1) {
        if(!is_valid(base0, size1)) {
            printk("Invalid configuration, region 0 (address:0x%lx, size:0x%lx) invalid.\n", base0, size1);
            return -1;
        }
    }

    if(ways >= 2) {
        if(!is_valid(base1, size1)) {
            printk("Invalid configuration, region 1 (address:0x%lx, size:0x%lx) invalid.\n", base1, size1);
            return -1;
        }
    }

    if(ways >= 4) {
        if(!is_valid(base2, size2)) {
            printk("Invalid configuration, region 2 (address:0x%lx, size:0x%lx) invalid.\n", base2, size2);
            return -1;
        }

        if(!is_valid(base3, size2)) {
            printk("Invalid configuration, region 3 (address:0x%lx, size:0x%lx) invalid.\n", base3, size2);
            return -1;
        }
    }

    regs.regs[0] = SIP_SMC64_CACHE_LOCKING | (ways << SIP_SMC64_CACHE_WAYS_SHIFT);
    regs.regs[1] = base0;
    regs.regs[2] = base1;
    regs.regs[3] = base2;
    regs.regs[4] = base3;
    smc_call(&regs);

    if(regs.regs[1] == 0) {
        printk("L3 Cache Locking Configured Successfully!\n");
        return 0;
    } else {
        printk("L3 Cache Locking Configured Failed:%lu!\n", regs.regs[1]);
        return -1;
    }

}

static int disable_l3_cache_locking(int ways) {

    struct smc_regs regs;

    regs.regs[0] = SIP_SMC64_CACHE_LOCKING | (ways << SIP_SMC64_CACHE_WAYS_SHIFT);
    regs.regs[1] = 0;
    regs.regs[2] = 0;
    regs.regs[3] = 0;
    regs.regs[4] = 0;
    smc_call(&regs);

    if(regs.regs[1] == 0) {
        printk("L3 Cache Unlocking Configured Successfully!\n");
        return 0;
    } else {
        printk("L3 Cache Unlocking Configured Unsuccessfully:%lu!\n", regs.regs[1]);
        return -1;
    }

}

static int cache_locking_open(struct inode *inode, struct file *filp)
{
    filp->private_data = NULL;
    return 0;
}

static int cache_locking_release(struct inode *inode, struct file *filp)
{
    filp->private_data = NULL;
    return 0;
}

static long
cache_locking_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    int err = 0;
    struct cache_locking_data data;
    int ret = 0;

    if (_IOC_TYPE(cmd) != MAGIC)
        return -ENOTTY;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
#else
    err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
#endif
    if (err)
        return -EFAULT;

    switch (cmd) {
    case CACHE_LOCKING_IOC: /* enable/disable pmccntr */
        if (copy_from_user(&data, (void *)arg, _IOC_SIZE(cmd))) {
            ret = -EIO;
            break;
        }
        if (data.enable)
            ret = enable_l3_cache_locking(data.ways, data.base0, data.base1, data.base2, data.base3);
        else
            ret = disable_l3_cache_locking(data.ways);
        break;
    default:
        ret = -ENOTTY;
    }

    return ret;
}

static const struct file_operations cache_locking_fops = {
    .owner      = THIS_MODULE,
    .open       = cache_locking_open,
    .release    = cache_locking_release,
    .unlocked_ioctl = cache_locking_ioctl,
};

static struct miscdevice cache_locking_dev = {
    .minor      = MISC_DYNAMIC_MINOR,
    .name       = "l3locking",
    .fops       = &cache_locking_fops,
};

static int __init
init(void)
{

    int ret = 0;

    ret = misc_register(&cache_locking_dev);
    if (ret) {
        printk(KERN_ERR "l3 cache locking - misc_register failed, err = %d\n", ret);
        return ret;
    }

    if(ways != 0) {
        ret = enable_l3_cache_locking(ways, base0, base1, base2, base3);
    }

    return ret;
}

static void __exit
fini(void)
{
    misc_deregister(&cache_locking_dev);

    if(ways != 0)
        disable_l3_cache_locking(ways);
}

module_param(ways, uint, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(ways, "Number of locked ways");
module_param(base0, ulong, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(base0, "Region 0 base address");
module_param(base1, ulong, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(base1, "Region 1 base address");
module_param(base2, ulong, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(base2, "Region 2 base address");
module_param(base3, ulong, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(base3, "Region 3 base address");

MODULE_DESCRIPTION("Enables l3 cache locking configuration at EL1 level.");
MODULE_LICENSE("GPL");
module_init(init);
module_exit(fini);
