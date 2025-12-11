/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2023 NXP
 */

#ifndef __L3_LOCKING_IOCTL__
#define __L3_LOCKING_IOCTL__

#include <linux/ioctl.h>

#define MAGIC    0xF8

#define CACHE_LOCKING_IOC \
    _IOW(MAGIC, 0, struct cache_locking_data)


struct cache_locking_data {
    int enable;
    int ways;
    unsigned long base0;
    unsigned long base1;
    unsigned long base2;
    unsigned long base3;
};

#endif
