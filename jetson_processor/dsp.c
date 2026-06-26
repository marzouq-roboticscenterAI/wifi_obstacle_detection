#define _POSIX_C_SOURCE 199309L   /* clock_gettime / CLOCK_MONOTONIC under -std=c11 */
/* dsp.c -- see dsp.h.
 *
 * Motion metric: for each usable subcarrier we compute the temporal variance of
 * amplitude over the sliding window, normalized by its mean^2 (so it is gain-
 * independent), then average across subcarriers. A still scene -> ~0; a moving
 * person -> a clear rise. This is the robust commodity-hardware analogue of the
 * paper's "moving reflector perturbs the channel" signal.
 *
 * Doppler: we take the strongest-variance subcarrier's amplitude time series,
 * remove its mean, and find the dominant frequency via a small Goertzel sweep.
 * That frequency scales with the radial speed of the moving body and is used
 * only as a confidence/speed cue (a 1x1 link cannot give true velocity vector).
 */
#include "dsp.h"
#include <math.h>
#include <string.h>
#include <float.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Jetson-local monotonic clock. Link staleness is measured against THIS, never
 * the Pi's timestamp -- so an unsynced/offline Pi clock can't stall tracking. */
static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int mac_eq(const uint8_t *a, const uint8_t *b){ return memcmp(a,b,6)==0; }

static int anchor_for_mac(const uint8_t *mac)
{
    for (int i = 0; i < N_ANCHORS; i++)
        if (mac_eq(ANCHORS[i].mac, mac)) return i;
    return -1;
}

void dsp_init(dsp_t *d)
{
    memset(d, 0, sizeof(*d));
}

static link_state_t *get_link(dsp_t *d, const uint8_t *mac)
{
    for (int i = 0; i < d->n_links; i++)
        if (d->links[i].used && mac_eq(d->links[i].mac, mac))
            return &d->links[i];

    if (d->n_links >= MAX_LINKS) return NULL;
    link_state_t *L = &d->links[d->n_links++];
    memset(L, 0, sizeof(*L));
    L->used = 1;
    memcpy(L->mac, mac, 6);
    L->anchor_idx = anchor_for_mac(mac);
    return L;
}

/* Update valid[] mask from the running mean: a subcarrier whose long-run mean
 * amplitude is far below the median is a guard/null/dead subcarrier. */
static void update_valid_mask(link_state_t *L)
{
    /* median of run_mean over n_sub */
    float tmp[CSI_MAX_SUBCARRIERS];
    int n = L->n_sub;
    memcpy(tmp, L->run_mean, sizeof(float)*n);
    /* simple insertion sort (n<=256, runs rarely) */
    for (int i = 1; i < n; i++) {
        float v = tmp[i]; int j = i - 1;
        while (j >= 0 && tmp[j] > v) { tmp[j+1] = tmp[j]; j--; }
        tmp[j+1] = v;
    }
    float med = tmp[n/2];
    float thr = DEAD_SUB_FRAC * med;
    for (int k = 0; k < n; k++)
        L->valid[k] = (L->run_mean[k] > thr) ? 1 : 0;
}

/* Goertzel power at normalized frequency f/fs for a length-N real signal. */
static float goertzel_power(const float *x, int N, float w)
{
    float coeff = 2.0f * cosf(w);
    float s0=0, s1=0, s2=0;
    for (int i = 0; i < N; i++) {
        s0 = x[i] + coeff*s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1*s1 + s2*s2 - coeff*s1*s2;
}

static void compute_metrics(link_state_t *L)
{
    int N = L->count;
    int n = L->n_sub;
    if (N < MOTION_WIN) { L->motion_energy = 0; L->doppler_hz = 0; return; }

    /* per-subcarrier mean and variance over the window */
    float best_var = -1.0f;
    int   best_k   = -1;
    double acc = 0.0;
    int    nvalid = 0;

    for (int k = 0; k < n; k++) {
        if (!L->valid[k]) continue;
        double m = 0.0;
        for (int t = 0; t < N; t++) m += L->amp[t][k];
        m /= N;
        if (m < 1e-6) continue;
        double v = 0.0;
        for (int t = 0; t < N; t++) { double e = L->amp[t][k] - m; v += e*e; }
        v /= N;
        double norm = v / (m*m);     /* coefficient-of-variation^2 */
        acc += norm;
        nvalid++;
        if (v > best_var) { best_var = (float)v; best_k = k; }
    }
    L->motion_energy = (nvalid > 0) ? (float)(acc / nvalid) : 0.0f;

    /* Doppler from the most-active subcarrier (mean-removed), in chrono order. */
    L->doppler_hz = 0.0f;
    if (best_k >= 0) {
        float sig[MOTION_WIN];
        float m = 0.0f;
        int start = L->head;             /* oldest sample (ring is full) */
        for (int t = 0; t < N; t++) {
            sig[t] = L->amp[(start + t) % MOTION_WIN][best_k];
            m += sig[t];
        }
        m /= N;
        for (int t = 0; t < N; t++) sig[t] -= m;

        /* sweep 0.5..15 Hz (typical human limb/torso Doppler) */
        float fs = NOMINAL_CSI_HZ;
        float best_p = 0.0f, best_f = 0.0f;
        for (float f = 0.5f; f <= 15.0f; f += 0.5f) {
            float w = 2.0f * (float)M_PI * f / fs;
            float p = goertzel_power(sig, N, w);
            if (p > best_p) { best_p = p; best_f = f; }
        }
        L->doppler_hz = best_f;
    }
}

int dsp_ingest(dsp_t *d, const csi_wire_hdr_t *hdr, const int16_t *csi)
{
    link_state_t *L = get_link(d, hdr->src_mac);
    if (!L) return -1;

    if (L->n_sub == 0) L->n_sub = hdr->n_sub;
    if (hdr->n_sub != L->n_sub) return -1;        /* bandwidth changed mid-run */
    int n = L->n_sub;

    /* amplitude per subcarrier from int16 I/Q */
    float *row = L->amp[L->head];
    for (int k = 0; k < n; k++) {
        float re = (float)csi[2*k + 0];
        float im = (float)csi[2*k + 1];
        float a  = sqrtf(re*re + im*im);
        row[k] = a;
        /* exponential running mean for the dead-subcarrier mask */
        L->run_mean[k] = (L->count == 0) ? a
                         : 0.99f * L->run_mean[k] + 0.01f * a;
    }

    L->head  = (L->head + 1) % MOTION_WIN;
    if (L->count < MOTION_WIN) L->count++;
    L->last_t_ns = mono_ns();      /* Jetson-local arrival, not the Pi's clock */

    if ((L->count & 31) == 0) update_valid_mask(L);
    compute_metrics(L);

    return (int)(L - d->links);
}

void dsp_decay(dsp_t *d, uint64_t now_ns)
{
    for (int i = 0; i < d->n_links; i++) {
        link_state_t *L = &d->links[i];
        if (!L->used) continue;
        uint64_t age_ms = (now_ns - L->last_t_ns) / 1000000ull;
        if (age_ms > TRACK_TIMEOUT_MS) { L->motion_energy = 0; L->doppler_hz = 0; }
    }
}
