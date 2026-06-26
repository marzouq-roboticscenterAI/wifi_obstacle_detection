/* config.h -- deployment-specific geometry and tuning for the Jetson processor.
 *
 * EDIT THIS FILE for your room. Coordinates are in metres in a top-down (x,y)
 * floor plane. The Pi (receiver) sits at RX_X/RX_Y. Each transmitter anchor is
 * identified by its MAC and its known (x,y) position. The person is localized
 * by triangulating which TX->RX line-of-sight paths are being perturbed.
 *
 * You need >= 2 anchors whose paths cross the monitored area for a 2-D fix;
 * 3-4 spread around the room works much better.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ---- room / grid ------------------------------------------------------- */
#define ROOM_W_M       6.0f     /* room width  (x: 0..ROOM_W_M)             */
#define ROOM_H_M       5.0f     /* room depth  (y: 0..ROOM_H_M)             */
#define GRID_RES_M     0.10f    /* localization grid cell size (m)          */
#define GRID_NX        ((int)(ROOM_W_M / GRID_RES_M))
#define GRID_NY        ((int)(ROOM_H_M / GRID_RES_M))

/* Receiver (the Pi) position. */
#define RX_X           3.0f
#define RX_Y           0.0f

/* First-Fresnel-zone-ish influence width of a link, in metres. A person
 * within ~this distance of a TX->RX path perturbs that link. */
#define LINK_SIGMA_M   0.45f

/* ---- transmitter anchors ---------------------------------------------- */
typedef struct {
    uint8_t mac[6];
    float   x, y;
    const char *name;
} anchor_t;

/* Fill in YOUR anchors' MACs (the source MAC the Pi sees) and positions.
 * Example placeholder anchors -- REPLACE the MACs. */
static const anchor_t ANCHORS[] = {
    { {0x00,0x11,0x22,0x33,0x44,0x01}, 0.0f, 5.0f, "TX-NW" },
    { {0x00,0x11,0x22,0x33,0x44,0x02}, 6.0f, 5.0f, "TX-NE" },
    { {0x00,0x11,0x22,0x33,0x44,0x03}, 0.0f, 2.5f, "TX-W"  },
    { {0x00,0x11,0x22,0x33,0x44,0x04}, 6.0f, 2.5f, "TX-E"  },
};
#define N_ANCHORS ((int)(sizeof(ANCHORS)/sizeof(ANCHORS[0])))

/* ---- DSP / motion ----------------------------------------------------- */
#define MAX_LINKS          16    /* distinct transmitter MACs tracked        */
#define MOTION_WIN         128   /* sliding window length (CSI frames)       */
#define DEAD_SUB_FRAC      0.15f /* subcarriers below this * median amp = dead*/
#define NOMINAL_CSI_HZ     200.0f/* nominal per-link CSI frame rate (for Doppler) */

/* Motion energy below this on a link = "no motion" (link not used in fix). */
#define MOTION_NOISE_FLOOR 0.0008f

/* ---- output / tracking ------------------------------------------------ */
#define PROC_PERIOD_MS     50    /* run localization + tracker this often    */
#define TRACK_TIMEOUT_MS   1500  /* drop track if no motion for this long    */

#endif /* CONFIG_H */
