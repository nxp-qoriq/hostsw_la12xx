#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2019-2022 NXP

CFLAGS  += -Wall -Werror
LDFLAGS  += -L${LIB_INSTALL_DIR}

INCLUDES += -I${LIB_DIR}/usim_library/

SRCS_TEST := test_app.c
OBJS_TEST := $(SRCS_TEST:.c =.o)
BIN_TEST := usim_testapp
SO_FILE := usim

all: $(BIN_TEST)

$(BIN_TEST): ${OBJS_TEST}
	${CC} ${CFLAGS} ${LDFLAGS} -o $(BIN_TEST) ${OBJS_TEST} -l${SO_FILE}  $(INCLUDES)

%.o: %.c
	${CC} -c ${CFLAGS} ${INCLUDES}  $< -o $@

clean:
	rm -rf *.o $(BIN_TEST)

install:
	install -D $(BIN_TEST) ${BIN_INSTALL_DIR}/$(BIN_TEST)
