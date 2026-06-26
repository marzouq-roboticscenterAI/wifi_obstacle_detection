/* net.h -- receiver that accepts framed CSI records from the Pi collector over
 * either TCP (USB/Ethernet/WiFi) or Bluetooth RFCOMM. Both are reliable byte
 * streams, so the same magic-framed reassembly handles either. */
#ifndef NET_H
#define NET_H

#include "../common/csi_protocol.h"

/* Callback invoked once per fully-received CSI record. */
typedef void (*csi_cb_t)(const csi_wire_hdr_t *hdr, const int16_t *csi, void *user);

enum { NET_TCP = 0, NET_BT = 1 };

typedef struct {
    int transport;   /* NET_TCP or NET_BT          */
    int port;        /* TCP port    (NET_TCP)      */
    int channel;     /* RFCOMM channel (NET_BT)    */
} net_opts_t;

/* Listen (TCP or RFCOMM), accept one collector at a time, dispatch records to
 * cb. Blocks until signalled. Returns 0 on clean shutdown, -1 on setup error. */
int net_serve(const net_opts_t *opts, csi_cb_t cb, void *user);

extern volatile int g_net_run;

#endif /* NET_H */
