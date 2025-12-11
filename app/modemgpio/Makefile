#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2021 NXP

CFLAGS  += -Wall -Werror
LDFLAGS  += -L${LIB_INSTALL_DIR}

INCLUDES += -I${API_DIR}
INCLUDES += -I${UAPI_DIR}

SRCS_TEST := modem_gpio_app.c
OBJS_TEST := $(SRCS_TEST:.c =.o)
BIN_TEST := modem_gpio_app
SO_FILE := gulmodemgpio

all: $(BIN_TEST)

$(BIN_TEST): ${OBJS_TEST}
	${CC} ${CFLAGS} ${LDFLAGS} -o $(BIN_TEST) ${OBJS_TEST} -l${SO_FILE}  $(INCLUDES)

%.o: %.c
	${CC} -c ${CFLAGS} ${INCLUDES}  $< -o $@

clean:
	 rm -rf *.o $(BIN_TEST)

install:
	install -D $(BIN_TEST) ${BIN_INSTALL_DIR}/$(BIN_TEST)

