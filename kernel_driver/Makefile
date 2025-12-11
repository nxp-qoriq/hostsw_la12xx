#SPDX-License-Identifier: GPL-2.0
#Copyright 2019, 2023 NXP

PWD=$(shell pwd)

ifndef KERNEL_DIR
$(error KERNEL_DIR is not set)
endif

ifndef COMMON_HEADERS_DIR
$(error COMMON_HEADERS_DIR is not set)
endif

ifdef KERNEL_CFG_PATH
include $(KERNEL_CFG_PATH)/.config
else
include $(KERNEL_DIR)/.config
endif

#Add any CFLAGS that needs to be passed to driver
EXTRA_CFLAGS += -g -DGUL_HOST_SW_VERSION="${VERSION_STRING}" -DVERSION=${GIT_VERSION} -I${COMMON_DIR} -I${UAPI_DIR}

ifeq (${BOOTROM_USE_EDMA}, 1)
EXTRA_CFLAGS += -DBOOTROM_USE_EDMA=1
endif
ifdef DEBUG
EXTRA_CFLAGS += -g -DDEBUG
endif

#DRV_SRCS := $(wildcard *.c)
#DRV_OBJS := $(patsubst %.c, %.o, ${DRV_SRCS})

obj-y += yami/
ifeq ($(LA1224),1)
obj-y += l3_cache_locking/
endif

all:
	$(MAKE) ARCH=$(ARCH) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) ARCH=$(ARCH) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -rf ${MODULE_INSTALL_DIR}/*.ko

install:
	install  -D  yami/yami.ko ${MODULE_INSTALL_DIR}/yami.ko
ifeq ($(LA1224),1)
	install  -D  l3_cache_locking/l3_cache_locking.ko ${MODULE_INSTALL_DIR}/l3_cache_locking.ko
endif
