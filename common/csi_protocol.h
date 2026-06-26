/* csi_protocol.h - wire format for the Pi(collector) -> Jetson(processor) link.
 *
 * Transport is a plain TCP stream (works identically over the USB CDC-Ethernet
 * gadget link or over WiFi). Both the Pi 5 and the Jetson Orin Nano are
 * little-endian ARM, so we serialize structs in native little-endian and avoid
 * per-field byte swapping. If you ever bridge to a big-endian host, add htole*.
 *
 * Each record on the wire is:  csi_wire_hdr_t  followed by  n_sub * 2 * int16
 * (interleaved real, imag) exactly as the BCM43455c0 emits them.
 */
#ifndef CSI_PROTOCOL_H
#define CSI_PROTOCOL_H

#include <stdint.h>

/* ASCII "CSI1" read as little-endian uint32. Used to resync the TCP stream. */
#define CSI_WIRE_MAGIC   0x31495343u

/* Hard upper bound: 80 MHz channel = 256 subcarriers. */
#define CSI_MAX_SUBCARRIERS 256

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;        /* CSI_WIRE_MAGIC                                  */
    uint64_t t_ns;         /* Pi capture timestamp, nanoseconds (CLOCK_REALTIME based, from pcap ts) */
    uint8_t  src_mac[6];   /* transmitter MAC -> identifies the link/anchor   */
    uint16_t seq;          /* 802.11 sequence number of the triggering frame  */
    uint16_t chanspec;     /* Broadcom chanspec used during capture           */
    uint8_t  core;         /* RX core    (low 3 bits of nexmon core/nss field)*/
    uint8_t  nss;          /* spatial stream (next 3 bits)                    */
    int8_t   rssi;         /* RSSI if available, else 0 (new nexmon fmt = 0)  */
    uint8_t  _pad;         /* keep 2-byte alignment of n_sub                  */
    uint16_t n_sub;        /* number of complex subcarriers that follow       */
    /* int16 csi[n_sub*2]  : real,imag,real,imag,...  (little-endian)         */
} csi_wire_hdr_t;
#pragma pack(pop)

/* Total bytes of one full record given the subcarrier count. */
static inline uint32_t csi_record_size(uint16_t n_sub) {
    return (uint32_t)sizeof(csi_wire_hdr_t) + (uint32_t)n_sub * 4u;
}

#endif /* CSI_PROTOCOL_H */
