#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2020-2022 NXP

#Add applicatin/exectuable directories here

APP_DIRS := tti wdog devmem tvd mpic modemgpio modem_info l3locking

ifeq ($(USIM_DRVR_ENABLED),1)
APP_DIRS += usim_app
endif

ifeq ($(HOST_CLI_ENABLED), 1)
APP_DIRS += cli
endif

CLEAN_APP_DIRS = $(patsubst %, %_clean, ${APP_DIRS})
INSTALL_APP_DIRS = $(patsubst %, %_install, ${APP_DIRS})

.PHONY: ${APP_DIRS} ${CLEAN_APP_DIRS} ${INSTALL_APP_DIRS}

#Add any CFLAGS that needs to be passed to driver
#EXTRA_CFLAGS += -g -DGUL_HOST_SW_VERSION="${VERSION_STRING}" -I${COMMON_DIR} -I${UAPI_DIR}
EXTRA_CFLAGS += -I${COMMON_DIR} -I${UAPI_DIR}

${APP_DIRS}:
	${MAKE} -C $@ all

${CLEAN_APP_DIRS}:
	${MAKE} -C $(patsubst %_clean, %, $@) clean

${INSTALL_APP_DIRS}:
	${MAKE} -C $(patsubst %_install, %, $@) install

all: ${APP_DIRS}

clean: ${CLEAN_APP_DIRS}

install: ${INSTALL_APP_DIRS}
