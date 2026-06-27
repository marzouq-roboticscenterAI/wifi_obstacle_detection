#!/usr/bin/env bash
# run_pi.sh -- one-shot bring-up for the Raspberry Pi 5 (the CSI collector).
#   1) builds the collector
#   2) sets a static IP on the direct-Ethernet link to the Jetson
#   3) configures Nexmon CSI capture on the onboard WiFi (channel/bandwidth/filter)
#   4) launches the collector, forwarding CSI to the Jetson (foreground; Ctrl-C)
#
# REQUIRES Nexmon CSI already installed (provides `makecsiparams`/`mcp` and
# `nexutil`). See README for the Pi 5 firmware install. Run from the repo root
# or anywhere -- the script cd's to the repo.
#
# Override via env vars, e.g.:
#   CHANSPEC=36/80 JETSON_IP=192.168.100.1 ./scripts/run_pi.sh
#   MACFILTER=aa:bb:cc:dd:ee:ff ./scripts/run_pi.sh     # only this transmitter
#   TRANSPORT=bt JETSON_BDADDR=AA:BB:CC:DD:EE:FF DECIMATE=4 ./scripts/run_pi.sh
set -euo pipefail

IFACE=${IFACE:-wlan0}                  # onboard sensing radio
ETH=${ETH:-eth0}                       # data link to the Jetson
PI_IP=${PI_IP:-192.168.100.2}
JETSON_IP=${JETSON_IP:-192.168.100.1}
PORT=${PORT:-9999}

CHANSPEC=${CHANSPEC:-36/80}            # 5 GHz ch36, 80 MHz (keep WiFi data off 2.4)
COREMASK=${COREMASK:-1}               # RX core bitmask
NSSMASK=${NSSMASK:-1}                 # spatial-stream bitmask
MACFILTER=${MACFILTER:-}              # optional TX MAC filter (comma-sep, up to 4)

TRANSPORT=${TRANSPORT:-tcp}           # tcp | bt
JETSON_BDADDR=${JETSON_BDADDR:-}      # required if TRANSPORT=bt
CHANNEL=${CHANNEL:-1}                 # RFCOMM channel (bt)
DECIMATE=${DECIMATE:-1}               # forward 1/N frames per link (use >1 for bt)
SHARE_NET=${SHARE_NET:-1}             # 1 = get internet via the Jetson (default)
DNS=${DNS:-1.1.1.1}

cd "$(dirname "$0")/.."
WITH_BT=$([ "$TRANSPORT" = bt ] && echo 1 || echo 0)

# --- locate nexmon tools (often not on PATH) ---------------------------------
MCP=$(command -v makecsiparams || command -v mcp || true)
NEXUTIL=$(command -v nexutil || true)
if [ -z "$MCP" ] || [ -z "$NEXUTIL" ]; then
    echo "ERROR: nexmon CSI tools not found (makecsiparams/mcp and nexutil)." >&2
    echo "       Install Nexmon CSI first (see README), or add them to PATH." >&2
    exit 1
fi

echo "[pi] building collector (WITH_BT=$WITH_BT) ..."
make -C pi_collector WITH_BT="$WITH_BT"

# --- static IP on the Ethernet data link -------------------------------------
# Bring up the wired link + internet-via-Jetson (idempotent; you likely already
# ran `make net-pi`). For Bluetooth transport, skip the Ethernet internet route.
NP_SHARE=$([ "$TRANSPORT" = tcp ] && echo "$SHARE_NET" || echo 0)
ETH="$ETH" PI_IP="$PI_IP" JETSON_IP="$JETSON_IP" SHARE_NET="$NP_SHARE" DNS="$DNS" \
    ./scripts/net_pi.sh

# --- free the sensing radio from NetworkManager ------------------------------
# CSI capture puts wlan0 into a monitor-like state; if NetworkManager keeps
# trying to reassociate it to an AP, capture becomes unstable. Release it (this
# DROPS any normal WiFi connection on this radio -- expected) and restore on exit.
if command -v nmcli >/dev/null 2>&1; then
    echo "[pi] releasing $IFACE from NetworkManager (drops normal WiFi on it) ..."
    sudo nmcli dev set "$IFACE" managed no || true
    restore_nm() { echo "[pi] restoring $IFACE to NetworkManager ..."; sudo nmcli dev set "$IFACE" managed yes 2>/dev/null || true; }
    trap restore_nm EXIT
fi

# --- Nexmon CSI capture configuration ----------------------------------------
echo "[pi] generating CSI params (chanspec=$CHANSPEC core=$COREMASK nss=$NSSMASK${MACFILTER:+ mac=$MACFILTER}) ..."
if [ -n "$MACFILTER" ]; then
    CSIPARAMS=$("$MCP" -c "$CHANSPEC" -C "$COREMASK" -N "$NSSMASK" -m "$MACFILTER")
else
    CSIPARAMS=$("$MCP" -c "$CHANSPEC" -C "$COREMASK" -N "$NSSMASK")
fi

echo "[pi] applying capture config to $IFACE ..."
sudo ip link set "$IFACE" up
sudo "$NEXUTIL" -I"$IFACE" -s500 -b -l34 -v"$CSIPARAMS"
# Some setups also need a monitor interface to keep frames flowing; uncomment if
# your install requires it:
#   sudo iw dev "$IFACE" interface add mon0 type monitor 2>/dev/null || true
#   sudo ip link set mon0 up 2>/dev/null || true

echo "[pi] NOTE: a transmitter must be sending frames the Pi receives"
echo "     (e.g. ping-flood each anchor) for CSI to be produced."

# --- launch collector --------------------------------------------------------
if [ "$TRANSPORT" = tcp ]; then
    echo "[pi] forwarding CSI -> $JETSON_IP:$PORT (decimate=$DECIMATE)   (Ctrl-C to stop)"
    exec sudo ./pi_collector/csi_collector -i "$IFACE" -H "$JETSON_IP" -p "$PORT" -d "$DECIMATE"
else
    [ -n "$JETSON_BDADDR" ] || { echo "TRANSPORT=bt needs JETSON_BDADDR=<mac>" >&2; exit 1; }
    echo "[pi] forwarding CSI -> BT $JETSON_BDADDR ch$CHANNEL (decimate=$DECIMATE)"
    exec sudo ./pi_collector/csi_collector -i "$IFACE" -t bt -B "$JETSON_BDADDR" -C "$CHANNEL" -d "$DECIMATE"
fi
