# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2022-2023 NXP

CFLAGS  += -Wall -Werror -I${COMMON_HEADERS_DIR}
LDFLAGS  += -L${LIB_INSTALL_DIR} -static

INCLUDES += -I${API_DIR}
INCLUDES += -I${UAPI_DIR}

SRCS_CLI := gul_cli.c physical_mem.c
SRCS_DPD := clpoc_refapp.c physical_mem.c
OBJS_CLI := $(SRCS_CLI:.c =.o)
BIN_CLI := gul_cli
OBJS_DPD := $(SRCS_DPD:.c = .o)
BIN_DPD := clpoc_refapp
LIB_FILE := gulcli
BIN_REFAPP := gul_refapp

all: $(BIN_CLI) $(BIN_DPD)

$(BIN_CLI): ${OBJS_CLI}
	${CC} ${CFLAGS} ${LDFLAGS} -o $(BIN_CLI) ${OBJS_CLI} -l${LIB_FILE}  $(INCLUDES) -lpthread

$(BIN_DPD): ${OBJS_DPD}
	${CC} ${CFLAGS} ${LDFLAGS} -o $(BIN_DPD) ${OBJS_DPD} -l${LIB_FILE}  $(INCLUDES) -lpthread

%.o: %.c
	${CC} -c ${CFLAGS} ${INCLUDES}  $< -o $@

clean:
	rm -rf *.o $(BIN_CLI) $(BIN_REFAPP) $(BIN_DPD)

install:
	install -D $(BIN_CLI) $(BIN_INSTALL_DIR)/$(BIN_CLI)
	install -D $(BIN_DPD) $(BIN_INSTALL_DIR)/$(BIN_DPD)
	ln -fsr $(BIN_INSTALL_DIR)/$(BIN_CLI) $(BIN_INSTALL_DIR)/$(BIN_REFAPP)
