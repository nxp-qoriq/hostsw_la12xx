#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2021-2023 NXP

CFLAGS  += -fPIC -Wall -Wno-packed-bitfield-compat
LDFLAGS += -shared -L${LIB_INSTALL_DIR}

INCLUDES += -I${API_DIR}
INCLUDES += -I${UAPI_DIR}

SRC_LIB = usim_lib.c aka_functions.c usim_apdu.c usim_ioctl.c usim_event.c
OBJ_LIB = $(SRC_LIB:.c=.o)

OUT_LIB = libusim.so

USIM_LIB=-L./ -lusim

all:$(OUT_LIB)

$(OUT_LIB): $(OBJ_LIB)
	$(CC) $(CFLAGS) $(OBJ_LIB) -o $@ $(LDFLAGS)
	install -D $(OUT_LIB) ${LIB_INSTALL_DIR}/$(OUT_LIB)

clean:
	rm -rf *.o *.a
install:
	install -D $(OUT_LIB) ${LIB_INSTALL_DIR}/$(OUT_LIB)

