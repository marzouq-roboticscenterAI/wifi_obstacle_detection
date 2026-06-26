/* localize.c -- see localize.h. */
#include "localize.h"
#include <math.h>
#include <float.h>
#include <string.h>

/* shortest distance from point p to segment a-b */
static float dist_point_seg(float px, float py,
                            float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay;
    float wx = px - ax, wy = py - ay;
    float vv = vx*vx + vy*vy;
    float t  = (vv > 1e-9f) ? (wx*vx + wy*vy) / vv : 0.0f;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    float cx = ax + t*vx, cy = ay + t*vy;
    float dx = px - cx, dy = py - cy;
    return sqrtf(dx*dx + dy*dy);
}

int localize(const dsp_t *d, fix_t *out)
{
    /* Gather ALL position-known links (active and quiet). Quiet links matter:
     * a candidate cell must explain why they are quiet (person far from them),
     * which is what disambiguates the region near the common RX point. */
    float obs[MAX_LINKS], tx[MAX_LINKS], ty[MAX_LINKS];
    int   nk = 0, na = 0;
    float emax = 0.0f;
    for (int i = 0; i < d->n_links; i++) {
        const link_state_t *L = &d->links[i];
        if (!L->used || L->anchor_idx < 0) continue;
        float e = L->motion_energy - MOTION_NOISE_FLOOR;
        if (e < 0) e = 0;
        obs[nk] = e;
        tx[nk]  = ANCHORS[L->anchor_idx].x;
        ty[nk]  = ANCHORS[L->anchor_idx].y;
        if (e > 0) na++;
        if (e > emax) emax = e;
        nk++;
    }
    if (na < 2 || emax <= 0.0f) { if (out) out->n_active = na; return 0; }

    const float inv2s2 = 1.0f / (2.0f * LINK_SIGMA_M * LINK_SIGMA_M);

    /* Radio-tomographic least squares: for a candidate cell, the expected
     * energy on link k is emax * exp(-d_k^2/2sigma^2). Score = -SSE between
     * expected and observed across ALL links. Argmax = best explanation. */
    float best = -1e30f, bx = RX_X, by = RX_Y;
    double sse_sum = 0.0;

    for (int iy = 0; iy < GRID_NY; iy++) {
        float py = (iy + 0.5f) * GRID_RES_M;
        for (int ix = 0; ix < GRID_NX; ix++) {
            float px = (ix + 0.5f) * GRID_RES_M;
            float sse = 0.0f;
            for (int k = 0; k < nk; k++) {
                float dseg = dist_point_seg(px, py, tx[k], ty[k], RX_X, RX_Y);
                float pred = emax * expf(-(dseg*dseg) * inv2s2);
                float diff = obs[k] - pred;
                sse += diff * diff;
            }
            float score = -sse;
            sse_sum += sse;
            if (score > best) { best = score; bx = px; by = py; }
        }
    }

    if (out) {
        out->x = bx; out->y = by; out->n_active = na;
        /* confidence: how much better the best cell fits than an average cell,
         * normalized, scaled by link count. best = -min_sse. */
        double mean_sse = sse_sum / ((double)GRID_NX * GRID_NY + 1e-9);
        double min_sse  = -best;
        float fit = (mean_sse > 1e-12)
                  ? (float)(1.0 - min_sse / mean_sse) : 0.0f;
        if (fit < 0) fit = 0;
        if (fit > 1) fit = 1;
        float link_factor = (na >= 3) ? 1.0f : 0.7f;
        out->confidence = fit * link_factor;
    }
    return 1;
}
