#!/usr/bin/env bash
# net_jetson.sh -- bring up the Jetson as the Pi's internet GATEWAY: static IP on
# the wired link + NAT sharing the Jetson's own internet (its WiFi/other NIC) to
# the Pi over Ethernet. Run this on the Jetson FIRST -- before the Pi's deps /
# Nexmon install -- so the Pi can be online over Ethernet (its onboard WiFi is
# about to become a CSI sensor). Does NOT start the processor; `make run-jetson`
# calls this too.
#
# Requires the Jetson to already have its own internet (connect its WiFi).
#
#   make net-jetson
#   ETH=eth0 JETSON_IP=192.168.100.1 ./scripts/net_jetson.sh
set -eo pipefail

ETH=${ETH:-eth0}
JETSON_IP=${JETSON_IP:-192.168.100.1}
SHARE_NET=${SHARE_NET:-1}

cd "$(dirname "$0")/.."

echo "[net-jetson] bringing up $ETH = $JETSON_IP/24 ..."
sudo ip link set "$ETH" up
sudo ip addr replace "$JETSON_IP/24" dev "$ETH"

if [ "$SHARE_NET" = 1 ]; then
    echo "[net-jetson] sharing the Jetson's internet to the Pi over $ETH ..."
    ETH="$ETH" JETSON_IP="$JETSON_IP" ./scripts/share_net.sh \
        || echo "[net-jetson] share-net skipped (no uplink found -- is the Jetson's WiFi up?)"
fi

echo "[net-jetson] gateway ready. Now on the Pi:  make net-pi"
