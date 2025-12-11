# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2023 NXP

CFLAGS  += -Wall -Werror -I${COMMON_HEADERS_DIR}
LDFLAGS  += -L${LIB_INSTALL_DIR} -static

INCLUDES += -I${API_DIR}
INCLUDES += -I${UAPI_DIR}

SRCS_CLI := l3locking.c
OBJS_CLI := $(SRCS_CLI:.c =.o)
BIN_CLI := l3locking

all: $(BIN_CLI)

$(BIN_CLI): ${OBJS_CLI}
	${CC} ${CFLAGS} ${LDFLAGS} -o $(BIN_CLI) ${OBJS_CLI} $(INCLUDES) -lpthread

%.o: %.c
	${CC} -c ${CFLAGS} ${INCLUDES}  $< -o $@

clean:
	rm -rf *.o $(BIN_CLI)

install:
	install -D $(BIN_CLI) $(BIN_INSTALL_DIR)/$(BIN_CLI)
