#SPDX-License-Identifier: GPL-2.0
#Copyright 2019-2026 NXP

CC              = $(CROSS_COMPILE)gcc
AR              = $(CROSS_COMPILE)ar
LD              = $(CROSS_COMPILE)ld

KERNEL_DIR ?=
DPDK_DIR ?=

ifdef SYSROOT_PATH
LDFLAGS := --sysroot=${SYSROOT_PATH}
CFLAGS := --sysroot=${SYSROOT_PATH}
endif

VERSION_STRING :=\"v3.x\"

INSTALL_DIR ?= ${PWD}/install
LIB_INSTALL_DIR := ${INSTALL_DIR}/usr/lib
BIN_INSTALL_DIR := ${INSTALL_DIR}/usr/bin
MODULE_INSTALL_DIR := ${INSTALL_DIR}/kernel_module/
CONFIG_INSTALL_DIR := ${INSTALL_DIR}/etc/
SCRIPTS_INSTALL_DIR := ${INSTALL_DIR}/usr/bin
FIRMWARE_INSTALL_DIR := ${INSTALL_DIR}/lib/firmware/

COMMON_DIR := ${PWD}/common/
API_DIR := ${PWD}/api/
UAPI_DIR := ${PWD}/uapi/
LIB_DIR := ${PWD}/lib/
GUL_DRV_HEADER_DIR := ${PWD}/kernel_driver/yami/

INCLUDES += -I${API_DIR}/
COMMON_INCLUDES += -I${COMMON_DIR} -I${UAPI_DIR}

CFLAGS += ${COMMON_INCLUDES}
HOST_CFLAGS += ${COMMON_INCLUDES}

GIT_VERSION := "$(shell git describe --abbrev=0 --tags --always 2>/dev/null || echo no-git)"

# Config Tweak handles
DEBUG ?= 1
BOOTROM_USE_EDMA ?= 1
LA1224 ?= 1
LA1238RDB ?= 0
RF_DRVR_ENABLED ?=1
HAWK_DRVR_ENABLED ?=0
RF_FR2_DRVR_ENABLED ?=0
DCS_DRVR_ENABLED ?=1
USIM_DRVR_ENABLED ?=1
HOST_CLI_ENABLED ?=1
ENABLE_ERRATA_A010867 ?=0

ifeq ($(LA1224),1)
YUCCA_RF_DRVR_ENABLED ?=1
export YUCCA_RF_DRVR_ENABLED
endif

ifeq ($(LA1238RDB),1)
YUCCA_RF_DRVR_ENABLED ?=1
export YUCCA_RF_DRVR_ENABLED
endif

ifeq ($(LA1238CPE),1)
YUCCA_RF_DRVR_ENABLED ?=1
export YUCCA_RF_DRVR_ENABLED
endif

ifeq ($(LA1224),1)
ENABLE_ERRATA_A010867 = 1
endif

export CC LIB_INSTALL_DIR BIN_INSTALL_DIR SCRIPTS_INSTALL_DIR MODULE_INSTALL_DIR \
	FIRMWARE_INSTALL_DIR
export CONFIG_INSTALL_DIR API_DIR LIB_DIR KERNEL_DIR COMMON_DIR UAPI_DIR GUL_DRV_HEADER_DIR
export INCLUDES CFLAGS VERSION_STRING LDFLAGS DPDK_DIR GIT_VERSION
export DEBUG BOOTROM_USE_EDMA RF_DRVR_ENABLED HAWK_DRVR_ENABLED LA1224 LA1224CPE LA1238RDB LA1238CPE
export RF_FR2_DRVR_ENABLED
export DCS_DRVR_ENABLED
export USIM_DRVR_ENABLED
export HOST_CLI_ENABLED
export ENABLE_ERRATA_A010867

ifdef DEBUG
CFLAGS += -DDEBUG
endif

ifeq ($(LA1224),1)
export BSP_VERSION := $(shell git describe --tags --abbrev=11 --dirty --match "la12*" --always 2>/dev/null || echo "snapshot")
CFLAGS += -DLA1224
endif

ifeq ($(LA1224CPE),1)
export BSP_VERSION := $(shell git describe --tags --abbrev=8 --dirty --match "CPEFR1_BSP*" --always 2>/dev/null || echo "snapshot")
CFLAGS += -DLA1224CPE
endif

ifeq ($(LA1238RDB),1)
export BSP_VERSION := $(shell git describe --tags --abbrev=8 --dirty --match "5GISCBSP*" --always 2>/dev/null || echo "snapshot")
CFLAGS += -DLA1238RDB
endif

ifeq ($(LA1238CPE),1)
export BSP_VERSION := $(shell git describe --tags --abbrev=8 --dirty --match "CPEFR1_BSP*" --always 2>/dev/null || echo "snapshot")
CFLAGS += -DLA1238CPE
endif
ifneq ($(BSP_VERSION),)
GIT_VERSION:=\"${BSP_VERSION}\"
endif

ifneq ($(GIT_VERSION),)
VERSION_STRING :=${GIT_VERSION}
endif
CFLAGS += -Wall -DGUL_HOST_SW_VERSION="${VERSION_STRING}" -DVERSION=${GIT_VERSION}

DIRS ?= lib app kernel_driver firmware scripts config
#DIRS ?= lib app kernel_driver firmware

CLEAN_DIRS = $(patsubst %, %_clean, ${DIRS})
INSTALL_DIRS = $(patsubst %, %_install, ${DIRS})

.PHONY: ${DIRS} ${CLEAN_DIRS} ${INSTALL_DIRS}

default: all

${DIRS}:
	${MAKE} -C $@ all

all: ${DIRS}

app: lib

${CLEAN_DIRS}:
	${MAKE} -C $(patsubst %_clean, %, $@) clean
	rm -rf ${LIB_INSTALL_DIR}/*;
	rm -rf ${BIN_INSTALL_DIR}/*;
	rm -rf ${SCRIPTS_INSTALL_DIR}/*;
	rm -rf ${CONFIG_INSTALL_DIR}/*;
	rm -rf ${INSTALL_DIR};

${INSTALL_DIRS}:
	${MAKE} -j1 -C $(patsubst %_install, %, $@) install

install: ${INSTALL_DIRS}

clean: ${CLEAN_DIRS}
