/* tracker.h -- constant-velocity 2-D Kalman filter smoothing the noisy fixes
 * into a stable track with velocity. State = [x, y, vx, vy]. */
#ifndef TRACKER_H
#define TRACKER_H

#include <stdint.h>

typedef struct {
    int   init;
    float x[4];        /* state: x, y, vx, vy            */
    float P[4][4];     /* covariance                     */
    float q;           /* process noise (accel) variance */
    float r;           /* measurement noise variance     */
    int   miss;        /* consecutive gated/missed updates */
    uint64_t last_ns;
} tracker_t;

void  tracker_init(tracker_t *t, float q, float r);
/* Advance to now_ns, then fold in measurement (mx,my) if have_meas. */
void  tracker_step(tracker_t *t, uint64_t now_ns, int have_meas, float mx, float my);

#endif /* TRACKER_H */
