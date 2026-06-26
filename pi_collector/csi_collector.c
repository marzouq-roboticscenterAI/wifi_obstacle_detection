/* csi_collector.c  --  Raspberry Pi 5 side.
 *
 * Captures Nexmon CSI UDP frames (dst port 5500) off the WiFi interface with
 * libpcap, parses the BCM43455c0 record, repackages it into the compact
 * csi_wire_hdr_t format, and streams it over TCP to the Jetson Orin Nano.
 *
 * The Pi does ZERO signal processing -- it is a pure capture/forward node so
 * the heavy lifting (DSP, multilateration, optional neural inference) happens
 * on the Jetson's CPU/GPU.
 *
 * Prereqs on the Pi:
 *   - Nexmon CSI firmware installed and a CSI capture configured, e.g.:
 *       mcp -C 1 -N 1 -c 36/80 -m <your-mac-filter>   # makecsiparams
 *       nexutil -Iwlan0 -s500 -b -l34 -v<base64-params>
 *       ifconfig wlan0 up
 *     (See README for the full Pi 5 / non-16k-page kernel procedure.)
 *   - Some transmitter(s) must be sending frames the Pi receives (ping flood
 *     from each anchor). Each distinct transmitter MAC becomes one "link".
 *   - sudo apt install libpcap-dev
 *
 * Build:  make            (in this directory)
 * Run:    sudo ./csi_collector -i wlan0 -H 192.168.55.1 -p 9999
 *         (192.168.55.1 = Jetson over the USB CDC-Ethernet gadget link)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pcap.h>

#ifdef WITH_BT
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#endif

#include "../common/csi_protocol.h"

/* ---- Nexmon CSI record layout (new format, kernel 5.x / Pi 5) ------------ *
 *   off 0  : uint32  magic = 0x11111111
 *   off 4  : uint8[6] source MAC
 *   off 10 : uint16  seq
 *   off 12 : uint16  core/nss  (bits0-2 core, bits3-5 nss)
 *   off 14 : uint16  chanspec
 *   off 16 : uint16  chip version
 *   off 18 : int16[2*n_sub]  CSI (real,imag interleaved)
 * ------------------------------------------------------------------------- */
#define NEXMON_MAGIC      0x11111111u
#define NEXMON_HDR_BYTES  18

static volatile sig_atomic_t g_run = 1;
static void on_sig(int s){ (void)s; g_run = 0; }

/* link configuration (set from argv, used by the (re)connect path) */
enum { LINK_TCP = 0, LINK_BT = 1 };
static int   g_transport = LINK_TCP;
static const char *g_host = "192.168.55.1";
static int   g_port = 9999;
static const char *g_bt_addr = NULL;   /* Jetson Bluetooth MAC for RFCOMM */
static int   g_channel = 1;

/* Connect (blocking, with retry) a TCP socket to the Jetson. */
static int connect_jetson(const char *host, int port)
{
    for (;;) {
        if (!g_run) return -1;
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { perror("socket"); sleep(1); continue; }

        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons((uint16_t)port);
        if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
            fprintf(stderr, "bad host %s\n", host); close(fd); return -1;
        }
        if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0) {
            fprintf(stderr, "[collector] connected to Jetson %s:%d\n", host, port);
            return fd;
        }
        perror("connect"); close(fd); sleep(1);
    }
}

#ifdef WITH_BT
/* Connect (blocking, with retry) over Bluetooth RFCOMM to the Jetson. */
static int connect_bt(const char *addr, int channel)
{
    for (;;) {
        if (!g_run) return -1;
        int fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
        if (fd < 0) { perror("socket(RFCOMM)"); sleep(1); continue; }

        struct sockaddr_rc sa = {0};
        sa.rc_family = AF_BLUETOOTH;
        sa.rc_channel = (uint8_t)channel;
        if (str2ba(addr, &sa.rc_bdaddr) != 0) {
            fprintf(stderr, "bad bluetooth addr %s\n", addr); close(fd); return -1;
        }
        if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0) {
            fprintf(stderr, "[collector] connected to Jetson %s (RFCOMM ch %d)\n",
                    addr, channel);
            return fd;
        }
        perror("connect(RFCOMM) (paired? processor running?)");
        close(fd); sleep(2);
    }
}
#endif /* WITH_BT */

/* Dispatch to the configured transport. */
static int link_connect(void)
{
    if (g_transport == LINK_BT) {
#ifdef WITH_BT
        return connect_bt(g_bt_addr, g_channel);
#else
        fprintf(stderr, "built without Bluetooth support. Rebuild with: make WITH_BT=1\n");
        return -1;
#endif
    }
    return connect_jetson(g_host, g_port);
}

/* Send exactly len bytes; return 0 ok, -1 on broken pipe. */
static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t*)buf;
    while (len) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR)) continue;
            return -1;
        }
        p += n; len -= (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *iface = "wlan0";
    int   decim       = 1;     /* forward 1 of every <decim> frames per link */
    int   opt;

    while ((opt = getopt(argc, argv, "i:t:H:p:B:C:d:h")) != -1) {
        switch (opt) {
            case 'i': iface = optarg; break;
            case 't':
                if      (!strcmp(optarg, "bt"))  g_transport = LINK_BT;
                else if (!strcmp(optarg, "tcp")) g_transport = LINK_TCP;
                else { fprintf(stderr, "transport must be tcp|bt\n"); return 1; }
                break;
            case 'H': g_host    = optarg; break;
            case 'p': g_port    = atoi(optarg); break;
            case 'B': g_bt_addr = optarg; break;
            case 'C': g_channel = atoi(optarg); break;
            case 'd': decim = atoi(optarg); if (decim < 1) decim = 1; break;
            default:
                fprintf(stderr,
                    "usage: %s [-i iface] [-t tcp|bt]\n"
                    "          TCP: [-H jetson_ip] [-p port]\n"
                    "          BT : [-B jetson_bdaddr] [-C rfcomm_channel]\n"
                    "          [-d decimate]  (drop all but 1/N frames per link, for Bluetooth)\n",
                    argv[0]);
                return 1;
        }
    }

    if (g_transport == LINK_BT && !g_bt_addr) {
        fprintf(stderr, "bluetooth transport needs -B <jetson_bdaddr>\n");
        return 1;
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    /* --- open the capture interface --------------------------------------- */
    char errbuf[PCAP_ERRBUF_SIZE] = {0};
    pcap_t *pc = pcap_open_live(iface, 2048 /*snaplen*/, 1 /*promisc*/,
                                100 /*ms*/, errbuf);
    if (!pc) { fprintf(stderr, "pcap_open_live(%s): %s\n", iface, errbuf); return 1; }

    if (pcap_datalink(pc) != DLT_EN10MB) {
        fprintf(stderr, "warning: link type is not Ethernet (%d); "
                        "nexmon CSI frames are normally EN10MB.\n",
                        pcap_datalink(pc));
    }

    struct bpf_program bpf;
    if (pcap_compile(pc, &bpf, "udp and dst port 5500", 1,
                     PCAP_NETMASK_UNKNOWN) != 0 ||
        pcap_setfilter(pc, &bpf) != 0) {
        fprintf(stderr, "bpf filter: %s\n", pcap_geterr(pc));
        pcap_close(pc); return 1;
    }
    pcap_freecode(&bpf);

    int jfd = link_connect();
    if (jfd < 0) { pcap_close(pc); return 1; }

    /* per-link decimation counters (parallel to the MAC table below) */
    uint8_t  dmac[CSI_MAX_SUBCARRIERS][6];   /* reuse generous bound as #links */
    unsigned dcnt[CSI_MAX_SUBCARRIERS] = {0};
    int      ndlinks = 0;

    /* reusable output buffer: header + max CSI payload */
    uint8_t out[sizeof(csi_wire_hdr_t) + CSI_MAX_SUBCARRIERS * 4];

    unsigned long forwarded = 0, dropped = 0;

    while (g_run) {
        struct pcap_pkthdr *ph;
        const u_char *pkt;
        int rc = pcap_next_ex(pc, &ph, &pkt);
        if (rc == 0)  continue;           /* timeout */
        if (rc < 0) { fprintf(stderr, "pcap: %s\n", pcap_geterr(pc)); break; }

        const uint8_t *p   = (const uint8_t*)pkt;
        uint32_t       len = ph->caplen;

        /* Ethernet(14) -> IPv4 -> UDP(8) -> nexmon payload. Parse IHL since the
         * IP header length can vary. */
        if (len < 14 + 20 + 8) { dropped++; continue; }
        uint16_t ethertype = (uint16_t)((p[12] << 8) | p[13]);
        if (ethertype != 0x0800) { dropped++; continue; }      /* not IPv4 */

        const uint8_t *ip = p + 14;
        uint32_t ihl = (ip[0] & 0x0F) * 4u;
        if (ihl < 20) { dropped++; continue; }
        if (ip[9] != 17) { dropped++; continue; }              /* not UDP */

        const uint8_t *udp = ip + ihl;
        uint32_t off_payload = 14 + ihl + 8;
        if (len <= off_payload + NEXMON_HDR_BYTES) { dropped++; continue; }
        (void)udp;

        const uint8_t *nx = p + off_payload;
        uint32_t nxlen = len - off_payload;

        uint32_t magic;
        memcpy(&magic, nx + 0, 4);          /* little-endian on ARM */
        if (magic != NEXMON_MAGIC) { dropped++; continue; }

        uint32_t csi_bytes = nxlen - NEXMON_HDR_BYTES;
        uint16_t n_sub = (uint16_t)(csi_bytes / 4u);
        if (n_sub == 0 || n_sub > CSI_MAX_SUBCARRIERS) { dropped++; continue; }

        uint16_t core_nss;
        memcpy(&core_nss, nx + 12, 2);

        /* per-link frame decimation -- crucial for the low-bandwidth BT link */
        if (decim > 1) {
            const uint8_t *mac = nx + 4;
            int li = -1;
            for (int i = 0; i < ndlinks; i++)
                if (memcmp(dmac[i], mac, 6) == 0) { li = i; break; }
            if (li < 0 && ndlinks < CSI_MAX_SUBCARRIERS) {
                li = ndlinks++; memcpy(dmac[li], mac, 6); dcnt[li] = 0;
            }
            if (li >= 0 && (dcnt[li]++ % (unsigned)decim) != 0) continue;
        }

        csi_wire_hdr_t *h = (csi_wire_hdr_t*)out;
        h->magic    = CSI_WIRE_MAGIC;
        h->t_ns     = (uint64_t)ph->ts.tv_sec * 1000000000ull
                    + (uint64_t)ph->ts.tv_usec * 1000ull;
        memcpy(h->src_mac, nx + 4, 6);
        memcpy(&h->seq,      nx + 10, 2);
        memcpy(&h->chanspec, nx + 14, 2);
        h->core = (uint8_t)(core_nss & 0x7);
        h->nss  = (uint8_t)((core_nss >> 3) & 0x7);
        h->rssi = 0;
        h->_pad = 0;
        h->n_sub = n_sub;

        /* CSI samples pass through untouched (int16 real/imag, little-endian) */
        memcpy(out + sizeof(*h), nx + NEXMON_HDR_BYTES, (size_t)n_sub * 4u);

        if (send_all(jfd, out, sizeof(*h) + (size_t)n_sub * 4u) != 0) {
            fprintf(stderr, "[collector] link down, reconnecting...\n");
            close(jfd);
            jfd = link_connect();
            if (jfd < 0) break;
            continue;
        }
        if ((++forwarded % 2000) == 0)
            fprintf(stderr, "[collector] forwarded=%lu dropped=%lu n_sub=%u\n",
                    forwarded, dropped, n_sub);
    }

    fprintf(stderr, "[collector] exiting (forwarded=%lu dropped=%lu)\n",
            forwarded, dropped);
    if (jfd >= 0) close(jfd);
    pcap_close(pc);
    return 0;
}
