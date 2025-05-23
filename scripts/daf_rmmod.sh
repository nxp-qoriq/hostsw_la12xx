#!/bin/bash
#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2025 NXP

rmmod yami.ko
i2cset -y 0 0x66 0x45 0x40
i2cset -y 0 0x66 0x45 0x00
i2cget -y 0 0x66 0x45
sync
sleep 5
lspci
echo 1 > /sys/bus/pci/rescan;
lspci
