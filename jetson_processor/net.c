/* net.c -- TCP receiver + stream reassembly for CSI records.
 *
 * The Pi sends a continuous byte stream of [csi_wire_hdr_t][csi samples]...
 * TCP gives no message boundaries, so we read the fixed header first, validate
 * the magic (resyncing byte-by-byte if it drifts), then read the variable CSI
 * payload, then fire the callback.
 */
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#ifdef WITH_BT
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#endif

volatile int g_net_run = 1;

/* Read exactly n bytes into buf. Returns 0 ok, -1 on close/error. */
static int read_all(int fd, void *buf, size_t n)
{
    uint8_t *p = (uint8_t*)buf;
    while (n) {
        ssize_t r = recv(fd, p, n, 0);
        if (r == 0)  return -1;                  /* peer closed */
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        p += r; n -= (size_t)r;
    }
    return 0;
}

/* Pull bytes until the 4-byte magic is found, leaving the stream positioned
 * just after a valid magic. Returns 0 ok, -1 on close. */
static int resync_magic(int fd)
{
    uint32_t w = 0;
    int have = 0;
    for (;;) {
        uint8_t b;
        if (read_all(fd, &b, 1) != 0) return -1;
        /* little-endian rolling window */
        w = (w >> 8) | ((uint32_t)b << 24);
        if (++have >= 4 && w == CSI_WIRE_MAGIC) return 0;
    }
}

static void handle_conn(int cfd, csi_cb_t cb, void *user)
{
    int one = 1;
    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    csi_wire_hdr_t hdr;
    int16_t csi[CSI_MAX_SUBCARRIERS * 2];
    int synced = 0;

    while (g_net_run) {
        if (!synced) {
            /* Read header: first try to read it whole; magic is field 0. */
            if (read_all(cfd, &hdr, sizeof(hdr)) != 0) break;
            if (hdr.magic != CSI_WIRE_MAGIC) {
                /* Lost framing: find next magic, then read the *rest* of a header. */
                if (resync_magic(cfd) != 0) break;
                hdr.magic = CSI_WIRE_MAGIC;
                if (read_all(cfd, (uint8_t*)&hdr + 4, sizeof(hdr) - 4) != 0) break;
            }
        } else {
            if (read_all(cfd, &hdr, sizeof(hdr)) != 0) break;
            if (hdr.magic != CSI_WIRE_MAGIC) { synced = 0; continue; }
        }

        if (hdr.n_sub == 0 || hdr.n_sub > CSI_MAX_SUBCARRIERS) {
            synced = 0;                          /* bogus -> resync */
            continue;
        }
        if (read_all(cfd, csi, (size_t)hdr.n_sub * 4u) != 0) break;

        synced = 1;
        cb(&hdr, csi, user);
    }
}

static int make_listen_tcp(int port)
{
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return -1; }
    int one = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((uint16_t)port);
    if (bind(sfd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        perror("bind"); close(sfd); return -1;
    }
    if (listen(sfd, 1) != 0) { perror("listen"); close(sfd); return -1; }
    fprintf(stderr, "[processor] listening (TCP) on 0.0.0.0:%d\n", port);
    return sfd;
}

#ifndef WITH_BT
static int make_listen_rfcomm(int channel)
{
    (void)channel;
    fprintf(stderr, "[processor] built without Bluetooth support.\n"
                    "            Rebuild with:  make WITH_BT=1\n");
    return -1;
}
#else
static int make_listen_rfcomm(int channel)
{
    int sfd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (sfd < 0) { perror("socket(RFCOMM)"); return -1; }
    struct sockaddr_rc loc = {0};
    loc.rc_family = AF_BLUETOOTH;
    loc.rc_bdaddr = *BDADDR_ANY;            /* bind on the local adapter */
    loc.rc_channel = (uint8_t)channel;
    if (bind(sfd, (struct sockaddr*)&loc, sizeof(loc)) != 0) {
        perror("bind(RFCOMM)"); close(sfd); return -1;
    }
    if (listen(sfd, 1) != 0) { perror("listen(RFCOMM)"); close(sfd); return -1; }
    fprintf(stderr, "[processor] listening (Bluetooth RFCOMM) on channel %d\n",
            channel);
    fprintf(stderr, "[processor] make this adapter discoverable/pairable:\n"
                    "            bluetoothctl -> power on; discoverable on; pairable on; agent on\n");
    return sfd;
}
#endif /* WITH_BT */

int net_serve(const net_opts_t *opts, csi_cb_t cb, void *user)
{
    int sfd = (opts->transport == NET_BT)
            ? make_listen_rfcomm(opts->channel)
            : make_listen_tcp(opts->port);
    if (sfd < 0) return -1;

    while (g_net_run) {
        int cfd;
        if (opts->transport == NET_BT) {
#ifdef WITH_BT
            struct sockaddr_rc peer; socklen_t plen = sizeof(peer);
            cfd = accept(sfd, (struct sockaddr*)&peer, &plen);
            if (cfd >= 0) {
                char a[18]; ba2str(&peer.rc_bdaddr, a);
                fprintf(stderr, "[processor] collector connected (BT %s)\n", a);
            }
#else
            cfd = -1;   /* unreachable: make_listen_rfcomm already failed */
#endif
        } else {
            struct sockaddr_in peer; socklen_t plen = sizeof(peer);
            cfd = accept(sfd, (struct sockaddr*)&peer, &plen);
            if (cfd >= 0) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
                fprintf(stderr, "[processor] collector connected from %s\n", ip);
            }
        }
        if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); break; }

        handle_conn(cfd, cb, user);
        close(cfd);
        fprintf(stderr, "[processor] collector disconnected\n");
    }

    close(sfd);
    return 0;
}
