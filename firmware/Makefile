#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2019, 2023 NXP

FIRMWARE_DIR := $(PWD)/firmware

all:
	@echo $(BSP_VERSION) > ${FIRMWARE_DIR}/bsp_version
install:
	install -D ${FIRMWARE_DIR}/geul-vspa0.eld ${FIRMWARE_INSTALL_DIR}/geul-vspa0.eld
	install -D ${FIRMWARE_DIR}/*.eld ${FIRMWARE_INSTALL_DIR}
	install -D ${FIRMWARE_DIR}/*.dmem ${FIRMWARE_INSTALL_DIR}
	install -D ${FIRMWARE_DIR}/*.pebmem ${FIRMWARE_INSTALL_DIR}
	install -D ${FIRMWARE_DIR}/*.bin ${FIRMWARE_INSTALL_DIR}
	install -D ${FIRMWARE_DIR}/*.mem ${FIRMWARE_INSTALL_DIR}
	install -D ${FIRMWARE_DIR}/bsp_version ${FIRMWARE_INSTALL_DIR}/bsp_version

clean:
	rm -f ${FIRMWARE_INSTALL_DIR}/*.elf
	rm -f ${FIRMWARE_INSTALL_DIR}/*.itb
	rm -f ${FIRMWARE_INSTALL_DIR}/*.eld
	rm -f ${FIRMWARE_INSTALL_DIR}/*.dmem
	rm -f ${FIRMWARE_INSTALL_DIR}/*.pebmem
	rm -f ${FIRMWARE_INSTALL_DIR}/*.bin
	rm -f ${FIRMWARE_INSTALL_DIR}/*.mem
	rm -f ${FIRMWARE_DIR}/bsp_version
	rm -f ${FIRMWARE_INSTALL_DIR}/bsp_version
