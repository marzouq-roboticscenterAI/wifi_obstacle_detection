/* main.c -- Jetson Orin Nano processor.
 *
 * Receives CSI from the Pi, runs the DSP + localization + Kalman tracker, and
 * emits one JSON line per processing tick on stdout (pipe it to a UI, a logger,
 * ROS bridge, etc.). All processing is single-threaded and driven off the CSI
 * arrival loop -- localization runs every PROC_PERIOD_MS of wall clock.
 *
 * Build: make    Run: ./csi_processor -p 9999
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#include "net.h"
#include "dsp.h"
#include "localize.h"
#include "tracker.h"
#include "config.h"

typedef struct {
    dsp_t      dsp;
    tracker_t  trk;
    fix_t      last_fix;
    uint64_t   next_proc_ns;
} app_t;

/* Monotonic clock for all internal timing (tick gating, decay, tracker dt).
 * Independent of wall-clock / NTP, so an offline Pi or a clock step can't
 * disturb tracking. */
static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Wall clock, used only for the human-readable timestamp in the JSON log. */
static uint64_t wall_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void emit(app_t *a, uint64_t t, const fix_t *fix, int have_fix)
{
    /* track state */
    float tx = a->trk.init ? a->trk.x[0] : 0.0f;
    float ty = a->trk.init ? a->trk.x[1] : 0.0f;
    float vx = a->trk.init ? a->trk.x[2] : 0.0f;
    float vy = a->trk.init ? a->trk.x[3] : 0.0f;
    float speed = sqrtf(vx*vx + vy*vy);

    printf("{\"t_ns\":%llu,\"track\":{\"x\":%.2f,\"y\":%.2f,\"vx\":%.2f,"
           "\"vy\":%.2f,\"speed\":%.2f,\"valid\":%d},\"fix\":{\"x\":%.2f,"
           "\"y\":%.2f,\"conf\":%.2f,\"n_active\":%d,\"have\":%d},\"links\":[",
           (unsigned long long)t, tx, ty, vx, vy, speed, a->trk.init,
           fix->x, fix->y, fix->confidence, fix->n_active, have_fix);

    int first = 1;
    for (int i = 0; i < a->dsp.n_links; i++) {
        link_state_t *L = &a->dsp.links[i];
        if (!L->used) continue;
        const char *nm = (L->anchor_idx >= 0) ? ANCHORS[L->anchor_idx].name : "?";
        printf("%s{\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"name\":\"%s\","
               "\"energy\":%.5f,\"doppler\":%.1f}",
               first ? "" : ",",
               L->mac[0],L->mac[1],L->mac[2],L->mac[3],L->mac[4],L->mac[5],
               nm, L->motion_energy, L->doppler_hz);
        first = 0;
    }
    printf("]}\n");
    fflush(stdout);
}

static void on_csi(const csi_wire_hdr_t *hdr, const int16_t *csi, void *user)
{
    app_t *a = (app_t*)user;
    dsp_ingest(&a->dsp, hdr, csi);

    uint64_t t = mono_ns();
    if (a->next_proc_ns == 0) a->next_proc_ns = t;
    if (t < a->next_proc_ns) return;
    a->next_proc_ns = t + (uint64_t)PROC_PERIOD_MS * 1000000ull;

    dsp_decay(&a->dsp, t);

    fix_t fix = a->last_fix;
    int have = localize(&a->dsp, &fix);
    if (have) a->last_fix = fix;

    tracker_step(&a->trk, t, have, fix.x, fix.y);
    emit(a, wall_ns(), &fix, have);   /* log timestamp = wall clock */

    /* --- optional learned body-tracking hook -------------------------------
     * To run a trained model (e.g. DensePose-from-WiFi style) instead of/along
     * with the classical fix, assemble the per-link CSI window into a tensor
     * and call your TensorRT engine here. See infer.h. Requires multi-antenna
     * CSI to be meaningful -- a single 1x1 link is not enough on its own.
     *   pose_infer(&a->dsp, &pose);
     * --------------------------------------------------------------------- */
}

static void on_sig(int s){ (void)s; g_net_run = 0; }

int main(int argc, char **argv)
{
    net_opts_t opts = { .transport = NET_TCP, .port = 9999, .channel = 1 };
    int opt;
    while ((opt = getopt(argc, argv, "t:p:C:h")) != -1) {
        switch (opt) {
            case 't':
                if      (!strcmp(optarg, "bt"))  opts.transport = NET_BT;
                else if (!strcmp(optarg, "tcp")) opts.transport = NET_TCP;
                else { fprintf(stderr, "transport must be tcp|bt\n"); return 1; }
                break;
            case 'p': opts.port = atoi(optarg); break;
            case 'C': opts.channel = atoi(optarg); break;
            default:
                fprintf(stderr,
                    "usage: %s [-t tcp|bt] [-p tcp_port] [-C rfcomm_channel]\n",
                    argv[0]);
                return 1;
        }
    }

    /* No SA_RESTART so a blocking accept()/recv() returns EINTR on Ctrl-C. */
    struct sigaction sigact = {0};
    sigact.sa_handler = on_sig;
    sigaction(SIGINT,  &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);

    app_t *a = calloc(1, sizeof(app_t));
    dsp_init(&a->dsp);
    tracker_init(&a->trk, /*q=*/0.4f, /*r=*/0.4f);

    fprintf(stderr, "[processor] grid %dx%d cells, %d anchors configured\n",
            GRID_NX, GRID_NY, N_ANCHORS);

    int rc = net_serve(&opts, on_csi, a);

    free(a);
    return rc;
}
