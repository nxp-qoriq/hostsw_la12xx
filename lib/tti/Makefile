#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2019,2022 NXP

CFLAGS  += -Wall -Werror -g -O -fPIC
LDFLAGS	 += -L${LIB_INSTALL_DIR}

INCLUDES += -I${API_DIR}
INCLUDES += -I${UAPI_DIR}
INCLUDES += -I${COMMON_HEADERS_DIR}

SRCS_TEST := gul_tti_lib.c
OBJ_LIB := $(SRCS_TEST:.c =.o)
OUT_LIB := libgultti.so

all: $(OUT_LIB)

$(OUT_LIB): ${OBJ_LIB}
	${CC} -shared -o $(OUT_LIB) ${OBJ_LIB} ${INCLUDES} ${CFLAGS} ${LDFLAGS}
	install -D $(OUT_LIB) ${LIB_INSTALL_DIR}/$(OUT_LIB)

%.o: %.c
	${CC} -c ${CFLAGS} ${INCLUDES}  $< -o $@

clean:
	rm -rf *.o *.so

install:
	install -D $(OUT_LIB) ${LIB_INSTALL_DIR}/$(OUT_LIB)
