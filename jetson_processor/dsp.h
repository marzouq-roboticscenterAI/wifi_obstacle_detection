/* dsp.h -- per-link CSI processing: amplitude, dead-subcarrier rejection,
 * sliding-window motion energy, and a coarse Doppler (radial speed) estimate.
 */
#ifndef DSP_H
#define DSP_H

#include <stdint.h>
#include "../common/csi_protocol.h"
#include "config.h"

typedef struct {
    int      used;
    uint8_t  mac[6];
    int      anchor_idx;        /* index into ANCHORS, or -1 if unknown MAC   */

    uint16_t n_sub;
    /* ring buffer of amplitude vectors */
    float    amp[MOTION_WIN][CSI_MAX_SUBCARRIERS];
    int      head;              /* next write index                           */
    int      count;            /* frames buffered (<= MOTION_WIN)             */

    /* long-run mean amplitude per subcarrier, for dead-subcarrier detection */
    float    run_mean[CSI_MAX_SUBCARRIERS];
    uint8_t  valid[CSI_MAX_SUBCARRIERS];  /* 1 = usable data subcarrier       */

    uint64_t last_t_ns;         /* latest arrival, Jetson CLOCK_MONOTONIC ns  */
    float    motion_energy;     /* latest normalized motion metric            */
    float    doppler_hz;        /* dominant non-DC Doppler (radial speed cue) */
} link_state_t;

typedef struct {
    link_state_t links[MAX_LINKS];
    int          n_links;
} dsp_t;

void  dsp_init(dsp_t *d);

/* Ingest one CSI record. Updates the matching link's ring buffer and, once the
 * window is full, its motion_energy + doppler_hz. Returns the link index, or
 * -1 if dropped. */
int   dsp_ingest(dsp_t *d, const csi_wire_hdr_t *hdr, const int16_t *csi);

/* Mark links stale (no recent frames) so localization ignores them. */
void  dsp_decay(dsp_t *d, uint64_t now_ns);

#endif /* DSP_H */
