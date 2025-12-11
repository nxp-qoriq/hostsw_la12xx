#SPDX-License-Identifier: GPL-2.0
#Copyright 2020-2024 NXP

EXTRA_CFLAGS += -g -DGUL_HOST_SW_VERSION="${VERSION_STRING}" -DVERSION=${GIT_VERSION} -I${COMMON_DIR} -I${COMMON_HEADERS_DIR} -I${UAPI_DIR} -Wall -Werror

obj-m += yami.o

yami-objs := gul_base.o  gul_boot.o  gul_pci.o  gul_modinfo.o \
                gul_ipc.o gul_tti.o gul_sysfs.o gul_stats.o \
		gul_main_vspa.o gul_wdog.o gul_rf_dspi.o gul_tvd.o gul_modem_gpio.o

yami-objs += gul_cli.o

ifeq ($(DCS_DRVR_ENABLED), 1)
EXTRA_CFLAGS += -DDCS_DRVR_ENABLED
yami-objs += gul_dcs.o
endif

ifeq ($(RF_DRVR_ENABLED), 1)
EXTRA_CFLAGS += -DRF_DRVR_ENABLED
ifeq ($(RF_FR2_DRVR_ENABLED), 1)
EXTRA_CFLAGS += -DRF_FR2_DRVR_ENABLED
endif
yami-objs += gul_rfic.o
endif

ifeq ($(YUCCA_RF_DRVR_ENABLED), 1)
EXTRA_CFLAGS += -DYUCCA_RF_DRVR_ENABLED
yami-objs += gul_fr1_rfic.o
endif
ifeq ($(HAWK_DRVR_ENABLED), 1)
EXTRA_CFLAGS += -DHAWK_DRVR_ENABLED
yami-objs += gul_hawk.o
endif

ifeq ($(USIM_DRVR_ENABLED),1)
EXTRA_CFLAGS += -DUSIM_DRVR_ENABLED
yami-objs += gul_usim.o
endif

ifeq ($(ENABLE_ERRATA_A010867),1)
EXTRA_CFLAGS += -DERRATA_A010867
endif

ifeq ($(LA1224),1)
EXTRA_CFLAGS += -DLA1224
endif

ifeq ($(LA1238RDB),1)
EXTRA_CFLAGS += -DLA1238RDB
endif
