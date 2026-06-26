/* localize.h -- coarse (x,y) estimate from per-link motion energies.
 *
 * Each active link is a line segment TX_i -> RX. A moving person near that
 * segment perturbs link i (high motion_energy). We accumulate, over a grid, a
 * likelihood that the person is near *every* perturbed link's path; the grid
 * argmax is the estimate. With >= 2 crossing links this localizes the person.
 */
#ifndef LOCALIZE_H
#define LOCALIZE_H

#include "dsp.h"

typedef struct {
    float x, y;        /* estimated position (m)                 */
    float confidence;  /* 0..1, from peak sharpness + #links     */
    int   n_active;    /* links with motion above the floor      */
} fix_t;

/* Compute a fix from current link motion energies. Returns 1 if a usable fix
 * was produced (n_active >= 2), else 0 (fix unchanged). */
int localize(const dsp_t *d, fix_t *out);

#endif /* LOCALIZE_H */
