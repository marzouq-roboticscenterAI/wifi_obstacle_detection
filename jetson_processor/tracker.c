/* tracker.c -- standard constant-velocity Kalman filter with outlier gating
 * and physical clamping (human motion stays in-room and below a walking-speed
 * cap), so noisy single-tick fixes can't fling the track out of the building. */
#include "tracker.h"
#include "config.h"
#include <string.h>
#include <math.h>

#define MAX_SPEED_MPS  2.5f     /* brisk walk; reject/clamp faster "motion" */
#define GATE_M         1.5f     /* reject a fix this far from prediction    */

void tracker_init(tracker_t *t, float q, float r)
{
    memset(t, 0, sizeof(*t));
    t->q = q; t->r = r;
    for (int i = 0; i < 4; i++) t->P[i][i] = 10.0f;
}

void tracker_step(tracker_t *t, uint64_t now_ns, int have_meas, float mx, float my)
{
    if (!t->init) {
        if (!have_meas) return;
        t->x[0] = mx; t->x[1] = my; t->x[2] = 0; t->x[3] = 0;
        t->last_ns = now_ns;
        t->init = 1;
        return;
    }

    float dt = (float)((double)(now_ns - t->last_ns) * 1e-9);
    if (dt < 0) dt = 0;
    if (dt > 1.0f) dt = 1.0f;            /* clamp after gaps */
    t->last_ns = now_ns;

    /* ---- predict: x = F x, with mild velocity damping so the track coasts to
     * a stop during measurement gaps instead of flying into a wall. ---- */
    float *x = t->x;
    x[0] += x[2] * dt;
    x[1] += x[3] * dt;
    float damp = powf(0.6f, dt);     /* ~0.6/s decay toward stationary */
    x[2] *= damp;
    x[3] *= damp;

    /* P = F P F^T + Q  (F = [[1,0,dt,0],[0,1,0,dt],[0,0,1,0],[0,0,0,1]]) */
    float P[4][4];
    memcpy(P, t->P, sizeof(P));
    /* F P */
    for (int j = 0; j < 4; j++) {
        P[0][j] += dt * t->P[2][j];
        P[1][j] += dt * t->P[3][j];
    }
    /* (F P) F^T */
    for (int i = 0; i < 4; i++) {
        P[i][0] += dt * P[i][2];
        P[i][1] += dt * P[i][3];
    }
    /* + Q : discrete white-noise accel */
    float q = t->q;
    float dt2 = dt*dt, dt3 = dt2*dt, dt4 = dt2*dt2;
    P[0][0] += q*dt4/4.0f; P[0][2] += q*dt3/2.0f;
    P[2][0] += q*dt3/2.0f; P[2][2] += q*dt2;
    P[1][1] += q*dt4/4.0f; P[1][3] += q*dt3/2.0f;
    P[3][1] += q*dt3/2.0f; P[3][3] += q*dt2;
    memcpy(t->P, P, sizeof(P));

    if (!have_meas) { goto clamp; }

    /* Outlier gate: ignore a measurement implausibly far from the prediction
     * (rejects localization glitches/ambiguities). But if we keep rejecting,
     * the track has probably diverged -- re-acquire by snapping to the fix. */
    {
        float gx = mx - x[0], gy = my - x[1];
        if (sqrtf(gx*gx + gy*gy) > GATE_M) {
            if (++t->miss >= 6) {
                x[0] = mx; x[1] = my; x[2] = 0; x[3] = 0;
                t->miss = 0;
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++) t->P[i][j] = (i==j)?5.0f:0.0f;
            }
            goto clamp;
        }
        t->miss = 0;
    }

    /* ---- update with z = [mx,my] (H picks x,y) ---- */
    float r = t->r;
    /* innovation covariance S = H P H^T + R  (2x2) */
    float S00 = t->P[0][0] + r;
    float S01 = t->P[0][1];
    float S10 = t->P[1][0];
    float S11 = t->P[1][1] + r;
    float det = S00*S11 - S01*S10;
    if (det < 1e-9f && det > -1e-9f) return;
    float iS00 =  S11/det, iS01 = -S01/det;
    float iS10 = -S10/det, iS11 =  S00/det;

    /* Kalman gain K = P H^T S^-1  (4x2) */
    float K[4][2];
    for (int i = 0; i < 4; i++) {
        float ph0 = t->P[i][0];     /* P H^T column for x */
        float ph1 = t->P[i][1];     /* ... for y          */
        K[i][0] = ph0*iS00 + ph1*iS10;
        K[i][1] = ph0*iS01 + ph1*iS11;
    }

    float yx = mx - x[0];
    float yy = my - x[1];
    for (int i = 0; i < 4; i++)
        x[i] += K[i][0]*yx + K[i][1]*yy;

    /* P = (I - K H) P */
    float Pn[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float KH = K[i][0]*t->P[0][j] + K[i][1]*t->P[1][j];
            Pn[i][j] = t->P[i][j] - KH;
        }
    memcpy(t->P, Pn, sizeof(Pn));

clamp:
    /* keep velocity sane and position inside the room */
    {
        float sp = sqrtf(x[2]*x[2] + x[3]*x[3]);
        if (sp > MAX_SPEED_MPS) { float s = MAX_SPEED_MPS/sp; x[2]*=s; x[3]*=s; }
        if (x[0] < 0) { x[0]=0; x[2]=0; } else if (x[0] > ROOM_W_M) { x[0]=ROOM_W_M; x[2]=0; }
        if (x[1] < 0) { x[1]=0; x[3]=0; } else if (x[1] > ROOM_H_M) { x[1]=ROOM_H_M; x[3]=0; }
    }
}
