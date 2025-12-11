# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2020-2022 NXP

CFLAGS  += -Wall -Werror -g -O -fPIC
LDFLAGS  += -L${LIB_INSTALL_DIR}

INCLUDES += -I${API_DIR}
INCLUDES += -I${UAPI_DIR}

OBJS_LIBS := gul_tvd_lib.o
OUT_LIB := libgultvd.a

all: $(OUT_LIB)

$(OUT_LIB): ${OBJS_LIBS}
	echo ${OBJ_LIB}
	${AR} rcs $(OUT_LIB) ${OBJS_LIBS}
	install -D $(OUT_LIB) ${LIB_INSTALL_DIR}/$(OUT_LIB)

%.o: %.c
	${CC} -c ${CFLAGS} ${INCLUDES}  $< -o $@

clean:
	rm -rf *.o *.a

install:
	install -D $(OUT_LIB) ${LIB_INSTALL_DIR}/$(OUT_LIB)
