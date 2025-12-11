# SPDX-License-Identifier: BSD-3-Clause
#Copyright 2017 NXP

CFLAGS  += -Wall -Werror
LDFLAGS	 += -L${LIB_INSTALL_DIR}

INCLUDES += -I${API_DIR}/rfic
INCLUDES += -I${UAPI_DIR}

SRCS_TEST := cal_table_util.c
OBJS_TEST := $(SRCS_TEST:.c =.o)
BIN_TEST := cal_table_util

all: $(BIN_TEST)

$(BIN_TEST): ${OBJS_TEST}
	${CC} ${CFLAGS} ${LDFLAGS} -o $(BIN_TEST) ${OBJS_TEST} ${LIBS} $(INCLUDES)

%.o: %.c
	${CC} -c ${CFLAGS} ${INCLUDES}  $< -o $@

clean:
	rm -rf *.o cal_table_util

install:
	install -D $(BIN_TEST) ${BIN_INSTALL_DIR}/$(BIN_TEST)
