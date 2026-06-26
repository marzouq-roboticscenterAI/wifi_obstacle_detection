# Jetson Orin Nano CSI processor
#   make              -> TCP transport only (USB/Ethernet/WiFi)
#   make WITH_BT=1    -> also enable Bluetooth RFCOMM (needs libbluetooth-dev)
CC      ?= cc
CFLAGS  ?= -O3 -Wall -Wextra -std=c11 -march=native
LDLIBS  := -lm

WITH_BT ?= 0
ifeq ($(WITH_BT),1)
CFLAGS  += -DWITH_BT
LDLIBS  += -lbluetooth
endif

OBJ = main.o net.o dsp.o localize.o tracker.o infer_stub.o

csi_processor: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:       main.c net.h dsp.h localize.h tracker.h config.h ../common/csi_protocol.h
net.o:        net.c net.h ../common/csi_protocol.h
dsp.o:        dsp.c dsp.h config.h ../common/csi_protocol.h
localize.o:   localize.c localize.h dsp.h config.h
tracker.o:    tracker.c tracker.h
infer_stub.o: infer_stub.c infer.h dsp.h

clean:
	rm -f $(OBJ) csi_processor

.PHONY: clean
