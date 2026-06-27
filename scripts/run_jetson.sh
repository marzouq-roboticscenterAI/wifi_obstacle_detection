#!/usr/bin/env bash
# run_jetson.sh -- one-shot bring-up for the Jetson Orin Nano (the processor).
#   1) builds the processor (and the 3D visualizer if a display is available)
#   2) sets a static IP on the direct-Ethernet link to the Pi
#   3) launches the processor, displaying the live 3D view of the tracking
#
# Override via env vars, e.g.:
#   ETH=eth1 JETSON_IP=10.0.0.1 PORT=9999 ./scripts/run_jetson.sh
#   VIZ=0 ./scripts/run_jetson.sh                 # headless (JSON to stdout)
#   TRANSPORT=bt CHANNEL=1 ./scripts/run_jetson.sh
set -euo pipefail
set -o pipefail

ETH=${ETH:-eth0}
JETSON_IP=${JETSON_IP:-192.168.100.1}
PORT=${PORT:-9999}
TRANSPORT=${TRANSPORT:-tcp}      # tcp | bt
CHANNEL=${CHANNEL:-1}
VIZ=${VIZ:-auto}                 # auto | 1 | 0   (auto = on if $DISPLAY is set)
SHARE_NET=${SHARE_NET:-1}        # 1 = share the Jetson's internet to the Pi (default)
LOG=${LOG:-track.jsonl}

cd "$(dirname "$0")/.."
WITH_BT=$([ "$TRANSPORT" = bt ] && echo 1 || echo 0)

echo "[jetson] building processor (WITH_BT=$WITH_BT) ..."
make -C jetson_processor WITH_BT="$WITH_BT"

# decide whether to show the 3D viewer
VIZ_OK=0
if [ "$VIZ" != 0 ] && { [ "$VIZ" = 1 ] || [ -n "${DISPLAY:-}" ]; }; then
    echo "[jetson] building 3D visualizer ..."
    if make -C viz; then VIZ_OK=1
    else echo "[jetson] viz build failed (need: sudo apt install freeglut3-dev) -- running headless"; fi
fi

# static IP for the TCP (Ethernet/USB) transport
if [ "$TRANSPORT" = tcp ]; then
    # Bring up the wired link + internet sharing (idempotent; you likely already
    # ran `make net-jetson` before installing Nexmon on the Pi).
    ETH="$ETH" JETSON_IP="$JETSON_IP" SHARE_NET="$SHARE_NET" ./scripts/net_jetson.sh
    PROC=(./jetson_processor/csi_processor -p "$PORT")
else
    echo "[jetson] Bluetooth: make this adapter discoverable/pairable via bluetoothctl"
    PROC=(./jetson_processor/csi_processor -t bt -C "$CHANNEL")
fi

echo "[jetson] starting (Ctrl-C to stop). JSON log -> $LOG"
if [ "$VIZ_OK" = 1 ]; then
    # processor JSON -> log file -> 3D viewer
    "${PROC[@]}" | tee "$LOG" | ./viz/csi_viz
else
    exec "${PROC[@]}"
fi
