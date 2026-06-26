/* config.h -- deployment-specific geometry and tuning for the Jetson processor.
 *
 * The room/RX/anchor section below is a GENERATED block: edit it by hand, or
 * (easier) run  scripts/configure.sh  to regenerate it. Coordinates are metres
 * in a top-down (x,y) floor plane. The Pi (receiver) sits at RX_X/RX_Y; each TX
 * anchor is a known-position WiFi device identified by the MAC the Pi sees.
 *
 * You need >= 2 anchors whose TX->RX paths cross the area for a 2-D fix; 3-4
 * spread around all walls works much better.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* TX anchor descriptor (referenced by the generated block below). */
typedef struct {
    uint8_t mac[6];
    float   x, y;
    const char *name;
} anchor_t;

/* === BEGIN GENERATED DEPLOYMENT CONFIG === */
/* Room geometry, receiver (Pi) position, and TX anchors.
 * Regenerate with: scripts/configure.sh */
#define ROOM_W_M       6.0f     /* room width  (x: 0..ROOM_W_M)  */
#define ROOM_H_M       5.0f     /* room depth  (y: 0..ROOM_H_M)  */
#define GRID_RES_M     0.10f    /* localization grid cell size   */
#define RX_X           3.0f     /* Pi (receiver) x               */
#define RX_Y           0.0f     /* Pi (receiver) y               */
#define LINK_SIGMA_M   0.45f    /* TX->RX path influence width   */

/* Fill in YOUR anchors' MACs (the source MAC the Pi sees) and positions. */
static const anchor_t ANCHORS[] = {
    { {0x00,0x11,0x22,0x33,0x44,0x01}, 0.0f, 5.0f, "TX-NW" },
    { {0x00,0x11,0x22,0x33,0x44,0x02}, 6.0f, 5.0f, "TX-NE" },
    { {0x00,0x11,0x22,0x33,0x44,0x03}, 0.0f, 2.5f, "TX-W"  },
    { {0x00,0x11,0x22,0x33,0x44,0x04}, 6.0f, 2.5f, "TX-E"  },
};
#define N_ANCHORS ((int)(sizeof(ANCHORS)/sizeof(ANCHORS[0])))
/* === END GENERATED DEPLOYMENT CONFIG === */

/* ---- derived grid dimensions ------------------------------------------ */
#define GRID_NX        ((int)(ROOM_W_M / GRID_RES_M))
#define GRID_NY        ((int)(ROOM_H_M / GRID_RES_M))

/* ---- DSP / motion (tuning) -------------------------------------------- */
#define MAX_LINKS          16    /* distinct transmitter MACs tracked        */
#define MOTION_WIN         128   /* sliding window length (CSI frames)       */
#define DEAD_SUB_FRAC      0.15f /* subcarriers below this * median amp = dead*/
#define NOMINAL_CSI_HZ     200.0f/* nominal per-link CSI frame rate (Doppler) */
#define MOTION_NOISE_FLOOR 0.0008f /* motion energy below this = "no motion"  */

/* ---- output / tracking ------------------------------------------------ */
#define PROC_PERIOD_MS     50    /* run localization + tracker this often    */
#define TRACK_TIMEOUT_MS   1500  /* drop track if no motion for this long    */

#endif /* CONFIG_H */
