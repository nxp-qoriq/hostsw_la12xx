# SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
#Copyright 2020 NXP

CFLAGS  += -Wall -Werror

INCLUDES += -I${UAPI_DIR}

SRCS_TEST := devmem.c
OBJS_TEST := $(SRCS_TEST:.c =.o)
BIN_TEST := devmem

all: $(BIN_TEST)

$(BIN_TEST): ${OBJS_TEST}
	${CC} ${CFLAGS} ${LDFLAGS} -o $(BIN_TEST) ${OBJS_TEST} $(INCLUDES)

%.o: %.c
	${CC} -c ${CFLAGS} ${INCLUDES}  $< -o $@

clean:
	rm -rf *.o $(BIN_TEST)

install:
	install -D $(BIN_TEST) ${BIN_INSTALL_DIR}/$(BIN_TEST)
