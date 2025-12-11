# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2019-2021 NXP

CFLAGS  += -Wall -Werror
LDFLAGS  += -L${LIB_INSTALL_DIR} -static

INCLUDES += -I${API_DIR}
INCLUDES += -I${UAPI_DIR}

SRCS_TEST := gul_wdog_app.c
OBJS_TEST := $(SRCS_TEST:.c =.o)
BIN_TEST := gul_wdog_testapp
LIB_FILE := gulwdog

all: $(BIN_TEST)

$(BIN_TEST): ${OBJS_TEST}
	${CC} ${CFLAGS} ${LDFLAGS} -o $(BIN_TEST) ${OBJS_TEST} -l${LIB_FILE}  $(INCLUDES)

%.o: %.c
	${CC} -c ${CFLAGS} ${INCLUDES}  $< -o $@

clean:
	 rm -rf *.o $(BIN_TEST)

install:
	install -D $(BIN_TEST) ${BIN_INSTALL_DIR}/$(BIN_TEST)

