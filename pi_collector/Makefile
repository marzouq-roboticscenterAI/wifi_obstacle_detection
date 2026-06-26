# Raspberry Pi 5 CSI collector
#   make              -> TCP transport only (USB/Ethernet/WiFi)
#   make WITH_BT=1    -> also enable Bluetooth RFCOMM (needs libbluetooth-dev)
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
LDLIBS  := -lpcap

WITH_BT ?= 0
ifeq ($(WITH_BT),1)
CFLAGS  += -DWITH_BT
LDLIBS  += -lbluetooth
endif

csi_collector: csi_collector.c ../common/csi_protocol.h
	$(CC) $(CFLAGS) -o $@ csi_collector.c $(LDLIBS)

clean:
	rm -f csi_collector

.PHONY: clean
